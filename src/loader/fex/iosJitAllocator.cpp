// SPDX-License-Identifier: GPL-2.0-or-later
#include "loader/fex/iosJitAllocator.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__)

#include "common/logging/log.h"
#include "common/hostException.h"
#include "common/assert.h"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <csignal>
#include <dlfcn.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <mutex>
#include <setjmp.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/syscall.h>
#include <sys/ucontext.h>
#include <thread>
#include <unistd.h>
#include <utility>

extern "C" void __clear_cache(void* start, void* end) {
    sys_icache_invalidate(start, static_cast<size_t>(static_cast<char*>(end) - static_cast<char*>(start)));
}

namespace {

// Thread-local trap expectation guard for BreakpointJIT
static thread_local sigjmp_buf g_jit_trap_jmp_buf;
static thread_local volatile bool g_expecting_jit_trap = false;

static void JitTrapHandler(int sig, siginfo_t* info, void* ctx) {
    (void)sig; (void)info; (void)ctx;
    if (g_expecting_jit_trap) {
        siglongjmp(g_jit_trap_jmp_buf, 1);
    }
    struct sigaction prev;
    sigemptyset(&prev.sa_mask);
    prev.sa_handler = SIG_DFL;
    prev.sa_flags = 0;
    sigaction(sig, &prev, nullptr);
    raise(sig);
}

void InstallJitTrapHandler() {
    static std::once_flag once;
    std::call_once(once, []() {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_flags = SA_SIGINFO;
        sa.sa_sigaction = JitTrapHandler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTRAP, &sa, nullptr);
    });
}

#if defined(__aarch64__) || defined(__arm64__)
__attribute__((noinline, optnone, naked))
static void* CallKytyBrkGetJitMappingRaw(void* addr, size_t len) {
    __asm__ volatile(
        "mov x16, #1\n"
        "brk #0xf00d\n"
        "ret\n"
    );
}

__attribute__((noinline, optnone, naked))
static void CallKytyBrkJitDetachRaw() {
    __asm__ volatile(
        "mov x16, #0\n"
        "brk #0xf00d\n"
        "ret\n"
    );
}

__attribute__((noinline, naked))
static uint64_t CallKytyBrkProbeRaw() {
    __asm__ volatile(
        "brk #0x69\n"
        "ret\n"
    );
}

static void* CallKytyBrkGetJitMapping(void* addr, size_t len) {
    InstallJitTrapHandler();
    void* result = nullptr;
    g_expecting_jit_trap = true;
    if (sigsetjmp(g_jit_trap_jmp_buf, 1) == 0) {
        result = CallKytyBrkGetJitMappingRaw(addr, len);
    } else {
        result = nullptr;
    }
    g_expecting_jit_trap = false;
    return result;
}

static void CallKytyBrkJitDetach() {
    InstallJitTrapHandler();
    g_expecting_jit_trap = true;
    if (sigsetjmp(g_jit_trap_jmp_buf, 1) == 0) {
        CallKytyBrkJitDetachRaw();
    }
    g_expecting_jit_trap = false;
}

static uint64_t CallKytyBrkProbe() {
    InstallJitTrapHandler();
    uint64_t result = 0;
    g_expecting_jit_trap = true;
    if (sigsetjmp(g_jit_trap_jmp_buf, 1) == 0) {
        result = CallKytyBrkProbeRaw();
    } else {
        result = 0;
    }
    g_expecting_jit_trap = false;
    return result;
}
#endif

