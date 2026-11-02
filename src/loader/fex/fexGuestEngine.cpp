// SPDX-License-Identifier: GPL-2.0-or-later
#include "loader/fex/fexGuestEngine.h"
#include "loader/fex/fexHleBridge.h"
#include "loader/fex/iosJitAllocator.h"
#include "common/logging/log.h"
#include "common/hostException.h"
#include "graphics/host_gpu/hostMemory.h"
#include "kernel/pthread.h"
#include "kernel/semaphore.h"
#include "kernel/eventFlag.h"
#include "kernel/memory.h"
#include "loader/runtimeLinker.h"

#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/Context.h>
#include <FEXCore/Core/CoreState.h>
#include <FEXCore/Core/HostFeatures.h>
#include <FEXCore/Core/SignalDelegator.h>
#include <FEXCore/Core/X86Enums.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/Utils/ArchHelpers/Arm64.h>
#include <FEXCore/Utils/AllocatorHooks.h>

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
#include <Zydis/Zydis.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#include <sys/sysctl.h>
#include <dlfcn.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <libkern/OSCacheControl.h>
#include <mach/arm/thread_status.h>
#if !TARGET_OS_IPHONE
#include <mach/mach_vm.h>
#endif
#include <sys/ucontext.h>
#endif

#if defined(__APPLE__)
static void KytyJitWriteProtect(int enabled) {
    typedef void (*jit_protect_fn_t)(int);
    static auto fn_protect = reinterpret_cast<jit_protect_fn_t>(dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_np"));
    if (fn_protect != nullptr) {
        fn_protect(enabled);
    }
}
#endif

namespace {

#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
static thread_local FEXCore::Core::InternalThreadState* s_tls_current_fex_thread{nullptr};
static std::atomic<uint64_t> s_current_session_id{1};
static thread_local uint64_t s_tls_guest_session_id{0};

struct ActiveGuestThreadEntry {
    pthread_t         pthread_handle{};
#if defined(__APPLE__)
    mach_port_t       mach_port{MACH_PORT_NULL};
#endif
    uint64_t          session_id{0};
    std::atomic<bool> in_syscall{false};
    std::atomic<bool> exited{false};
};

static std::mutex s_active_threads_mutex;
static std::vector<std::shared_ptr<ActiveGuestThreadEntry>> s_active_guest_threads;
static thread_local std::shared_ptr<ActiveGuestThreadEntry> s_tls_active_entry;
static thread_local void (*s_tls_cleanup_before_exit)(void*){nullptr};
static thread_local void* s_tls_cleanup_arg{nullptr};

static void ExitStaleGuestThreadNow() {
    if (Common::Thread::IsMainThread()) {
        return;
    }
    Loader::Fex::FexGuestEngine::CleanupCurrentThreadBeforeExit();
    pthread_exit(nullptr);
}

struct SavedGuestThreadState {
    uint64_t gregs[16];
    uint64_t rip;
    uint32_t pf_raw;
    uint32_t af_raw;
    uint32_t mxcsr;
    uint64_t fs_cached;
    uint64_t gs_cached;
    uint64_t callret_sp;
    uint8_t flags[48];
    uint64_t avx_high[16][2];
    decltype(FEXCore::Core::CPUState::xmm) xmm;
};

static inline void SaveCpuState(const FEXCore::Core::CPUState& src, SavedGuestThreadState* dst) {
    std::memcpy(dst->gregs, src.gregs, sizeof(dst->gregs));
    dst->rip = src.rip;
    dst->pf_raw = src.pf_raw;
    dst->af_raw = src.af_raw;
    dst->mxcsr = src.mxcsr;
    dst->fs_cached = src.fs_cached;
    dst->gs_cached = src.gs_cached;
    dst->callret_sp = src.callret_sp;
    std::memcpy(dst->flags, src.flags, sizeof(dst->flags));
    std::memcpy(dst->avx_high, src.avx_high, sizeof(dst->avx_high));
    std::memcpy(&dst->xmm, &src.xmm, sizeof(dst->xmm));
}

static inline void RestoreCpuState(FEXCore::Core::CPUState* dst, const SavedGuestThreadState& src) {
    std::memcpy(dst->gregs, src.gregs, sizeof(dst->gregs));
    dst->rip = src.rip;
    dst->pf_raw = src.pf_raw;
    dst->af_raw = src.af_raw;
    dst->mxcsr = src.mxcsr;
    dst->fs_cached = src.fs_cached;
    dst->gs_cached = src.gs_cached;
    dst->callret_sp = src.callret_sp;
    std::memcpy(dst->flags, src.flags, sizeof(dst->flags));
    std::memcpy(dst->avx_high, src.avx_high, sizeof(dst->avx_high));
    std::memcpy(&dst->xmm, &src.xmm, sizeof(dst->xmm));
}
#endif

class KytySyscallHandler final : public FEXCore::HLE::SyscallHandler {
public:
    KytySyscallHandler() {
        OSABI = FEXCore::HLE::SyscallOSABI::OS_LINUX64;
    }

    uint64_t HandleSyscall(FEXCore::Core::CpuStateFrame* frame, FEXCore::HLE::SyscallArguments* args) override {
        if (Common::HostException::IsEmulationShuttingDown() ||
            Loader::Fex::FexGuestEngine::IsCurrentThreadFromStaleSession()) {
            if (Common::Thread::IsMainThread()) {
                if (frame != nullptr) {
                    frame->State.rip = Loader::Fex::FexHleBridge::Instance().GetHltAddress();
                }
                return 0;
            }
            ExitStaleGuestThreadNow();
        }
        if (frame == nullptr) {
            return 0;
        }

        // Full store barrier on entering HLE: guarantees guest writes in CPU store buffers
        // are fully visible to all cores before host synchronization or HLE functions execute.
        std::atomic_thread_fence(std::memory_order_seq_cst);

        uint64_t thunk_id = (args != nullptr) ? args->Argument[0] : frame->State.gregs[FEXCore::X86State::REG_RAX];
        const auto* fn_info = Loader::Fex::FexHleBridge::Instance().FindById(thunk_id);
        if (fn_info == nullptr || fn_info->host_fn == 0) {
            LOGF("FEX Syscall: unknown thunk_id=%" PRIu64 "\n", thunk_id);
            return 0;
        }

        uint64_t args_array[6] = {
            (args != nullptr) ? args->Argument[1] : frame->State.gregs[FEXCore::X86State::REG_RDI],
            (args != nullptr) ? args->Argument[2] : frame->State.gregs[FEXCore::X86State::REG_RSI],
            (args != nullptr) ? args->Argument[3] : frame->State.gregs[FEXCore::X86State::REG_RDX],
            (args != nullptr) ? args->Argument[4] : frame->State.gregs[FEXCore::X86State::REG_R10], // Preserved from RCX by thunk stub
            (args != nullptr) ? args->Argument[5] : frame->State.gregs[FEXCore::X86State::REG_R8],
            (args != nullptr) ? args->Argument[6] : frame->State.gregs[FEXCore::X86State::REG_R9],
        };

        uint64_t guest_rsp = frame->State.gregs[FEXCore::X86State::REG_RSP];
        uint64_t caller_rip = (guest_rsp != 0) ? *reinterpret_cast<const uint64_t*>(guest_rsp) : 0;

        if (s_tls_active_entry) {
            s_tls_active_entry->in_syscall.store(true, std::memory_order_release);
        }
        uint64_t result = Loader::Fex::FexHleBridge::Instance().DispatchHle(thunk_id, args_array, guest_rsp);
        if (s_tls_active_entry) {
            s_tls_active_entry->in_syscall.store(false, std::memory_order_release);
        }
        if (Common::HostException::IsEmulationShuttingDown() ||
            Loader::Fex::FexGuestEngine::IsCurrentThreadFromStaleSession()) {
            if (Common::Thread::IsMainThread()) {
                if (frame != nullptr) {
                    frame->State.rip = Loader::Fex::FexHleBridge::Instance().GetHltAddress();
                }
                return 0;
            }
            ExitStaleGuestThreadNow();
        }
        Loader::Fex::FexHleBridge::Instance().RecordHleCall(thunk_id, args_array, guest_rsp, result);

        frame->State.gregs[FEXCore::X86State::REG_RAX] = result;
        // Full barrier on resuming guest execution: ensures all memory written by HLE/kernel
        // is committed before the guest thread executes its next instructions.
        std::atomic_thread_fence(std::memory_order_seq_cst);
        return result;
    }

    FEXCore::HLE::ExecutableRangeInfo QueryGuestExecutableRange(FEXCore::Core::InternalThreadState*, uint64_t addr) override {
        if (addr < 0x10000) {
            return {0, 0, false};
        }
#if defined(__APPLE__)
#if TARGET_OS_IPHONE
        vm_address_t                   region_addr = static_cast<vm_address_t>(addr);
        vm_size_t                      region_size = 0;
        vm_region_basic_info_data_64_t info {};
        mach_msg_type_number_t         count       = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t                    object_name = MACH_PORT_NULL;
        kern_return_t kr = vm_region_64(mach_task_self(), &region_addr, &region_size, VM_REGION_BASIC_INFO_64,
                                        reinterpret_cast<vm_region_info_t>(&info), &count,
                                        &object_name);
        if (kr != KERN_SUCCESS || region_addr > addr || (info.protection & VM_PROT_READ) == 0) {
            return {0, 0, false};
        }
        uint64_t end = region_addr + region_size;
        return {static_cast<uint64_t>(region_addr), end - region_addr, (info.protection & VM_PROT_WRITE) != 0};
#else
        mach_vm_address_t              region_addr = addr;
        mach_vm_size_t                 region_size = 0;
        vm_region_basic_info_data_64_t info {};
        mach_msg_type_number_t         count       = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t                    object_name = MACH_PORT_NULL;
        kern_return_t kr = mach_vm_region(mach_task_self(), &region_addr, &region_size, VM_REGION_BASIC_INFO_64,
                                          reinterpret_cast<vm_region_info_t>(&info), &count,
                                          &object_name);
        if (kr != KERN_SUCCESS || region_addr > addr || (info.protection & VM_PROT_READ) == 0) {
            return {0, 0, false};
        }
        uint64_t end = region_addr + region_size;
        return {region_addr, end - region_addr, (info.protection & VM_PROT_WRITE) != 0};
#endif
#else
        void* start = nullptr;
        void* end = nullptr;
        int prot = 0;
        if (Libs::LibKernel::Memory::KernelQueryMemoryProtection(reinterpret_cast<void*>(addr), &start, &end, &prot) != 0 ||
            (prot & Libs::LibKernel::Memory::KERNEL_PROT_CPU_READ) == 0) {
            return {0, 0, false};
        }
        uint64_t u_start = reinterpret_cast<uint64_t>(start);
        uint64_t u_end = reinterpret_cast<uint64_t>(end);
        return {u_start, u_end - u_start, (prot & Libs::LibKernel::Memory::KERNEL_PROT_CPU_WRITE) != 0};
#endif
    }

    void SleepThread(FEXCore::Context::Context*, FEXCore::Core::CpuStateFrame* frame) override {
        LOGF("[FEXCore] SleepThread invoked! GuestRIP=0x%016" PRIx64 "\n", frame ? frame->State.rip : 0);
    }

    std::optional<FEXCore::ExecutableFileSectionInfo>
    LookupExecutableFileSection(FEXCore::Core::InternalThreadState*, uint64_t) override {
        return std::nullopt;
    }
};

class KytySignalDelegator final : public FEXCore::SignalDelegator {
public:
    explicit KytySignalDelegator(uintptr_t callbackReturn = 0)
        : CallbackReturn{callbackReturn} {}

    uintptr_t GetThunkCallbackRET() const override {
        if (CallbackReturn != 0) {
            return CallbackReturn;
        }
        return Loader::Fex::FexHleBridge::Instance().GetCallbackRetAddress();
    }

private:
    uintptr_t CallbackReturn;
};

} // namespace

namespace Loader::Fex {
void KytyRecordKnownBlockSnapshot(unsigned long rip, unsigned char const* data, unsigned long size) {
    (void)rip;
    (void)data;
    (void)size;
}
} // namespace Loader::Fex
#endif

namespace Loader::Fex {

FexGuestEngine& FexGuestEngine::Instance() {
    static FexGuestEngine engine;
    return engine;
}

FexGuestEngine::~FexGuestEngine() {
    Shutdown();
}

bool FexGuestEngine::Initialize() {
    if (m_initialized) {
        return true;
    }

#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
#if defined(__APPLE__) && TARGET_OS_IPHONE
    if (!Loader::Fex::KytyIsJitPoolReady()) {
        Loader::Fex::KytyAcquireJitPool();
    }
#endif
    LogMan::Msg::InstallHandler([](LogMan::DebugLevels level, const char* msg) {
        if (level <= LogMan::ERROR) {
            static std::atomic<int> s_err_count{0};
            if (++s_err_count <= 100) {
                std::printf("[FEXCore %s] %s\n", LogMan::DebugLevelStr(level), msg);
                std::fflush(stdout);
            }
        }
    });
    LogMan::Throw::InstallHandler([](const char* msg) {
        std::printf("[FEXCore Fatal] %s\n", msg);
        std::fflush(stdout);
    });

    FEXCore::Config::Initialize();
    FEXCore::Config::Load();
    FEXCore::Config::Set(FEXCore::Config::CONFIG_IS64BIT_MODE, "1");
    FEXCore::Config::Set(FEXCore::Config::CONFIG_MULTIBLOCK, "0");
    FEXCore::Config::Set(FEXCore::Config::CONFIG_MAXINST, "1000");
    FEXCore::Config::Set(FEXCore::Config::CONFIG_DISABLETELEMETRY, "1");
    // Keep software TSO disabled on ARM64 to prevent unaligned LDAR/STLR SIGBUS storms and startup stalls
    FEXCore::Config::Set(FEXCore::Config::CONFIG_TSOENABLED, "0");
    FEXCore::Config::Set(FEXCore::Config::CONFIG_HALFBARRIERTSOENABLED, "0");
    FEXCore::Config::Set(FEXCore::Config::CONFIG_DISABLEL2CACHE, "1");
    FEXCore::Config::Set(FEXCore::Config::CONFIG_DYNAMICL1CACHE, "1");

    FEXCore::HostFeatures features{};
    features.DCacheLineSize = 64;
    features.ICacheLineSize = 64;
    features.SupportsCacheMaintenanceOps = true;
    features.SupportsAVX = true; // Required for PS5 guest code (AVX/AVX128 emulated via NEON)
#ifdef __APPLE__
    auto sysctl_present = [](const char* name) -> bool {
        int32_t val = 0;
        size_t sz = sizeof(val);
        return (::sysctlbyname(name, &val, &sz, nullptr, 0) == 0 && val != 0);
    };
    if (sysctl_present("hw.optional.arm.FEAT_AES")) features.SupportsAES = true;
    if (sysctl_present("hw.optional.arm.FEAT_CRC32")) features.SupportsCRC = true;
    if (sysctl_present("hw.optional.arm.FEAT_LSE")) features.SupportsAtomics = true;
    if (sysctl_present("hw.optional.arm.FEAT_PMULL")) features.SupportsPMULL_128Bit = true;
    if (sysctl_present("hw.optional.arm.FEAT_FCMA")) features.SupportsFCMA = true;
    if (sysctl_present("hw.optional.arm.FEAT_FlagM")) features.SupportsFlagM = true;
    if (sysctl_present("hw.optional.arm.FEAT_FlagM2")) features.SupportsFlagM2 = true;
    // Keep LRCPC / LRCPC2 off for software TSO to prevent unaligned LDAPUR traps
    features.SupportsRCPC = false;
    features.SupportsTSOImm9 = false;
    if (sysctl_present("hw.optional.arm.FEAT_SHA1") || sysctl_present("hw.optional.arm.FEAT_SHA256")) features.SupportsSHA = true;
    features.SupportsAES256 = features.SupportsAVX && features.SupportsAES;
#endif

    auto ctx = FEXCore::Context::Context::CreateNewContext(features);
    if (!ctx) {
        LOGF("FexGuestEngine: Failed to create FEXCore context\n");
        return false;
    }

    uint64_t callback_ret_addr = Loader::Fex::FexHleBridge::Instance().GetCallbackRetAddress();
    auto signal_delegator = std::make_unique<KytySignalDelegator>(callback_ret_addr);
    auto syscall_handler = std::make_unique<KytySyscallHandler>();

    ctx->SetSignalDelegator(signal_delegator.get());
    ctx->SetSyscallHandler(syscall_handler.get());
    ctx->EnableExitOnHLT();

    if (!ctx->InitCore()) {
        LOGF("FexGuestEngine: Failed to initialize FEXCore core\n");
        return false;
    }

    m_fex_context = ctx.release();
    m_signal_delegator = signal_delegator.release();
    m_syscall_handler = syscall_handler.release();
    m_initialized = true;
    Common::HostException::RegisterFexSignalHandler(&FexGuestEngine::HandleHostSignal);
    return true;
#else
    LOGF("FexGuestEngine: KYTY_ENABLE_FEX_GUEST_CPU not enabled in build\n");
    return false;
#endif
}

void FexGuestEngine::Shutdown() {
#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
    Common::HostException::RegisterFexSignalHandler(nullptr);
    if (m_fex_context != nullptr) {
        auto* ctx = static_cast<FEXCore::Context::Context*>(m_fex_context);
        delete ctx;
        m_fex_context = nullptr;
    }
    if (m_signal_delegator != nullptr) {
        delete static_cast<KytySignalDelegator*>(m_signal_delegator);
        m_signal_delegator = nullptr;
    }
    if (m_syscall_handler != nullptr) {
        delete static_cast<KytySyscallHandler*>(m_syscall_handler);
        m_syscall_handler = nullptr;
    }
    FEXCore::Config::Shutdown();
#if defined(__APPLE__)
    Loader::Fex::KytyResetJitPoolForNewSession();
#endif
#endif
    m_initialized = false;
}

bool FexGuestEngine::IsCurrentThreadFromStaleSession() noexcept {
#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
    const uint64_t my_session = s_tls_guest_session_id;
    return my_session != 0 && my_session != s_current_session_id.load(std::memory_order_acquire);
#else
    return false;
#endif
}

void FexGuestEngine::BindCurrentThreadToActiveSession() noexcept {
#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
    s_tls_guest_session_id = s_current_session_id.load(std::memory_order_acquire);
#endif
}

bool FexGuestEngine::GetCurrentThreadRegisters(GuestRegisterSnapshot* out) noexcept {
#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
    auto* t = s_tls_current_fex_thread;
    if (out == nullptr || t == nullptr || t->CurrentFrame == nullptr) {
        return false;
    }
    const auto& st = t->CurrentFrame->State;
    namespace R = FEXCore::X86State;
    out->rax = st.gregs[R::REG_RAX];
    out->rbx = st.gregs[R::REG_RBX];
    out->rcx = st.gregs[R::REG_RCX];
    out->rdx = st.gregs[R::REG_RDX];
    out->rsi = st.gregs[R::REG_RSI];
    out->rdi = st.gregs[R::REG_RDI];
    out->rbp = st.gregs[R::REG_RBP];
    out->rsp = st.gregs[R::REG_RSP];
    out->r8  = st.gregs[R::REG_R8];
    out->r9  = st.gregs[R::REG_R9];
    out->r10 = st.gregs[R::REG_R10];
    out->r11 = st.gregs[R::REG_R11];
    out->r12 = st.gregs[R::REG_R12];
    out->r13 = st.gregs[R::REG_R13];
    out->r14 = st.gregs[R::REG_R14];
    out->r15 = st.gregs[R::REG_R15];
    out->rip = st.rip;
    return out->rsp >= 0x10000000;
#else
    (void)out;
    return false;
#endif
}

void FexGuestEngine::CleanupCurrentThreadBeforeExit() noexcept {
#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
    if (s_tls_active_entry) {
        s_tls_active_entry->exited.store(true, std::memory_order_release);
        s_tls_active_entry.reset();
    }
    s_tls_current_fex_thread = nullptr;
    if (s_tls_cleanup_before_exit != nullptr) {
        auto fn = s_tls_cleanup_before_exit;
        void* arg = s_tls_cleanup_arg;
        s_tls_cleanup_before_exit = nullptr;
        s_tls_cleanup_arg = nullptr;
        fn(arg);
    }
#endif
}

void FexGuestEngine::StopAllThreads() {
#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
    Common::HostException::SetEmulationShuttingDown(true);
    const uint64_t finished_session = s_current_session_id.fetch_add(1, std::memory_order_seq_cst);

    // Give active threads in syscalls or condvar waits a cooperative window to notice shutdown and exit cleanly
    for (int i = 0; i < 20; ++i) {
        bool any_running = false;
        {
            std::lock_guard<std::mutex> lock(s_active_threads_mutex);
            for (const auto& entry : s_active_guest_threads) {
                if (entry && entry->session_id <= finished_session && !entry->exited.load(std::memory_order_acquire)) {
                    any_running = true;
                    break;
                }
            }
        }
        if (!any_running) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

#if defined(__APPLE__)
    mach_port_t self_mach = mach_thread_self();
    {
        std::lock_guard<std::mutex> lock(s_active_threads_mutex);
        for (auto& entry : s_active_guest_threads) {
            if (!entry || entry->session_id > finished_session || entry->exited.load(std::memory_order_acquire)) {
                continue;
            }
            if (entry->mach_port != MACH_PORT_NULL && entry->mach_port != self_mach) {
                thread_abort(entry->mach_port);
                thread_terminate(entry->mach_port);
                entry->exited.store(true, std::memory_order_release);
            }
        }
    }
    mach_port_deallocate(mach_task_self(), self_mach);
#endif

    {
        std::lock_guard<std::mutex> lock(s_active_threads_mutex);
        for (auto& entry : s_active_guest_threads) {
            if (entry && entry->session_id <= finished_session) {
                if (entry->pthread_handle != 0 && entry->pthread_handle != pthread_self()) {
                    pthread_cancel(entry->pthread_handle);
                }
                entry->exited.store(true, std::memory_order_release);
            }
        }
        std::erase_if(s_active_guest_threads, [](const auto& entry) {
            return !entry || entry->exited.load(std::memory_order_acquire);
        });
    }

#if defined(__APPLE__)
    Loader::Fex::KytyResetJitPoolForNewSession();
#endif

    s_tls_guest_session_id = 0;
#endif
}

void FexGuestEngine::InvalidateCodeRange(uint64_t start, uint64_t length) {
#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
    if (Common::HostException::IsEmulationShuttingDown() || length == ~0ULL || length == 0) {
        return;
    }
    if (m_fex_context != nullptr) {
        if (start + length < start) {
            length = ~0ULL - start;
        }
        auto* ctx = static_cast<FEXCore::Context::Context*>(m_fex_context);
        std::unique_lock lk(ctx->GetCodeInvalidationMutex());
        ctx->InvalidateCodeBuffersCodeRange(start, length);
    }
#else
    (void)start;
    (void)length;
#endif
}

GuestThreadState* FexGuestEngine::CreateThread(uint64_t entry_rip, uint64_t stack_top, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t fs_base) {
    return CreateThreadWithArgs(entry_rip, stack_top, arg0, arg1, arg2, 0, 0, 0, fs_base);
}

GuestThreadState* FexGuestEngine::CreateThreadWithArgs(uint64_t entry_rip, uint64_t stack_top,
                                                       uint64_t arg0, uint64_t arg1, uint64_t arg2,
                                                       uint64_t arg3, uint64_t arg4, uint64_t arg5,
                                                       uint64_t fs_base) {
    if (!Initialize()) {
        return nullptr;
    }

    auto* thread_state = new GuestThreadState();
    thread_state->rip = entry_rip;
    thread_state->rsp = stack_top;
    thread_state->rbp = stack_top;
    thread_state->rdi = arg0;
    thread_state->rsi = arg1;
    thread_state->rdx = arg2;
    thread_state->rcx = arg3;
    thread_state->r8  = arg4;
    thread_state->r9  = arg5;
    thread_state->fs_base = fs_base;

#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
    auto* ctx = static_cast<FEXCore::Context::Context*>(m_fex_context);
    auto* fex_thread = ctx->CreateThread(entry_rip, stack_top);
    if (fex_thread == nullptr) {
        LOGF("FexGuestEngine: Failed to create internal FEX thread\n");
        delete thread_state;
        return nullptr;
    }

    auto& cpu_state = fex_thread->CurrentFrame->State;
    cpu_state.segment_arrays[FEXCore::Core::CPUState::SEGMENT_ARRAY_INDEX_GDT] = &cpu_state.private_gdt[0];
    cpu_state.segment_arrays[FEXCore::Core::CPUState::SEGMENT_ARRAY_INDEX_LDT] = &cpu_state.private_gdt[0];
    cpu_state.cs_idx = 0;
    cpu_state.cs_cached = 0;
    for (auto& seg : cpu_state.private_gdt) {
        seg.L = 1; // 64-bit Long Mode for PS5
        seg.D = 0;
    }
    cpu_state.rip = entry_rip;
    cpu_state.gregs[FEXCore::X86State::REG_RSP] = stack_top;
    cpu_state.gregs[FEXCore::X86State::REG_RBP] = stack_top;
    cpu_state.gregs[FEXCore::X86State::REG_RDI] = arg0;
    cpu_state.gregs[FEXCore::X86State::REG_RSI] = arg1;
    cpu_state.gregs[FEXCore::X86State::REG_RDX] = arg2;
    cpu_state.gregs[FEXCore::X86State::REG_RCX] = arg3;
    cpu_state.gregs[FEXCore::X86State::REG_R8]  = arg4;
    cpu_state.gregs[FEXCore::X86State::REG_R9]  = arg5;
    if (fs_base != 0) {
        cpu_state.fs_cached = fs_base;
    }
    fex_thread->CurrentFrame->Pointers.ThunkCallbackRet = Loader::Fex::FexHleBridge::Instance().GetCallbackRetAddress();

    if (fex_thread->CallRetStackBase == nullptr || cpu_state.callret_sp == 0) {
        size_t stack_size = FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE;
        void* callret_stack = ::mmap(nullptr, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (callret_stack == MAP_FAILED) {
            size_t fallback_sizes[] = { 128 * 1024, 64 * 1024, 32 * 1024 };
            for (size_t sz : fallback_sizes) {
                callret_stack = ::mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
                if (callret_stack != MAP_FAILED) {
                    stack_size = sz;
                    break;
                }
            }
        }
        if (callret_stack == MAP_FAILED) {
            stack_size = 64 * 1024;
            callret_stack = ::malloc(stack_size);
            fex_thread->CallRetStackFromHeap = true;
        }
        if (callret_stack != nullptr && callret_stack != MAP_FAILED) {
            fex_thread->CallRetStackBase = callret_stack;
            fex_thread->CallRetStackAllocSize = stack_size;
            cpu_state.callret_sp = reinterpret_cast<uint64_t>(callret_stack) + (stack_size / 2);
        }
    }

    if (cpu_state.callret_sp == 0) {
        static uint8_t emergency_callret[64 * 1024];
        cpu_state.callret_sp = reinterpret_cast<uint64_t>(emergency_callret) + 32768;
    }

    thread_state->internal_thread = fex_thread;
#endif

    return thread_state;
}

void FexGuestEngine::DestroyThread(GuestThreadState* thread) {
    if (thread == nullptr) {
        return;
    }
#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
    if (thread->internal_thread != nullptr) {
        auto* fex_thread = static_cast<FEXCore::Core::InternalThreadState*>(thread->internal_thread);
        if (fex_thread->CallRetStackBase != nullptr && fex_thread->CallRetStackBase != MAP_FAILED) {
            if (fex_thread->CallRetStackFromHeap) {
                ::free(fex_thread->CallRetStackBase);
            } else {
                ::munmap(fex_thread->CallRetStackBase, fex_thread->CallRetStackAllocSize);
            }
            fex_thread->CallRetStackBase = nullptr;
        }
        if (m_fex_context != nullptr) {
            auto* ctx = static_cast<FEXCore::Context::Context*>(m_fex_context);
            ctx->DestroyThread(fex_thread);
        }
    }
#endif
    delete thread;
}

uint64_t FexGuestEngine::Run(GuestThreadState* thread, bool enable_watchdog) {
    if (thread == nullptr) {
        return static_cast<uint64_t>(-1);
    }

#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
    if (thread->internal_thread == nullptr || m_fex_context == nullptr) {
        return static_cast<uint64_t>(-1);
    }

    auto* fex_thread = static_cast<FEXCore::Core::InternalThreadState*>(thread->internal_thread);
    auto* ctx = static_cast<FEXCore::Context::Context*>(m_fex_context);

#if defined(__APPLE__)
    typedef void (*jit_protect_fn_t)(int);
    static auto fn_protect = reinterpret_cast<jit_protect_fn_t>(dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_np"));
    if (fn_protect != nullptr) {
        fn_protect(1);
    }
#endif

    pthread_t runner_pthread = pthread_self();
    char thread_name[64] = "Unknown";
    int guest_tid = -1;
    bool is_main_thread = false;
#if defined(__APPLE__)
    mach_port_t runner_mach = pthread_mach_thread_np(runner_pthread);
    is_main_thread = (pthread_main_np() != 0);
#endif
    auto* self_pt = Libs::LibKernel::PthreadSelfOrNull();
    if (self_pt != nullptr) {
        guest_tid = Libs::LibKernel::PthreadGetthreadid();
        if (Libs::LibKernel::PthreadGetname(self_pt, thread_name) != 0 || thread_name[0] == '\0') {
            std::snprintf(thread_name, sizeof(thread_name), "guest_tid_%d", guest_tid);
        }
        if (guest_tid == 1 || std::strcmp(thread_name, "MainThread") == 0) {
            is_main_thread = true;
        }
    }

    std::atomic<bool> thread_running{true};
    std::thread watchdog;

    struct WatchdogCleanupCtx {
        std::atomic<bool>* running;
        std::thread*       watchdog_thread;
        GuestThreadState*  guest_thread;
        void             (*prev_fn)(void*);
        void*              prev_arg;
    } cleanup_ctx{&thread_running, &watchdog, thread, s_tls_cleanup_before_exit, s_tls_cleanup_arg};

    auto prev_session_id = s_tls_guest_session_id;
    s_tls_guest_session_id = s_current_session_id.load(std::memory_order_acquire);
    auto prev_active_entry = s_tls_active_entry;
    auto active_entry = std::make_shared<ActiveGuestThreadEntry>();
    active_entry->pthread_handle = runner_pthread;
#if defined(__APPLE__)
    active_entry->mach_port = runner_mach;
#endif
    active_entry->session_id = s_tls_guest_session_id;
    s_tls_active_entry = active_entry;
    {
        std::lock_guard<std::mutex> lock(s_active_threads_mutex);
        s_active_guest_threads.push_back(active_entry);
    }

    s_tls_cleanup_arg = &cleanup_ctx;
    s_tls_cleanup_before_exit = [](void* arg) {
        auto* c = static_cast<WatchdogCleanupCtx*>(arg);
        if (c != nullptr) {
            c->running->store(false, std::memory_order_relaxed);
            if (c->watchdog_thread->joinable()) {
                c->watchdog_thread->join();
            }
            if (c->guest_thread != nullptr) {
                auto* gt = c->guest_thread;
                c->guest_thread = nullptr;
                FexGuestEngine::Instance().DestroyThread(gt);
            }
            if (c->prev_fn != nullptr) {
                auto fn = c->prev_fn;
                void* parg = c->prev_arg;
                c->prev_fn = nullptr;
                c->prev_arg = nullptr;
                fn(parg);
            }
        }
    };

auto* prev_fex_thread = s_tls_current_fex_thread;
s_tls_current_fex_thread = fex_thread;
ctx->ExecuteThread(fex_thread);
s_tls_current_fex_thread = prev_fex_thread;

s_tls_cleanup_before_exit = cleanup_ctx.prev_fn;
s_tls_cleanup_arg = cleanup_ctx.prev_arg;
active_entry->exited.store(true, std::memory_order_release);
s_tls_active_entry = prev_active_entry;
if (prev_active_entry == nullptr) {
    s_tls_guest_session_id = 0;
} else {
    s_tls_guest_session_id = prev_session_id;
}

if (is_main_thread && enable_watchdog) {
    thread_running.store(false, std::memory_order_relaxed);
    if (watchdog.joinable()) {
        watchdog.join();
    }
}

    uint64_t result = fex_thread->CurrentFrame->State.gregs[FEXCore::X86State::REG_RAX];
    return result;
#else
    LOGF("FexGuestEngine: Cannot run guest without KYTY_ENABLE_FEX_GUEST_CPU\n");
    return static_cast<uint64_t>(-1);
#endif
}

uint64_t FexGuestEngine::CallGuestFunction(uint64_t rip,
                                           uint64_t arg0, uint64_t arg1, uint64_t arg2,
                                           uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    if (rip == 0) {
        return 0;
    }
    uint64_t target_rip = Libs::LibKernel::Memory::KernelTranslateAddress(rip);
    void* q_start = nullptr;
    void* q_end = nullptr;
    int q_prot = 0;
    if (target_rip < 0x10000 ||
        Libs::LibKernel::Memory::KernelQueryMemoryProtection(
            reinterpret_cast<void*>(target_rip), &q_start, &q_end, &q_prot) != 0 ||
        (q_prot & Libs::LibKernel::Memory::KERNEL_PROT_CPU_EXEC) == 0) {
        LOGF_COLOR(Log::Color::Red,
                   "[FexGuestEngine] CallGuestFunction: target_rip=0x%016llx is not executable (prot=0x%x)\n",
                   (unsigned long long)target_rip, q_prot);
        return 0;
    }

#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
    auto* current_fex_thread = s_tls_current_fex_thread;
    if (current_fex_thread != nullptr && current_fex_thread->CurrentFrame != nullptr && m_fex_context != nullptr) {
        auto* frame = current_fex_thread->CurrentFrame;
        if (frame->Pointers.ThunkCallbackRet == 0) {
            frame->Pointers.ThunkCallbackRet = Loader::Fex::FexHleBridge::Instance().GetCallbackRetAddress();
        }
        SavedGuestThreadState saved_state{};
        SaveCpuState(frame->State, &saved_state);

        frame->State.gregs[FEXCore::X86State::REG_RDI] = arg0;
        frame->State.gregs[FEXCore::X86State::REG_RSI] = arg1;
        frame->State.gregs[FEXCore::X86State::REG_RDX] = arg2;
        frame->State.gregs[FEXCore::X86State::REG_RCX] = arg3;
        frame->State.gregs[FEXCore::X86State::REG_R8]  = arg4;
        frame->State.gregs[FEXCore::X86State::REG_R9]  = arg5;

        auto* ctx = static_cast<FEXCore::Context::Context*>(m_fex_context);
        ctx->HandleCallback(current_fex_thread, target_rip);

        uint64_t ret = frame->State.gregs[FEXCore::X86State::REG_RAX];

        // Restore caller CPU register state so nested callbacks cannot corrupt caller registers (rbp, rbx, rsp, xmm, flags)
        RestoreCpuState(&frame->State, saved_state);

        return ret;
    }

    uint64_t result = 0;
    auto* caller_thread = Libs::LibKernel::PthreadSelfOrNull();
    std::thread helper([&, caller_thread, target_rip]() {
        auto* previous_self = (caller_thread != nullptr) ? Libs::LibKernel::PthreadSwapSelfForSignal(caller_thread) : nullptr;

        constexpr uint64_t stack_size = 0x40000;
        uint64_t stack_base = Libs::LibKernel::Memory::AllocateGuestStackMemory(
            0, stack_size, Common::VirtualMemory::Mode::ReadWrite, "guest_call_stack");
        if (stack_base != 0) {
            uint64_t stack_top = (stack_base + stack_size - 0x80) & ~0xFull;
            stack_top -= 8;
            *reinterpret_cast<uint64_t*>(stack_top) =
                Loader::Fex::FexHleBridge::Instance().GetHltAddress();

            uint64_t fs_base = Loader::RuntimeLinker::GetMainTlsBase();

            auto* fex_thread = CreateThreadWithArgs(
                target_rip, stack_top, arg0, arg1, arg2, arg3, arg4, arg5, fs_base);
            if (fex_thread != nullptr) {
                result = Run(fex_thread, false);
                DestroyThread(fex_thread);
            }
            Libs::LibKernel::Memory::FreeGuestMemory(stack_base, stack_size);
        }

        if (previous_self != nullptr) {
            Libs::LibKernel::PthreadSwapSelfForSignal(previous_self);
        }
    });

    if (helper.joinable()) {
        helper.join();
    }
    return result;
#else
    LOGF("FexGuestEngine: CallGuestFunction called without KYTY_ENABLE_FEX_GUEST_CPU\n");
    return 0;
#endif
}

struct alignas(64) StripedAtomicLock {
    std::mutex mtx;
};
static StripedAtomicLock s_striped_atomic_locks[256];

static inline std::mutex& GetAtomicLockForAddress(uintptr_t addr) {
    size_t bucket = (addr >> 6) ^ (addr >> 14);
    return s_striped_atomic_locks[bucket & 255].mtx;
}

bool FexGuestEngine::HandleHostSignal(int sig, void* si_ptr, void* ucontext) {
#if defined(KYTY_ENABLE_FEX_GUEST_CPU) && (defined(__arm64__) || defined(__aarch64__)) && defined(__APPLE__)
    auto* thread = s_tls_current_fex_thread;
    auto* engine_instance = &Instance();
    if (thread == nullptr || engine_instance->m_fex_context == nullptr) {
        return false;
    }
    auto* ctx = static_cast<FEXCore::Context::Context*>(engine_instance->m_fex_context);
    auto* uc = static_cast<ucontext_t*>(ucontext);
    if (uc == nullptr || uc->uc_mcontext == nullptr) {
        return false;
    }
    auto& ss = const_cast<decltype(uc->uc_mcontext->__ss)&>(uc->uc_mcontext->__ss);
    uintptr_t pc = arm_thread_state64_get_pc(ss);

    // 1. Recover from branch to NULL (PC == 0) caused by unlinked or failed JIT block compilation
    if ((sig == SIGSEGV || sig == SIGBUS) && pc == 0 && thread->CurrentFrame != nullptr) {
        static thread_local uint64_t s_last_null_rip = 0;
        static thread_local int s_null_branch_count = 0;
        uintptr_t loop_top = thread->CurrentFrame->Pointers.DispatcherLoopTop;
        uint64_t cur_rip = thread->CurrentFrame->State.rip;

        auto& bridge = Loader::Fex::FexHleBridge::Instance();
        if (bridge.IsThunkAddress(cur_rip)) {
            uint64_t caller_rip = bridge.GetCurrentCallerRip();
            if (caller_rip == 0) {
                uint64_t rsp = thread->CurrentFrame->State.gregs[FEXCore::X86State::REG_RSP];
                if (rsp >= 8 && rsp <= 0x800000000000ULL) {
                    uint64_t cand1 = *reinterpret_cast<const uint64_t*>(rsp - 8);
                    uint64_t cand2 = *reinterpret_cast<const uint64_t*>(rsp);
                    if (cand1 >= 0x10000000 && !bridge.IsThunkAddress(cand1)) {
                        caller_rip = cand1;
                    } else if (cand2 >= 0x10000000 && !bridge.IsThunkAddress(cand2)) {
                        caller_rip = cand2;
                    }
                }
            }
            if (caller_rip != 0 && loop_top != 0) {
                thread->CurrentFrame->State.rip = caller_rip;
                arm_thread_state64_set_pc_fptr(ss, reinterpret_cast<void*>(loop_top));
                return true;
            }
        }

        if (cur_rip == s_last_null_rip) {
            s_null_branch_count++;
        } else {
            s_last_null_rip = cur_rip;
            s_null_branch_count = 1;
        }
        if (s_null_branch_count > 3) {
            return false;
        }
        if (loop_top != 0) {
            arm_thread_state64_set_pc_fptr(ss, reinterpret_cast<void*>(loop_top));
            return true;
        }
    }

    if (sig != SIGBUS && sig != SIGILL) {
        return false;
    }
    const auto* si = static_cast<const siginfo_t*>(si_ptr);
    if (sig == SIGBUS && si != nullptr && si->si_code != BUS_ADRALN) {
        return false;
    }

    if (sig == SIGILL) {
        if (!ctx->IsAddressInCodeBuffer(thread, pc)) {
            return false;
        }
        uint64_t cur_rip = thread->CurrentFrame->State.rip;
        if (cur_rip != 0) {
            const auto* code = reinterpret_cast<const uint8_t*>(cur_rip);

            // If UD2 (0x0F 0x0B), skip 2 bytes and continue execution at next guest instruction
            if (code[0] == 0x0F && code[1] == 0x0B) {
                thread->CurrentFrame->State.rip += 2;
                uintptr_t loop_top = thread->CurrentFrame->Pointers.DispatcherLoopTop;
                if (loop_top != 0) {
                    arm_thread_state64_set_pc_fptr(ss, reinterpret_cast<void*>(loop_top));
                    return true;
                }
            }
        }

        // Trap recovery (UDF #0 from unlinked thunks, HLT #0 from dispatcher fault dance, etc.)
        static thread_local uintptr_t s_last_trap_pc = 0;
        static thread_local uint64_t  s_last_trap_rip = 0;
        static thread_local int       s_trap_repeat_count = 0;

        if (s_last_trap_pc == pc && s_last_trap_rip == cur_rip) {
            s_trap_repeat_count++;
        } else {
            s_last_trap_pc = pc;
            s_last_trap_rip = cur_rip;
            s_trap_repeat_count = 1;
        }

        if (s_trap_repeat_count <= 5 && thread->CurrentFrame != nullptr) {
            uintptr_t loop_top = thread->CurrentFrame->Pointers.DispatcherLoopTop;
            if (loop_top != 0) {
                arm_thread_state64_set_pc_fptr(ss, reinterpret_cast<void*>(loop_top));
                return true;
            }
        }

        return false;
    }

    if (!ctx->IsAddressInCodeBuffer(thread, pc)) {
        return false;
    }

    uintptr_t write_pc = reinterpret_cast<uintptr_t>(FEXCore::Allocator::GetWritableAddress(reinterpret_cast<void*>(pc)));
    if (write_pc == 0) {
        write_pc = pc;
    }

    // Attempt FEXCore's unaligned access handler (NonAtomic)
    // to backpatch atomic instructions so subsequent executions never trap again.
    uint64_t gprs[31];
    for (int i = 0; i < 29; ++i) {
        gprs[i] = ss.__x[i];
    }
    gprs[29] = arm_thread_state64_get_fp(ss);
    gprs[30] = arm_thread_state64_get_lr(ss);

#if defined(__APPLE__) && !TARGET_OS_IPHONE
    KytyJitWriteProtect(0);
#endif

    auto result = FEXCore::ArchHelpers::Arm64::HandleUnalignedAccess(
        thread, FEXCore::ArchHelpers::Arm64::UnalignedHandlerType::NonAtomic,
        write_pc, gprs, true);

#if defined(__APPLE__) && !TARGET_OS_IPHONE
    KytyJitWriteProtect(1);
#endif

    if (result.has_value()) {
        for (int i = 0; i < 29; ++i) {
            ss.__x[i] = gprs[i];
        }
        arm_thread_state64_set_fp(ss, gprs[29]);
        arm_thread_state64_set_lr_fptr(ss, reinterpret_cast<void*>(gprs[30]));
        arm_thread_state64_set_pc_fptr(ss, reinterpret_cast<void*>(pc + result.value()));

        __builtin___clear_cache(reinterpret_cast<char*>(pc - 4), reinterpret_cast<char*>(pc + 8));
        sys_icache_invalidate(reinterpret_cast<void*>(pc - 4), 16);
        return true;
    }

    // Emulate unaligned ARM64 loads and stores for vector or regular load/store instructions
    // not handled by FEXCore's HandleUnalignedAccess.
    auto& ns = uc->uc_mcontext->__ns;
    uint32_t instr = *reinterpret_cast<const uint32_t*>(write_pc);

    auto get_gpr = [&](uint32_t reg) -> uint64_t {
        if (reg == 31) return arm_thread_state64_get_sp(ss);
        if (reg == 29) return arm_thread_state64_get_fp(ss);
        if (reg == 30) return arm_thread_state64_get_lr(ss);
        return ss.__x[reg];
    };

    auto set_gpr = [&](uint32_t reg, uint64_t val) {
        if (reg == 31) {
            arm_thread_state64_set_sp(ss, val);
        } else if (reg == 29) {
            arm_thread_state64_set_fp(ss, val);
        } else if (reg == 30) {
            arm_thread_state64_set_lr_fptr(ss, reinterpret_cast<void*>(val));
        } else {
            ss.__x[reg] = val;
        }
    };

    // Safe copy helpers using Mach VM APIs when available so that reads/writes
    // to unmapped or invalid guest ranges return gracefully without faulting.
    auto SafeCopyFromGuest = [](void* dst, const void* src, size_t size) {
#if defined(__APPLE__)
        vm_size_t outsize = 0;
        kern_return_t kr = vm_read_overwrite(mach_task_self(), reinterpret_cast<vm_address_t>(src), size,
                                              reinterpret_cast<vm_address_t>(dst), &outsize);
        if (kr == KERN_SUCCESS && outsize == size) {
            return;
        }
        std::memset(dst, 0, size);
#else
        auto* d = static_cast<uint8_t*>(dst);
        const auto* s = static_cast<const volatile uint8_t*>(src);
        for (size_t i = 0; i < size; ++i) {
            d[i] = s[i];
        }
#endif
    };

    auto SafeCopyToGuest = [](void* dst, const void* src, size_t size) {
#if defined(__APPLE__)
        kern_return_t kr = vm_write(mach_task_self(), reinterpret_cast<vm_address_t>(dst),
                                    reinterpret_cast<vm_offset_t>(src), static_cast<mach_msg_type_number_t>(size));
        if (kr == KERN_SUCCESS) {
            return;
        }
#else
        auto* d = static_cast<volatile uint8_t*>(dst);
        const auto* s = static_cast<const uint8_t*>(src);
        for (size_t i = 0; i < size; ++i) {
            d[i] = s[i];
        }
#endif
    };

    bool emulated = false;
    uintptr_t target_addr = 0;

    // Category 1: Load/Store Register (register offset)
    // e.g. STR S19, [X10, X4, SXTX #2] (0xbc24f953), LDR/STR B/H/S/D/Q/W/X
    if ((instr & 0x3b200c00) == 0x38200800) {
        uint32_t size = (instr >> 30) & 3;
        uint32_t v = (instr >> 26) & 1;
        uint32_t opc = (instr >> 22) & 3;
        uint32_t rm = (instr >> 16) & 0x1f;
        uint32_t option = (instr >> 13) & 7;
        uint32_t s = (instr >> 12) & 1;
        uint32_t rn = (instr >> 5) & 0x1f;
        uint32_t rt = instr & 0x1f;

        uint64_t rm_val = (rm == 31 ? 0 : get_gpr(rm));
        uint64_t offset = 0;
        switch (option) {
            case 0b000: offset = static_cast<uint8_t>(rm_val); break;
            case 0b001: offset = static_cast<uint16_t>(rm_val); break;
            case 0b010: offset = static_cast<uint32_t>(rm_val); break;
            case 0b011: offset = rm_val; break;
            case 0b100: offset = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(rm_val))); break;
            case 0b101: offset = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(rm_val))); break;
            case 0b110: offset = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(rm_val))); break;
            case 0b111: default: offset = rm_val; break;
        }

        uint32_t shift = (v && (opc & 2)) ? 4 : size;
        if (s) {
            offset <<= shift;
        }

        target_addr = get_gpr(rn) + offset;
        size_t bytes = (v && (opc & 2)) ? 16 : (1 << size);
        if (target_addr >= 0x1000) {
            if (v) { // SIMD / FP
                if (opc & 2) { // Q (128-bit)
                    if (opc & 1) { // LDR Q
                        SafeCopyFromGuest(&ns.__v[rt], reinterpret_cast<const void*>(target_addr), 16);
                    } else { // STR Q
                        SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &ns.__v[rt], 16);
                    }
                    emulated = true;
                } else { // B, H, S, D
                    size_t bytes = 1 << size;
                    if (opc & 1) { // LDR
                        ns.__v[rt] = 0;
                        SafeCopyFromGuest(&ns.__v[rt], reinterpret_cast<const void*>(target_addr), bytes);
                    } else { // STR
                        SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &ns.__v[rt], bytes);
                    }
                    emulated = true;
                }
            } else { // GPR
                size_t bytes = 1 << size;
                if (opc == 0) { // STR
                    uint64_t val = (rt == 31 ? 0 : get_gpr(rt));
                    SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &val, bytes);
                    emulated = true;
                } else if (opc == 1) { // LDR (zero-extended)
                    uint64_t val = 0;
                    SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), bytes);
                    if (rt != 31) set_gpr(rt, val);
                    emulated = true;
                } else if (opc == 2) { // LDRS to 64-bit X
                    if (size == 0) {
                        int8_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 1);
                        if (rt != 31) set_gpr(rt, static_cast<uint64_t>(static_cast<int64_t>(val)));
                    } else if (size == 1) {
                        int16_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 2);
                        if (rt != 31) set_gpr(rt, static_cast<uint64_t>(static_cast<int64_t>(val)));
                    } else if (size == 2) { // LDRSW
                        int32_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 4);
                        if (rt != 31) set_gpr(rt, static_cast<uint64_t>(static_cast<int64_t>(val)));
                    }
                    emulated = true;
                } else if (opc == 3) { // LDRS to 32-bit W
                    if (size == 0) {
                        int8_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 1);
                        if (rt != 31) set_gpr(rt, static_cast<uint32_t>(val));
                    } else if (size == 1) {
                        int16_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 2);
                        if (rt != 31) set_gpr(rt, static_cast<uint32_t>(val));
                    }
                    emulated = true;
                }
            }
        }
    }
    // Category 2: Load/Store Register (unsigned scaled immediate)
    // e.g. LDR/STR B/H/S/D/Q/W/X [Xn, #imm12 * scale]
    else if ((instr & 0x3b000000) == 0x39000000) {
        uint32_t size = (instr >> 30) & 3;
        uint32_t v = (instr >> 26) & 1;
        uint32_t opc = (instr >> 22) & 3;
        uint32_t rn = (instr >> 5) & 0x1f;
        uint32_t rt = instr & 0x1f;
        uint32_t imm12 = (instr >> 10) & 0xfff;

        uint32_t scale = (v && (opc & 2)) ? 16 : (1 << size);
        target_addr = get_gpr(rn) + static_cast<uint64_t>(imm12) * scale;
        if (target_addr >= 0x1000) {
            if (v) { // SIMD / FP
                if (opc & 2) { // Q (128-bit)
                    if (opc & 1) { // LDR Q
                        SafeCopyFromGuest(&ns.__v[rt], reinterpret_cast<const void*>(target_addr), 16);
                    } else { // STR Q
                        SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &ns.__v[rt], 16);
                    }
                    emulated = true;
                } else { // B, H, S, D
                    size_t bytes = 1 << size;
                    if (opc & 1) { // LDR
                        ns.__v[rt] = 0;
                        SafeCopyFromGuest(&ns.__v[rt], reinterpret_cast<const void*>(target_addr), bytes);
                    } else { // STR
                        SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &ns.__v[rt], bytes);
                    }
                    emulated = true;
                }
            } else { // GPR
                size_t bytes = 1 << size;
                if (opc == 0) { // STR
                    uint64_t val = (rt == 31 ? 0 : get_gpr(rt));
                    SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &val, bytes);
                    emulated = true;
                } else if (opc == 1) { // LDR
                    uint64_t val = 0;
                    SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), bytes);
                    if (rt != 31) set_gpr(rt, val);
                    emulated = true;
                } else if (opc == 2) { // LDRS to 64-bit
                    if (size == 0) {
                        int8_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 1);
                        if (rt != 31) set_gpr(rt, static_cast<uint64_t>(static_cast<int64_t>(val)));
                    } else if (size == 1) {
                        int16_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 2);
                        if (rt != 31) set_gpr(rt, static_cast<uint64_t>(static_cast<int64_t>(val)));
                    } else if (size == 2) { // LDRSW
                        int32_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 4);
                        if (rt != 31) set_gpr(rt, static_cast<uint64_t>(static_cast<int64_t>(val)));
                    }
                    emulated = true;
                } else if (opc == 3) { // LDRS to 32-bit
                    if (size == 0) {
                        int8_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 1);
                        if (rt != 31) set_gpr(rt, static_cast<uint32_t>(val));
                    } else if (size == 1) {
                        int16_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 2);
                        if (rt != 31) set_gpr(rt, static_cast<uint32_t>(val));
                    }
                    emulated = true;
                }
            }
        }
    }
    // Category 3: Load/Store Register (unscaled immediate LDUR/STUR, pre-index, post-index)
    else if ((instr & 0x3b200000) == 0x38000000) {
        uint32_t size = (instr >> 30) & 3;
        uint32_t v = (instr >> 26) & 1;
        uint32_t opc = (instr >> 22) & 3;
        uint32_t rn = (instr >> 5) & 0x1f;
        uint32_t rt = instr & 0x1f;
        int32_t imm9 = (static_cast<int32_t>(instr) << 11) >> 23;
        uint32_t idx_mode = (instr >> 10) & 3; // 00=unscaled, 01=post-index, 11=pre-index

        target_addr = (idx_mode == 0b01) ? get_gpr(rn) : (get_gpr(rn) + imm9);
        size_t bytes = (v && (opc & 2)) ? 16 : (1 << size);
        if (target_addr >= 0x1000) {
            if (v) { // SIMD / FP
                if (opc & 2) { // Q (128-bit)
                    if (opc & 1) { // LDUR Q
                        SafeCopyFromGuest(&ns.__v[rt], reinterpret_cast<const void*>(target_addr), 16);
                    } else { // STUR Q
                        SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &ns.__v[rt], 16);
                    }
                    emulated = true;
                } else { // B, H, S, D
                    size_t bytes = 1 << size;
                    if (opc & 1) { // LDUR
                        ns.__v[rt] = 0;
                        SafeCopyFromGuest(&ns.__v[rt], reinterpret_cast<const void*>(target_addr), bytes);
                    } else { // STUR
                        SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &ns.__v[rt], bytes);
                    }
                    emulated = true;
                }
            } else { // GPR
                size_t bytes = 1 << size;
                if (opc == 0) { // STUR
                    uint64_t val = (rt == 31 ? 0 : get_gpr(rt));
                    SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &val, bytes);
                    emulated = true;
                } else if (opc == 1) { // LDUR
                    uint64_t val = 0;
                    SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), bytes);
                    if (rt != 31) set_gpr(rt, val);
                    emulated = true;
                } else if (opc == 2) { // LDURSW / LDURS 64-bit
                    if (size == 0) {
                        int8_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 1);
                        if (rt != 31) set_gpr(rt, static_cast<uint64_t>(static_cast<int64_t>(val)));
                    } else if (size == 1) {
                        int16_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 2);
                        if (rt != 31) set_gpr(rt, static_cast<uint64_t>(static_cast<int64_t>(val)));
                    } else if (size == 2) { // LDURSW
                        int32_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 4);
                        if (rt != 31) set_gpr(rt, static_cast<uint64_t>(static_cast<int64_t>(val)));
                    }
                    emulated = true;
                } else if (opc == 3) { // LDURS 32-bit
                    if (size == 0) {
                        int8_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 1);
                        if (rt != 31) set_gpr(rt, static_cast<uint32_t>(val));
                    } else if (size == 1) {
                        int16_t val = 0;
                        SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), 2);
                        if (rt != 31) set_gpr(rt, static_cast<uint32_t>(val));
                    }
                    emulated = true;
                }
            }

            if (emulated) {
                if (idx_mode == 0b01) {
                    set_gpr(rn, get_gpr(rn) + imm9);
                } else if (idx_mode == 0b11) {
                    set_gpr(rn, target_addr);
                }
            }
        }
    }
    // Category 4: Load/Store Register Pair (LDP / STP)
    else if ((instr & 0x3a000000) == 0x28000000) {
        uint32_t opc = (instr >> 30) & 3;
        uint32_t v = (instr >> 26) & 1;
        uint32_t type = (instr >> 23) & 7; // 001=post-index, 010=signed offset, 011=pre-index
        uint32_t l = (instr >> 22) & 1;
        int32_t imm7 = (static_cast<int32_t>(instr) << 10) >> 25;
        uint32_t rt2 = (instr >> 10) & 0x1f;
        uint32_t rn = (instr >> 5) & 0x1f;
        uint32_t rt1 = instr & 0x1f;

        uint32_t scale = 0;
        if (v) {
            scale = (opc == 2 ? 16 : (opc == 1 ? 8 : 4));
        } else {
            scale = (opc == 2 ? 8 : 4);
        }

        int64_t offset = static_cast<int64_t>(imm7) * scale;
        target_addr = (type == 0b001) ? get_gpr(rn) : (get_gpr(rn) + offset);
        if (target_addr >= 0x1000) {
            if (v) { // SIMD / FP pair
                size_t bytes = scale;
                if (l == 1) { // LDP
                    if (bytes < 16) {
                        ns.__v[rt1] = 0;
                        ns.__v[rt2] = 0;
                    }
                    SafeCopyFromGuest(&ns.__v[rt1], reinterpret_cast<const void*>(target_addr), bytes);
                    SafeCopyFromGuest(&ns.__v[rt2], reinterpret_cast<const void*>(target_addr + bytes), bytes);
                } else { // STP
                    SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &ns.__v[rt1], bytes);
                    SafeCopyToGuest(reinterpret_cast<void*>(target_addr + bytes), &ns.__v[rt2], bytes);
                }
                emulated = true;
            } else { // GPR pair
                if (opc == 0) { // 32-bit W pair
                    if (l == 1) {
                        uint32_t val1 = 0, val2 = 0;
                        SafeCopyFromGuest(&val1, reinterpret_cast<const void*>(target_addr), 4);
                        SafeCopyFromGuest(&val2, reinterpret_cast<const void*>(target_addr + 4), 4);
                        if (rt1 != 31) set_gpr(rt1, val1);
                        if (rt2 != 31) set_gpr(rt2, val2);
                    } else {
                        uint32_t val1 = (rt1 == 31 ? 0 : static_cast<uint32_t>(get_gpr(rt1)));
                        uint32_t val2 = (rt2 == 31 ? 0 : static_cast<uint32_t>(get_gpr(rt2)));
                        SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &val1, 4);
                        SafeCopyToGuest(reinterpret_cast<void*>(target_addr + 4), &val2, 4);
                    }
                    emulated = true;
                } else if (opc == 2) { // 64-bit X pair
                    if (l == 1) {
                        uint64_t val1 = 0, val2 = 0;
                        SafeCopyFromGuest(&val1, reinterpret_cast<const void*>(target_addr), 8);
                        SafeCopyFromGuest(&val2, reinterpret_cast<const void*>(target_addr + 8), 8);
                        if (rt1 != 31) set_gpr(rt1, val1);
                        if (rt2 != 31) set_gpr(rt2, val2);
                    } else {
                        uint64_t val1 = (rt1 == 31 ? 0 : get_gpr(rt1));
                        uint64_t val2 = (rt2 == 31 ? 0 : get_gpr(rt2));
                        SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &val1, 8);
                        SafeCopyToGuest(reinterpret_cast<void*>(target_addr + 8), &val2, 8);
                    }
                    emulated = true;
                } else if (opc == 1) { // LDPSW pair
                    if (l == 1) {
                        int32_t val1 = 0, val2 = 0;
                        SafeCopyFromGuest(&val1, reinterpret_cast<const void*>(target_addr), 4);
                        SafeCopyFromGuest(&val2, reinterpret_cast<const void*>(target_addr + 4), 4);
                        if (rt1 != 31) set_gpr(rt1, static_cast<uint64_t>(static_cast<int64_t>(val1)));
                        if (rt2 != 31) set_gpr(rt2, static_cast<uint64_t>(static_cast<int64_t>(val2)));
                    }
                    emulated = true;
                }
            }

            if (emulated) {
                if (type == 0b001) { // post-index
                    set_gpr(rn, get_gpr(rn) + offset);
                } else if (type == 0b011) { // pre-index
                    set_gpr(rn, target_addr);
                }
            }
        }
    }
    // Category 5: Atomic Load-Acquire / Store-Release (LDAR, STLR, LDAPR)
    else if ((instr & 0x3fff'fc00) == 0x08df'fc00 ||  // LDAR*
             (instr & 0x3fff'fc00) == 0x38bf'c000 ||  // LDAPR*
             (instr & 0x3fff'fc00) == 0x089f'fc00) {  // STLR*
        uint32_t size = (instr >> 30) & 3;
        uint32_t rn = (instr >> 5) & 0x1f;
        uint32_t rt = instr & 0x1f;
        size_t bytes = 1 << size;
        target_addr = get_gpr(rn);
        bool is_store = ((instr & 0x3fff'fc00) == 0x089f'fc00);

        if (target_addr >= 0x1000) {
            std::atomic_thread_fence(std::memory_order_seq_cst);
            if (is_store) {
                uint64_t val = (rt == 31 ? 0 : get_gpr(rt));
                SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &val, bytes);
            } else {
                uint64_t val = 0;
                SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), bytes);
                if (rt != 31) set_gpr(rt, val);
            }
            std::atomic_thread_fence(std::memory_order_seq_cst);
            emulated = true;
        }
    }
    // Category 6: Atomic Load-Acquire / Store-Release unscaled offset (LDAPUR, STLUR)
    else if ((instr & 0x3fe0'0c00) == 0x1940'0000 ||  // LDAPUR*
             (instr & 0x3fe0'0c00) == 0x1900'0000) {  // STLUR*
        uint32_t size = (instr >> 30) & 3;
        uint32_t rn = (instr >> 5) & 0x1f;
        uint32_t rt = instr & 0x1f;
        size_t bytes = 1 << size;
        int32_t imm9 = (static_cast<int32_t>(instr) << 11) >> 23;
        target_addr = get_gpr(rn) + imm9;
        bool is_store = ((instr & 0x3fe0'0c00) == 0x1900'0000);

        if (target_addr >= 0x1000) {
            std::atomic_thread_fence(std::memory_order_seq_cst);
            if (is_store) {
                uint64_t val = (rt == 31 ? 0 : get_gpr(rt));
                SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &val, bytes);
            } else {
                uint64_t val = 0;
                SafeCopyFromGuest(&val, reinterpret_cast<const void*>(target_addr), bytes);
                if (rt != 31) set_gpr(rt, val);
            }
            std::atomic_thread_fence(std::memory_order_seq_cst);
            emulated = true;
        }
    }
    // Category 7: Compare and Swap (CAS, CASAL)
    else if ((instr & 0x3fe0'fc00) == 0x08e0'fc00) {
        uint32_t size = (instr >> 30) & 3;
        uint32_t rn = (instr >> 5) & 0x1f;
        uint32_t rs = (instr >> 16) & 0x1f;
        uint32_t rt = instr & 0x1f;
        size_t bytes = 1 << size;
        target_addr = get_gpr(rn);

        if (target_addr >= 0x1000) {
            std::lock_guard<std::mutex> lk(GetAtomicLockForAddress(target_addr));
            std::atomic_thread_fence(std::memory_order_seq_cst);
            uint64_t cur_val = 0;
            SafeCopyFromGuest(&cur_val, reinterpret_cast<const void*>(target_addr), bytes);
            uint64_t expected = (rs == 31 ? 0 : get_gpr(rs));
            uint64_t desired = (rt == 31 ? 0 : get_gpr(rt));
            uint64_t mask = (bytes == 8) ? ~0ULL : ((1ULL << (bytes * 8)) - 1);
            if ((cur_val & mask) == (expected & mask)) {
                SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &desired, bytes);
            }
            if (rs != 31) set_gpr(rs, cur_val & mask);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            emulated = true;
        }
    }
    // Category 8: Atomic Memory Operations (LDADD, SWP, LDSET, LDCLR, LDEOR)
    else if ((instr & 0x3b20'0c00) == 0x3820'0000) {
        uint32_t size = (instr >> 30) & 3;
        uint32_t rn = (instr >> 5) & 0x1f;
        uint32_t rs = (instr >> 16) & 0x1f;
        uint32_t rt = instr & 0x1f;
        uint32_t op = (instr >> 12) & 0xf;
        size_t bytes = 1 << size;
        target_addr = get_gpr(rn);

        if (target_addr >= 0x1000) {
            std::lock_guard<std::mutex> lk(GetAtomicLockForAddress(target_addr));
            std::atomic_thread_fence(std::memory_order_seq_cst);
            uint64_t cur_val = 0;
            SafeCopyFromGuest(&cur_val, reinterpret_cast<const void*>(target_addr), bytes);
            uint64_t op_val = (rs == 31 ? 0 : get_gpr(rs));
            uint64_t new_val = cur_val;
            uint64_t mask = (bytes == 8) ? ~0ULL : ((1ULL << (bytes * 8)) - 1);
            if (op == 0b0000) new_val = (cur_val + op_val) & mask;      // LDADD
            else if (op == 0b0001) new_val = (cur_val & ~op_val) & mask; // LDCLR
            else if (op == 0b0010) new_val = (cur_val ^ op_val) & mask;  // LDEOR
            else if (op == 0b0011) new_val = (cur_val | op_val) & mask;  // LDSET
            else if (op == 0b1000) new_val = op_val & mask;             // SWP
            SafeCopyToGuest(reinterpret_cast<void*>(target_addr), &new_val, bytes);
            if (rt != 31) set_gpr(rt, cur_val & mask);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            emulated = true;
        }
    }

    if (emulated) {
        arm_thread_state64_set_pc_fptr(ss, reinterpret_cast<void*>(pc + 4));
        return true;
    }
#endif
    return false;
}

} // namespace Loader::Fex