static inline void ToggleJitWriteProtect(int enabled) {
    typedef void (*jit_protect_fn_t)(int);
    static auto fn = reinterpret_cast<jit_protect_fn_t>(dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_np"));
    if (fn != nullptr) {
        fn(enabled);
    }
}

static thread_local sigjmp_buf g_verify_jmp_buf;
static thread_local volatile bool g_verifying = false;

static void VerifyTrapHandler(int sig, siginfo_t* info, void* ctx) {
    (void)sig; (void)info; (void)ctx;
    if (g_verifying) {
        siglongjmp(g_verify_jmp_buf, 1);
    }
}

// 2-instruction ARM64 execution verification (Kyty selftest: movz w0, #42; ret)
bool VerifyExecutableCode(void* rx_addr, void* rw_addr) {
    if (!rx_addr || !rw_addr) return false;
    static const uint32_t kTestCode[2] = { 0x52800540u, 0xD65F03C0u }; // mov w0, #42; ret

    ToggleJitWriteProtect(0);
    std::memcpy(rw_addr, kTestCode, sizeof(kTestCode));
    ToggleJitWriteProtect(1);
    sys_icache_invalidate(rx_addr, sizeof(kTestCode));

    uint32_t readback[2] = {0, 0};
    std::memcpy(readback, rx_addr, sizeof(readback));
    if (readback[0] != kTestCode[0] || readback[1] != kTestCode[1]) {
        std::printf("[Kyty iOS JIT] Verification failed: readback mismatch (got 0x%08x 0x%08x, expected 0x%08x 0x%08x)\n",
                    readback[0], readback[1], kTestCode[0], kTestCode[1]);
        std::fflush(stdout);
        return false;
    }

    struct sigaction sa_new{}, sa_old_bus{}, sa_old_segv{}, sa_old_ill{};
    sa_new.sa_sigaction = VerifyTrapHandler;
    sa_new.sa_flags = SA_SIGINFO;
    sigemptyset(&sa_new.sa_mask);

    sigaction(SIGBUS, &sa_new, &sa_old_bus);
    sigaction(SIGSEGV, &sa_new, &sa_old_segv);
    sigaction(SIGILL, &sa_new, &sa_old_ill);

    volatile bool ok = false;
    g_verifying = true;
    Common::HostException::SetAllocatorSelftestRunning(true);
    if (sigsetjmp(g_verify_jmp_buf, 1) == 0) {
        auto fn = reinterpret_cast<int (*)(void)>(rx_addr);
        int result = fn();
        ok = (result == 42);
        if (!ok) {
            std::printf("[Kyty iOS JIT] Verification failed: fn() returned %d, expected 42\n", result);
            std::fflush(stdout);
        }
    } else {
        ok = false;
        std::printf("[Kyty iOS JIT] Verification trapped: memory at %p is not executable by hardware (W^X protected)\n", rx_addr);
        std::fflush(stdout);
    }
    Common::HostException::SetAllocatorSelftestRunning(false);
    g_verifying = false;

    sigaction(SIGBUS, &sa_old_bus, nullptr);
    sigaction(SIGSEGV, &sa_old_segv, nullptr);
    sigaction(SIGILL, &sa_old_ill, nullptr);

    return ok;
}

bool IsTraced() noexcept {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    struct kinfo_proc info;
    size_t size = sizeof(info);
    info.kp_proc.p_flag = 0;
    if (sysctl(mib, 4, &info, &size, nullptr, 0) == 0) {
        return (info.kp_proc.p_flag & P_TRACED) != 0;
    }
    return false;
}

bool IsCsDebugged() noexcept {
    uint32_t flags = 0;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if (syscall(169 /* SYS_csops */, getpid(), 0 /* CS_OPS_STATUS */, &flags, sizeof(flags)) == 0) {
        if ((flags & 0x10000000 /* CS_DEBUGGED */) != 0) {
            return true;
        }
    }
#pragma clang diagnostic pop

    typedef int (*csops_t)(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);
    static csops_t p_csops = (csops_t)dlsym(RTLD_DEFAULT, "csops");
    if (p_csops && p_csops(getpid(), 0, &flags, sizeof(flags)) == 0) {
        if ((flags & 0x10000000) != 0) {
            return true;
        }
    }
    return false;
}

bool IsProcessDebugged() noexcept {
    return IsTraced() || IsCsDebugged();
}

static uint8_t* KytyRwAlias(uint8_t* rx, size_t size, bool fixed) {
    vm_address_t alias = fixed ? (vm_address_t)(rx + size) : 0;
    vm_prot_t cur = VM_PROT_NONE, max = VM_PROT_NONE;
    kern_return_t kr = vm_remap(mach_task_self(), &alias, static_cast<vm_size_t>(size), 0,
                                fixed ? (VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE) : VM_FLAGS_ANYWHERE,
                                mach_task_self(), reinterpret_cast<vm_address_t>(rx), FALSE, &cur, &max, VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS) {
        std::printf("[Kyty iOS JIT] KytyRwAlias: vm_remap failed: %d (%s)\n", kr, mach_error_string(kr));
        std::fflush(stdout);
        return nullptr;
    }
    kr = vm_protect(mach_task_self(), alias, static_cast<vm_size_t>(size), FALSE, VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS) {
        std::printf("[Kyty iOS JIT] KytyRwAlias: vm_protect failed: %d (%s)\n", kr, mach_error_string(kr));
        std::fflush(stdout);
        vm_deallocate(mach_task_self(), alias, static_cast<vm_size_t>(size));
        return nullptr;
    }
    return reinterpret_cast<uint8_t*>(alias);
}

static Loader::Fex::DualMappedRegion KytySelfDualMapping(size_t bytes) {
    Loader::Fex::DualMappedRegion region;
    size_t size = (bytes + 0x3fff) & ~(size_t)0x3fff;

    // PATH 1: Primary path for Xcode debugging (P_TRACED / get-task-allow) - MAP_JIT
    uint8_t* map_jit_ptr = static_cast<uint8_t*>(mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0));
    if (map_jit_ptr != MAP_FAILED) {
        // Attempt dual-mapping with vm_remap alias
        uint8_t* rw = KytyRwAlias(map_jit_ptr, size, false);
        if (rw != nullptr && VerifyExecutableCode(map_jit_ptr, rw)) {
            region.rx_addr = map_jit_ptr;
            region.rw_addr = rw;
            region.size = size;
            std::printf("[Kyty iOS JIT] MAP_JIT dual-mapping SUCCESS: RW=%p, RX=%p (%zu bytes)\n",
                        static_cast<void*>(rw), static_cast<void*>(map_jit_ptr), size);
            std::fflush(stdout);
            return region;
        }
        if (rw != nullptr) {
            vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(rw), static_cast<vm_size_t>(size));
        }

        // If dual-mapping alias failed, verify direct MAP_JIT single-mapping
        if (VerifyExecutableCode(map_jit_ptr, map_jit_ptr)) {
            region.rx_addr = map_jit_ptr;
            region.rw_addr = map_jit_ptr;
            region.size = size;
            std::printf("[Kyty iOS JIT] MAP_JIT single-mapping SUCCESS: RW=%p, RX=%p (%zu bytes)\n",
                        static_cast<void*>(map_jit_ptr), static_cast<void*>(map_jit_ptr), size);
            std::fflush(stdout);
            return region;
        }

        munmap(map_jit_ptr, size);
    } else {
        std::printf("[Kyty iOS JIT] mmap(MAP_JIT) failed (errno: %d)\n", errno);
        std::fflush(stdout);
    }

    return region;
}

struct JitMemoryPool {
    std::mutex mutex;
    uint8_t* rx_base{nullptr};
    uint8_t* rw_base{nullptr};
    size_t total_size{0};
    size_t allocated_offset{0};
    bool is_ready{false};
    std::string status_str{"JIT Inactive"};
};

static JitMemoryPool g_pool;

std::atomic<uint64_t> g_allocation_counter{0};

} // namespace

extern "C" {

bool KytyAcquireJitPool() noexcept {
    std::lock_guard<std::mutex> lock(g_pool.mutex);
    if (g_pool.is_ready) {
        return true;
    }

    constexpr size_t kJitPoolSize = 64 * 1024 * 1024; // 64 MB
    InstallJitTrapHandler();

    const bool is_traced = IsTraced();
    const bool is_debugged = IsProcessDebugged();

    std::printf("[Kyty iOS JIT] Attempting to acquire %zu MB JIT pool (P_TRACED=%d, CS_DEBUGGED=%d)...\n",
                kJitPoolSize / (1024 * 1024), is_traced ? 1 : 0, is_debugged ? 1 : 0);
    std::fflush(stdout);

    // Strategy 1: Xcode LLDB attached (P_TRACED=1) -> MAP_JIT permitted by kernel
    if (is_traced) {
        uint8_t* map_jit_ptr = static_cast<uint8_t*>(mmap(nullptr, kJitPoolSize,
            PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0));
        if (map_jit_ptr != MAP_FAILED) {
            uint8_t* rw = KytyRwAlias(map_jit_ptr, kJitPoolSize, false);
            if (rw != nullptr && VerifyExecutableCode(map_jit_ptr, rw)) {
                g_pool.rx_base = map_jit_ptr;
                g_pool.rw_base = rw;
                g_pool.total_size = kJitPoolSize;
                g_pool.allocated_offset = 0;
                g_pool.is_ready = true;
                g_pool.status_str = "Xcode LLDB Active (Dual-Mapped MAP_JIT, 64MB Ready)";
                std::printf("[Kyty iOS JIT] %s: RX=%p, RW=%p\n", g_pool.status_str.c_str(), map_jit_ptr, static_cast<void*>(rw));
                std::fflush(stdout);
                return true;
            }
            if (rw != nullptr) {
                vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(rw), kJitPoolSize);
            }
            if (VerifyExecutableCode(map_jit_ptr, map_jit_ptr)) {
                g_pool.rx_base = map_jit_ptr;
                g_pool.rw_base = map_jit_ptr;
                g_pool.total_size = kJitPoolSize;
                g_pool.allocated_offset = 0;
                g_pool.is_ready = true;
                g_pool.status_str = "Xcode LLDB Active (Single-Mapped MAP_JIT, 64MB Ready)";
                std::printf("[Kyty iOS JIT] %s at %p\n", g_pool.status_str.c_str(), map_jit_ptr);
                std::fflush(stdout);
                return true;
            }
            munmap(map_jit_ptr, kJitPoolSize);
        }
    }

    // Strategy 2: StikDebug BreakpointJIT (Universal Protocol via brk #0xf00d)
    if (is_debugged) {
#if defined(__aarch64__) || defined(__arm64__)
        void* rx = nullptr;
        // Attempt 1: Ask debug server to allocate RX mapping (passing nullptr address)
        for (int attempt = 1; attempt <= 3; attempt++) {
            rx = CallKytyBrkGetJitMapping(nullptr, kJitPoolSize);
            if (rx != nullptr) break;
            usleep(25 * 1000);
        }

        // Attempt 2: If server didn't allocate, preallocate RW region and ask server to prepare it
        void* user_alloc = nullptr;
        if (rx == nullptr) {
            user_alloc = mmap(nullptr, kJitPoolSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (user_alloc != MAP_FAILED) {
                rx = CallKytyBrkGetJitMapping(user_alloc, kJitPoolSize);
                if (rx == nullptr) {
                    munmap(user_alloc, kJitPoolSize);
                    user_alloc = nullptr;
                }
            } else {
                user_alloc = nullptr;
            }
        }

        if (rx != nullptr) {
            vm_address_t rw = 0;
            vm_prot_t cur_prot = VM_PROT_NONE, max_prot = VM_PROT_NONE;
            kern_return_t kr = vm_remap(mach_task_self(), &rw, kJitPoolSize, 0, VM_FLAGS_ANYWHERE,
                                        mach_task_self(), reinterpret_cast<vm_address_t>(rx),
                                        FALSE, &cur_prot, &max_prot, VM_INHERIT_NONE);
            if (kr == KERN_SUCCESS) {
                kr = vm_protect(mach_task_self(), rw, kJitPoolSize, FALSE, VM_PROT_READ | VM_PROT_WRITE);
            }
            if (kr == KERN_SUCCESS && VerifyExecutableCode(rx, reinterpret_cast<void*>(rw))) {
                // Detach debugger as required by universal protocol after initial regions prepared
                CallKytyBrkJitDetach();

                g_pool.rx_base = static_cast<uint8_t*>(rx);
                g_pool.rw_base = reinterpret_cast<uint8_t*>(rw);
                g_pool.total_size = kJitPoolSize;
                g_pool.allocated_offset = 0;
                g_pool.is_ready = true;
                g_pool.status_str = "StikDebug Universal Protocol (Dual-Mapped, 64MB Ready)";
                std::printf("[Kyty iOS JIT] %s: RX=%p, RW=%p\n", g_pool.status_str.c_str(), rx, reinterpret_cast<void*>(rw));
                std::fflush(stdout);
                return true;
            }
            if (rw != 0) {
                vm_deallocate(mach_task_self(), rw, kJitPoolSize);
            }
            if (user_alloc != nullptr) {
                munmap(user_alloc, kJitPoolSize);
            } else {
                vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(rx), kJitPoolSize);
            }
        }
#endif
    }

    // Strategy 3: Direct dual-mapping fallback (TrollStore / Jailbreak / Entitled)
    auto region = KytySelfDualMapping(kJitPoolSize);
    if (region.IsValid()) {
        g_pool.rx_base = region.rx_addr;
        g_pool.rw_base = region.rw_addr;
        g_pool.total_size = region.size;
        g_pool.allocated_offset = 0;
        g_pool.is_ready = true;
        g_pool.status_str = "Direct Dual-Mapped (64MB Ready)";
        region.rx_addr = nullptr;
        region.rw_addr = nullptr;
        std::printf("[Kyty iOS JIT] %s: RX=%p, RW=%p\n", g_pool.status_str.c_str(), g_pool.rx_base, g_pool.rw_base);
        std::fflush(stdout);
        return true;
    }

    g_pool.is_ready = false;
    g_pool.status_str = "JIT Inactive (Executable permissions unavailable)";
    std::printf("[Kyty iOS JIT] KytyAcquireJitPool: All acquisition strategies failed for 64MB pool\n");
    std::fflush(stdout);
    return false;
}

bool KytyIsJitPoolReady() noexcept {
    return g_pool.is_ready;
}

const char* KytyGetJitPoolStatus() noexcept {
    return g_pool.status_str.c_str();
}

void KytyResetJitPoolForNewSession() noexcept {
    std::lock_guard<std::mutex> lock(g_pool.mutex);
    constexpr size_t kDispatcherReservedBytes = 16 * 1024 * 1024;
    if (g_pool.is_ready && g_pool.allocated_offset > kDispatcherReservedBytes) {
        g_pool.allocated_offset = kDispatcherReservedBytes;
        std::printf("[Kyty iOS JIT] Reset JIT pool offset to %zu MB for new session\n",
                    kDispatcherReservedBytes / (1024 * 1024));
        std::fflush(stdout);
    }
}

} // extern "C"

namespace Loader::Fex {

bool KytyAcquireJitPool() noexcept {
    return ::KytyAcquireJitPool();
}

bool KytyIsJitPoolReady() noexcept {
    return ::KytyIsJitPoolReady();
}

const char* KytyGetJitPoolStatus() noexcept {
    return ::KytyGetJitPoolStatus();
}

void KytyResetJitPoolForNewSession() noexcept {
    ::KytyResetJitPoolForNewSession();
}

DualMappedRegion DualMappedRegion::Allocate(size_t bytes) noexcept {
    DualMappedRegion region;
    InstallJitTrapHandler();
    const uint64_t request_number = g_allocation_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool is_debugged = IsProcessDebugged();
    const bool is_traced = IsTraced();

    std::printf("[Kyty iOS JIT] Allocation #%" PRIu64 " requested (%zu bytes). Debugger: %s (PID: %d, P_TRACED=%d)\n",
                request_number, bytes, is_debugged ? "ACTIVE" : "INACTIVE", getpid(), is_traced ? 1 : 0);
    std::fflush(stdout);

    if (!is_debugged) {
        std::printf("[Kyty iOS JIT] ERROR: Process is not debugged. Debugger / StikDebug must be attached before allocating JIT memory.\n");
        std::fflush(stdout);
        return region;
    }

    // PATH 1: Primary path, StikDebug BreakpointJIT protocol (brk #0xf00d)
#if defined(__aarch64__) || defined(__arm64__)
    void* rx = nullptr;
    for (int attempt = 1; attempt <= 3; attempt++) {
        g_expecting_jit_trap = true;
        rx = CallKytyBrkGetJitMapping(nullptr, bytes);
        g_expecting_jit_trap = false;

        if (rx != nullptr) break;
        usleep(25 * 1000);
    }

    if (rx != nullptr) {
        // Remap writable alias
        vm_address_t rw = 0;
        vm_prot_t cur_prot = VM_PROT_NONE, max_prot = VM_PROT_NONE;
        kern_return_t kr = vm_remap(mach_task_self(), &rw, static_cast<vm_size_t>(bytes), 0, VM_FLAGS_ANYWHERE,
                                    mach_task_self(), reinterpret_cast<vm_address_t>(rx), FALSE, &cur_prot, &max_prot, VM_INHERIT_NONE);

        if (kr == KERN_SUCCESS &&
            vm_protect(mach_task_self(), rw, static_cast<vm_size_t>(bytes), FALSE, VM_PROT_READ | VM_PROT_WRITE) == KERN_SUCCESS &&
            VerifyExecutableCode(rx, reinterpret_cast<void*>(rw))) {
            region.rx_addr = static_cast<uint8_t*>(rx);
            region.rw_addr = reinterpret_cast<uint8_t*>(rw);
            region.size = bytes;
            std::printf("[Kyty iOS JIT] StikDebug dual-mapping VERIFIED (42 test passed): RW=%p, RX=%p (%zu bytes)\n",
                        static_cast<void*>(region.rw_addr), static_cast<void*>(region.rx_addr), bytes);
            std::fflush(stdout);
            return region;
        }
        if (rw != 0) {
            vm_deallocate(mach_task_self(), rw, static_cast<vm_size_t>(bytes));
        }
    }
#endif

    // PATH 2: Direct dual-mapping fallback (StikDebug already detached or persistent CS_DEBUGGED)
    region = KytySelfDualMapping(bytes);
    if (!region.IsValid()) {
        std::printf("[Kyty iOS JIT] FAILED: All JIT allocation strategies failed for %zu bytes.\n"
                    "[Kyty iOS JIT] NOTE: To run with JIT on iOS:\n"
                    "[Kyty iOS JIT]   Option 1: Launch via Xcode debugger (P_TRACED=1 enables MAP_JIT).\n"
                    "[Kyty iOS JIT]   Option 2: Use StikDebug with universal.js actively attached.\n",
                    bytes);
        std::fflush(stdout);
    }
    return region;
}

DualMappedRegion::~DualMappedRegion() {
    if (rw_addr != nullptr && rw_addr != rx_addr) {
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(rw_addr), static_cast<vm_size_t>(size));
        rw_addr = nullptr;
    }
    if (rx_addr != nullptr) {
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(rx_addr), static_cast<vm_size_t>(size));
        rx_addr = nullptr;
    }
    size = 0;
}

DualMappedRegion::DualMappedRegion(DualMappedRegion&& other) noexcept
    : rw_addr(std::exchange(other.rw_addr, nullptr))
    , rx_addr(std::exchange(other.rx_addr, nullptr))
    , size(std::exchange(other.size, 0)) {}

DualMappedRegion& DualMappedRegion::operator=(DualMappedRegion&& other) noexcept {
    if (this != &other) {
        if (rw_addr != nullptr && rw_addr != rx_addr) {
            vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(rw_addr), static_cast<vm_size_t>(size));
        }
        if (rx_addr != nullptr) {
            vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(rx_addr), static_cast<vm_size_t>(size));
        }
        rw_addr = std::exchange(other.rw_addr, nullptr);
        rx_addr = std::exchange(other.rx_addr, nullptr);
        size    = std::exchange(other.size, 0);
    }
    return *this;
}

bool IsIosJitAvailable() noexcept {
    return KytyIsJitPoolReady() || IsProcessDebugged();
}

} // namespace Loader::Fex

extern "C" __attribute__((visibility("default")))
bool KytyDualMappedAlloc(size_t size, void** out_rw, void** out_rx) noexcept {
    if (!out_rw || !out_rx) return false;

    // Ensure preallocated pool is initialized
    if (!KytyIsJitPoolReady()) {
        KytyAcquireJitPool();
    }

    if (KytyIsJitPoolReady()) {
        std::lock_guard<std::mutex> lock(g_pool.mutex);
        const size_t aligned_size = (size + 0x3fff) & ~size_t(0x3fff);
        if (g_pool.allocated_offset + aligned_size <= g_pool.total_size) {
            *out_rw = g_pool.rw_base + g_pool.allocated_offset;
            *out_rx = g_pool.rx_base + g_pool.allocated_offset;
            g_pool.allocated_offset += aligned_size;
            std::printf("[Kyty iOS JIT] KytyDualMappedAlloc SUCCESS: RW=%p, RX=%p (%zu bytes, %zu/%zu MB pool used)\n",
                        *out_rw, *out_rx, size,
                        g_pool.allocated_offset / (1024 * 1024),
                        g_pool.total_size / (1024 * 1024));
            std::fflush(stdout);
            return true;
        }
        std::printf("[Kyty iOS JIT] WARNING: Preallocated JIT pool full (%zu/%zu MB), trying fallback\n",
                    g_pool.allocated_offset / (1024 * 1024), g_pool.total_size / (1024 * 1024));
        std::fflush(stdout);
    }

    // Fallback: Individual region allocation (e.g. Xcode P_TRACED=1 direct MAP_JIT)
    auto region = Loader::Fex::DualMappedRegion::Allocate(size);
    if (region.IsValid()) {
        *out_rw = region.rw_addr;
        *out_rx = region.rx_addr;
        region.rw_addr = nullptr;
        region.rx_addr = nullptr;
        region.size = 0;
        std::printf("[Kyty iOS JIT] KytyDualMappedAlloc (fallback) SUCCESS: RW=%p, RX=%p (%zu bytes)\n",
                    *out_rw, *out_rx, size);
        std::fflush(stdout);
        return true;
    }

    std::printf("[Kyty iOS JIT] KytyDualMappedAlloc FAILED for %zu bytes\n", size);
    std::fflush(stdout);
    *out_rw = nullptr;
    *out_rx = nullptr;
    Common::InvokeFatalHandler(SIGBUS,
        "JIT Memory Allocation Failed: Process lacks executable memory permissions on iOS.\n\n"
        "To run games on iOS:\n"
        "- On-device: Open StikDebug -> Long-press KytyPS5 -> 'Attach Script' -> choose 'universal.js', then tap 'Enable JIT'.");
    mach_port_t self = mach_thread_self();
    thread_suspend(self);
    mach_port_deallocate(mach_task_self(), self);
    while (true) {
        sleep(60);
    }
    return false;
}

extern "C" __attribute__((visibility("default")))
void KytyDualMappedFree(void* rw, void* rx, size_t size) noexcept {
    if (rw >= g_pool.rw_base && rw < g_pool.rw_base + g_pool.total_size) {
        // Belongs to the preallocated pool - keep mapped for subsequent execution
        return;
    }
    if (rw != nullptr && rw != rx) {
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(rw), static_cast<vm_size_t>(size));
    }
    if (rx != nullptr) {
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(rx), static_cast<vm_size_t>(size));
    }
    std::printf("[Kyty iOS JIT] KytyDualMappedFree: RW=%p, RX=%p (%zu bytes)\n", rw, rx, size);
    std::fflush(stdout);
}

extern "C" __attribute__((visibility("default"))) void* BreakGetJITMapping(void* addr, size_t len) noexcept {
    if (addr != nullptr) {
        return nullptr;
    }
    size_t size = (len + 0x3fff) & ~(size_t)0x3fff;

    if (!KytyIsJitPoolReady()) {
        KytyAcquireJitPool();
    }
    if (KytyIsJitPoolReady()) {
        std::lock_guard<std::mutex> lock(g_pool.mutex);
        if (g_pool.allocated_offset + size <= g_pool.total_size) {
            void* rx = g_pool.rx_base + g_pool.allocated_offset;
            g_pool.allocated_offset += size;
            std::printf("[Kyty iOS JIT] BreakGetJITMapping (from pool): %p (%zu bytes)\n", rx, size);
            std::fflush(stdout);
            return rx;
        }
    }

    if (!IsTraced()) {
#if defined(__aarch64__) || defined(__arm64__)
        InstallJitTrapHandler();
        g_expecting_jit_trap = true;
        uint64_t probe = CallKytyBrkProbe();
        g_expecting_jit_trap = false;
        if (probe != 0) {
            void* rx = nullptr;
            for (int attempt = 1; attempt <= 3; attempt++) {
                g_expecting_jit_trap = true;
                rx = CallKytyBrkGetJitMapping(nullptr, size);
                g_expecting_jit_trap = false;
                if (rx != nullptr) break;
                usleep(25 * 1000);
            }
            if (rx != nullptr) {
                std::printf("[Kyty iOS JIT] BreakGetJITMapping via StikDebug: %p (%zu bytes)\n", rx, size);
                std::fflush(stdout);
                return rx;
            }
        }
#endif
    }

    void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (ptr == MAP_FAILED) {
        ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    }
    if (ptr == MAP_FAILED) {
        ptr = ::mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    }
    if (ptr == MAP_FAILED) {
        std::printf("[Kyty iOS JIT] BreakGetJITMapping failed for %zu bytes (errno: %d)\n", len, errno);
        std::fflush(stdout);
        return nullptr;
    }
    std::printf("[Kyty iOS JIT] BreakGetJITMapping direct: %p (%zu bytes)\n", ptr, size);
    std::fflush(stdout);
    return ptr;
}

extern "C" __attribute__((visibility("default"))) void BreakJITDetach() noexcept {
    if (!IsTraced()) {
#if defined(__aarch64__) || defined(__arm64__)
        CallKytyBrkJitDetach();
#endif
    }
}


#endif // defined(__APPLE__)

