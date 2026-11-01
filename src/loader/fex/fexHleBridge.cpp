// SPDX-License-Identifier: GPL-2.0-or-later
#include "loader/fex/fexHleBridge.h"
#include "loader/fex/fexGuestEngine.h"
#include "common/logging/log.h"
#include "common/virtualMemory.h"
#include "graphics/host_gpu/hostMemory.h"
#include "kernel/memory.h"
#include <Zydis/Zydis.h>

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <shared_mutex>
#include "kernel/pthread.h"

namespace Loader::Fex {

FexHleBridge::FexHleBridge()
    : m_call_counts(std::make_unique<std::atomic<uint64_t>[]>(16384)) {
    m_functions.reserve(16384);
    m_host_to_id.reserve(16384);
}

FexHleBridge& FexHleBridge::Instance() {
    static FexHleBridge bridge;
    return bridge;
}

void FexHleBridge::Reset() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    if (m_thunk_base != 0 && m_thunk_capacity != 0) {
        FexGuestEngine::Instance().InvalidateCodeRange(m_thunk_base, m_thunk_capacity);
    }
    m_thunk_base            = 0;
    m_thunk_capacity        = 0;
    m_custom_code_offset    = 0;
    m_fiber_trampoline_addr = 0;
    m_functions.clear();
    m_host_to_id.clear();
    if (m_call_counts) {
        for (size_t i = 0; i < 16384; ++i) {
            m_call_counts[i].store(0, std::memory_order_relaxed);
        }
    }
    m_recent_calls_total.store(0, std::memory_order_relaxed);
    std::memset(m_recent_calls, 0, sizeof(m_recent_calls));
    for (size_t i = 0; i < MAX_IN_FLIGHT; ++i) {
        m_in_flight[i].thunk_id.store(UINT64_MAX, std::memory_order_relaxed);
        m_in_flight[i].caller_rip.store(0, std::memory_order_relaxed);
        m_in_flight[i].arg0.store(0, std::memory_order_relaxed);
        m_in_flight[i].arg1.store(0, std::memory_order_relaxed);
        m_in_flight[i].start_ms.store(0, std::memory_order_relaxed);
        m_in_flight[i].tid.store(-1, std::memory_order_relaxed);
    }
}

void FexHleBridge::EnsureInitialized() {
    if (m_thunk_base != 0) {
        return;
    }

    if (!Libs::LibKernel::Memory::IsInitialized()) {
        return;
    }

    constexpr uint64_t THUNK_CAPACITY = 2 * 1024 * 1024; // 2 MB (first 1MB = 16,384 64-byte thunks, second 1MB = native guest code)
    // Place HLE thunks in SYSTEM_RESERVED space (0x800000000 / 32 GB) so game heaps at 0x300000000 cannot overwrite them
    constexpr uint64_t FEX_THUNK_SYSTEM_BASE = 0x800000000ull;
    m_thunk_base = Libs::LibKernel::Memory::AllocateRuntimeMemory(
        FEX_THUNK_SYSTEM_BASE, THUNK_CAPACITY, Common::VirtualMemory::Mode::ExecuteReadWrite, "fex_hle_thunks", true);
    if (m_thunk_base == 0) {
        m_thunk_base = Libs::LibKernel::Memory::AllocateRuntimeMemory(
            FEX_THUNK_SYSTEM_BASE, THUNK_CAPACITY, Common::VirtualMemory::Mode::ExecuteReadWrite, "fex_hle_thunks", false);
    }
    m_thunk_capacity = THUNK_CAPACITY;
    m_custom_code_offset = 1024 * 1024;

    if (m_thunk_base != 0) {
        // Slot 0 (first 32 bytes) is reserved for the HLT trampoline.
        // When a guest module entry / thread function executes RET, it returns here to cleanly halt.
        auto* hlt_code = reinterpret_cast<uint8_t*>(m_thunk_base);
        std::memset(hlt_code, 0xf4, THUNK_STRIDE); // 0xF4 = HLT
        Common::VirtualMemory::FlushInstructionCache(m_thunk_base, THUNK_STRIDE);

        // Emit shared guest x86-64 fiber context-switch trampoline at m_thunk_base + m_custom_code_offset
        m_fiber_trampoline_addr = m_thunk_base + m_custom_code_offset;
        m_custom_code_offset += 128;
        auto* fiber_stub = reinterpret_cast<uint8_t*>(m_fiber_trampoline_addr);
        std::memset(fiber_stub, 0xcc, 128);
        static const uint8_t kFiberTrampolineCode[] = {
            // Save caller's GuestFiberContext at [rsp - 80]:
            0x48, 0x89, 0x5c, 0x24, 0xb0, // mov [rsp - 80], rbx
            0x48, 0x89, 0x6c, 0x24, 0xb8, // mov [rsp - 72], rbp
            0x48, 0x89, 0x7c, 0x24, 0xc0, // mov [rsp - 64], rdi
            0x48, 0x89, 0x74, 0x24, 0xc8, // mov [rsp - 56], rsi
            0x4c, 0x89, 0x64, 0x24, 0xd0, // mov [rsp - 48], r12
            0x4c, 0x89, 0x6c, 0x24, 0xd8, // mov [rsp - 40], r13
            0x4c, 0x89, 0x74, 0x24, 0xe0, // mov [rsp - 32], r14
            0x4c, 0x89, 0x7c, 0x24, 0xe8, // mov [rsp - 24], r15
            0x4c, 0x8d, 0x5c, 0x24, 0x08, // lea r11, [rsp + 8]
            0x4c, 0x89, 0x5c, 0x24, 0xf0, // mov [rsp - 16], r11 (saved rsp)
            0x4c, 0x8b, 0x1c, 0x24,       // mov r11, [rsp]
            0x4c, 0x89, 0x5c, 0x24, 0xf8, // mov [rsp - 8], r11  (saved rip)
            // Invoke HLE syscall (eax already set by per-function thunk slot):
            0x49, 0x89, 0xca,             // mov r10, rcx
            0x0f, 0x05,                   // syscall
            // If rax <= 0 (error return without context switch), return:
            0x48, 0x85, 0xc0,             // test rax, rax
            0x7e, 0x31,                   // jle +49 (.no_switch)
            // Restore target GuestFiberRestoreFrame pointed to by rax:
            0x49, 0x89, 0xc2,             // mov r10, rax
            0x49, 0x8b, 0x1a,             // mov rbx, [r10 + 0]
            0x49, 0x8b, 0x6a, 0x08,       // mov rbp, [r10 + 8]
            0x49, 0x8b, 0x7a, 0x10,       // mov rdi, [r10 + 16]
            0x49, 0x8b, 0x72, 0x18,       // mov rsi, [r10 + 24]
            0x4d, 0x8b, 0x62, 0x20,       // mov r12, [r10 + 32]
            0x4d, 0x8b, 0x6a, 0x28,       // mov r13, [r10 + 40]
            0x4d, 0x8b, 0x72, 0x30,       // mov r14, [r10 + 48]
            0x4d, 0x8b, 0x7a, 0x38,       // mov r15, [r10 + 56]
            0x49, 0x8b, 0x62, 0x40,       // mov rsp, [r10 + 64]
            0x4d, 0x8b, 0x5a, 0x48,       // mov r11, [r10 + 72]
            0x49, 0x8b, 0x42, 0x50,       // mov rax, [r10 + 80]
            0x41, 0xff, 0xe3,             // jmp r11
            // .no_switch:
            0xc3,                         // ret
        };
        std::memcpy(fiber_stub, kFiberTrampolineCode, sizeof(kFiberTrampolineCode));
        Common::VirtualMemory::FlushInstructionCache(m_fiber_trampoline_addr, 128);

        LOGF("FexHleBridge: Allocated thunk buffer at 0x%016" PRIx64 " (size: 0x%" PRIx64 ")\n",
             m_thunk_base, m_thunk_capacity);
    } else {
        LOGF("FexHleBridge: Failed to allocate guest memory for thunks!\n");
    }
}

uint64_t FexHleBridge::GetHltAddress() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    EnsureInitialized();
    return m_thunk_base;
}

static ThunkSignature DetectSignatureByName(const std::string& name) {
    std::string leaf = name;
    const auto  pos  = name.rfind("::");
    if (pos != std::string::npos) {
        leaf = name.substr(pos + 2);
    }

    if (leaf == "FiberRun" || leaf == "FiberSwitch" || leaf == "FiberReturnToThread" ||
        leaf == "FiberExitTrampoline" || name == "a0LLrZWac0M" || name == "avfGJ94g36Q" ||
        name == "PFT2S-tJ7Uk" || name == "ZqhZFuzKT6U" || name == "B0ZX2hx9DMw") {
        return ThunkSignature::FiberSwitch;
    }
    if (name == "pztV4AF18iI" || leaf == "sincosf") {
        return ThunkSignature::Sincosf;
    }
    if (name == "jMB7EFyu30Y" || leaf == "sincos") {
        return ThunkSignature::Sincos;
    }
    if (name == "-VVn74ZyhEs" || leaf == "difftime" || leaf == "libc_difftime") {
        return ThunkSignature::Difftime;
    }
    // Single float arg -> float ret
    if (name == "Q4rRL34CEeE" || leaf == "sinf" ||
        name == "-P6FNMzk2Kc" || leaf == "cosf" ||
        leaf == "tanf" ||
        leaf == "sqrtf" ||
        leaf == "acosf" ||
        leaf == "asinf" ||
        leaf == "atanf" ||
        leaf == "ceilf" ||
        leaf == "floorf" ||
        leaf == "roundf" ||
        leaf == "truncf" ||
        leaf == "fabsf" ||
        leaf == "expf" ||
        leaf == "logf" ||
        leaf == "log10f") {
        return ThunkSignature::FloatArg0_FloatRet;
    }
    // Two float args -> float ret
    if (leaf == "powf" ||
        leaf == "atan2f" ||
        leaf == "fmodf" ||
        leaf == "hypotf" ||
        leaf == "copysignf") {
        return ThunkSignature::FloatArg01_FloatRet;
    }
    // Single double arg -> double ret
    if (name == "H8ya2H00jbI" || leaf == "sin" ||
        name == "2WE3BTYVwKM" || leaf == "cos" ||
        leaf == "tan" ||
        leaf == "sqrt" ||
        leaf == "acos" ||
        leaf == "asin" ||
        leaf == "atan" ||
        leaf == "ceil" ||
        leaf == "floor" ||
        leaf == "fabs" ||
        leaf == "exp" ||
        leaf == "log" ||
        leaf == "log10") {
        return ThunkSignature::DoubleArg0_DoubleRet;
    }
    // Two double args -> double ret
    if (leaf == "pow" ||
        leaf == "atan2" ||
        leaf == "fmod" ||
        leaf == "hypot") {
        return ThunkSignature::DoubleArg01_DoubleRet;
    }
    return ThunkSignature::IntOnly;
}

uint64_t FexHleBridge::RegisterFunction(uint64_t host_func_ptr, const std::string& name, ThunkSignature sig, HleDescriptor desc) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    EnsureInitialized();

    if (m_thunk_base == 0) {
        return host_func_ptr;
    }

    auto it = m_host_to_id.find(host_func_ptr);
    if (it != m_host_to_id.end()) {
        return m_thunk_base + (it->second + 1) * THUNK_STRIDE;
    }

    uint64_t id = m_functions.size();
    if ((id + 2) * THUNK_STRIDE > (1024 * 1024)) {
        LOGF("FexHleBridge: Thunk pool exhausted (id=%" PRIu64 ")!\n", id);
        return 0;
    }

    if (sig == ThunkSignature::Auto) {
        const auto detected = DetectSignatureByName(name);
        if (detected == ThunkSignature::FiberSwitch || desc.invoker == nullptr) {
            sig = detected;
        }
    }

    m_functions.push_back(std::make_unique<HleFunction>(HleFunction{id, host_func_ptr, name, sig, desc}));
    m_host_to_id[host_func_ptr] = id;

    // Slot 0 is the HLT trampoline, so function id gets slot (id + 1)
    uint64_t thunk_addr = m_thunk_base + (id + 1) * THUNK_STRIDE;
    auto* code = reinterpret_cast<uint8_t*>(thunk_addr);
    std::memset(code, 0xcc, THUNK_STRIDE); // 0xCC = INT3 padding

    size_t idx = 0;
    if (sig == ThunkSignature::FiberSwitch && m_fiber_trampoline_addr != 0) {
        // mov eax, id32 (5 bytes: 0xb8, <4 bytes uint32_t>)
        const auto id32 = static_cast<uint32_t>(id);
        code[idx++] = 0xb8;
        std::memcpy(&code[idx], &id32, sizeof(id32));
        idx += sizeof(id32);
        // jmp rel32 to m_fiber_trampoline_addr (5 bytes: 0xe9, <4 bytes int32_t>)
        code[idx++] = 0xe9;
        const auto rel32 = static_cast<int32_t>(
            static_cast<int64_t>(m_fiber_trampoline_addr) - static_cast<int64_t>(thunk_addr + idx + 4));
        std::memcpy(&code[idx], &rel32, sizeof(rel32));
        idx += sizeof(rel32);
    } else if (desc.invoker != nullptr) {
        const uint8_t xmm_count = (desc.num_xmm_args > 6) ? 6 : desc.num_xmm_args;
        for (uint8_t xi = 0; xi < xmm_count; ++xi) {
            // movq [rsp - 8*(xi+1)], xmm{xi} (7 bytes: 0x66, 0x48, 0x0f, 0x7e, 0x44|(xi<<3), 0x24, disp8)
            code[idx++] = 0x66;
            code[idx++] = 0x48;
            code[idx++] = 0x0f;
            code[idx++] = 0x7e;
            code[idx++] = static_cast<uint8_t>(0x44u | (xi << 3u));
            code[idx++] = 0x24;
            code[idx++] = static_cast<uint8_t>(-8 * static_cast<int>(xi + 1));
        }
        // mov r10, rcx (3 bytes: 0x49, 0x89, 0xca)
        code[idx++] = 0x49;
        code[idx++] = 0x89;
        code[idx++] = 0xca;
        // mov eax, id32 (5 bytes: 0xb8, <4 bytes uint32_t>)
        const auto id32 = static_cast<uint32_t>(id);
        code[idx++] = 0xb8;
        std::memcpy(&code[idx], &id32, sizeof(id32));
        idx += sizeof(id32);
        // syscall (2 bytes: 0x0f, 0x05)
        code[idx++] = 0x0f;
        code[idx++] = 0x05;
        if (desc.float_ret) {
            // movq xmm0, rax (5 bytes: 0x66, 0x48, 0x0f, 0x6e, 0xc0)
            code[idx++] = 0x66;
            code[idx++] = 0x48;
            code[idx++] = 0x0f;
            code[idx++] = 0x6e;
            code[idx++] = 0xc0;
        } else if (desc.has_rdx_ret) {
            // mov rdx, [rsp - 8] (5 bytes: 0x48, 0x8b, 0x54, 0x24, 0xf8)
            code[idx++] = 0x48;
            code[idx++] = 0x8b;
            code[idx++] = 0x54;
            code[idx++] = 0x24;
            code[idx++] = 0xf8;
        }
        // ret (1 byte: 0xc3)
        code[idx++] = 0xc3;
    } else switch (sig) {
        case ThunkSignature::FloatArg0_FloatRet:
        case ThunkSignature::DoubleArg0_DoubleRet: {
            // movq rdi, xmm0 (5 bytes: 0x66, 0x48, 0x0f, 0x7e, 0xc7)
            code[idx++] = 0x66; code[idx++] = 0x48; code[idx++] = 0x0f; code[idx++] = 0x7e; code[idx++] = 0xc7;
            // mov r10, rcx (3 bytes: 0x49, 0x89, 0xca)
            code[idx++] = 0x49; code[idx++] = 0x89; code[idx++] = 0xca;
            // mov rax, id (10 bytes: 0x48, 0xb8, <8 bytes uint64_t>)
            code[idx++] = 0x48; code[idx++] = 0xb8;
            std::memcpy(&code[idx], &id, sizeof(id)); idx += sizeof(id);
            // syscall (2 bytes: 0x0f, 0x05)
            code[idx++] = 0x0f; code[idx++] = 0x05;
            // movq xmm0, rax (5 bytes: 0x66, 0x48, 0x0f, 0x6e, 0xc0)
            code[idx++] = 0x66; code[idx++] = 0x48; code[idx++] = 0x0f; code[idx++] = 0x6e; code[idx++] = 0xc0;
            // ret (1 byte: 0xc3)
            code[idx++] = 0xc3;
            break;
        }
        case ThunkSignature::FloatArg01_FloatRet:
        case ThunkSignature::DoubleArg01_DoubleRet: {
            // movq rdi, xmm0 (5 bytes: 0x66, 0x48, 0x0f, 0x7e, 0xc7)
            code[idx++] = 0x66; code[idx++] = 0x48; code[idx++] = 0x0f; code[idx++] = 0x7e; code[idx++] = 0xc7;
            // movq rsi, xmm1 (5 bytes: 0x66, 0x48, 0x0f, 0x7e, 0xce)
            code[idx++] = 0x66; code[idx++] = 0x48; code[idx++] = 0x0f; code[idx++] = 0x7e; code[idx++] = 0xce;
            // mov r10, rcx (3 bytes: 0x49, 0x89, 0xca)
            code[idx++] = 0x49; code[idx++] = 0x89; code[idx++] = 0xca;
            // mov rax, id (10 bytes: 0x48, 0xb8, <8 bytes uint64_t>)
            code[idx++] = 0x48; code[idx++] = 0xb8;
            std::memcpy(&code[idx], &id, sizeof(id)); idx += sizeof(id);
            // syscall (2 bytes: 0x0f, 0x05)
            code[idx++] = 0x0f; code[idx++] = 0x05;
            // movq xmm0, rax (5 bytes: 0x66, 0x48, 0x0f, 0x6e, 0xc0)
            code[idx++] = 0x66; code[idx++] = 0x48; code[idx++] = 0x0f; code[idx++] = 0x6e; code[idx++] = 0xc0;
            // ret (1 byte: 0xc3)
            code[idx++] = 0xc3;
            break;
        }
        case ThunkSignature::FloatRet:
        case ThunkSignature::DoubleRet:
        case ThunkSignature::Difftime: {
            // mov r10, rcx (3 bytes: 0x49, 0x89, 0xca)
            code[idx++] = 0x49; code[idx++] = 0x89; code[idx++] = 0xca;
            // mov rax, id (10 bytes: 0x48, 0xb8, <8 bytes uint64_t>)
            code[idx++] = 0x48; code[idx++] = 0xb8;
            std::memcpy(&code[idx], &id, sizeof(id)); idx += sizeof(id);
            // syscall (2 bytes: 0x0f, 0x05)
            code[idx++] = 0x0f; code[idx++] = 0x05;
            // movq xmm0, rax (5 bytes: 0x66, 0x48, 0x0f, 0x6e, 0xc0)
            code[idx++] = 0x66; code[idx++] = 0x48; code[idx++] = 0x0f; code[idx++] = 0x6e; code[idx++] = 0xc0;
            // ret (1 byte: 0xc3)
            code[idx++] = 0xc3;
            break;
        }
        case ThunkSignature::FloatArg0:
        case ThunkSignature::DoubleArg0: {
            // movq rdi, xmm0 (5 bytes: 0x66, 0x48, 0x0f, 0x7e, 0xc7)
            code[idx++] = 0x66; code[idx++] = 0x48; code[idx++] = 0x0f; code[idx++] = 0x7e; code[idx++] = 0xc7;
            // mov r10, rcx (3 bytes: 0x49, 0x89, 0xca)
            code[idx++] = 0x49; code[idx++] = 0x89; code[idx++] = 0xca;
            // mov rax, id (10 bytes: 0x48, 0xb8, <8 bytes uint64_t>)
            code[idx++] = 0x48; code[idx++] = 0xb8;
            std::memcpy(&code[idx], &id, sizeof(id)); idx += sizeof(id);
            // syscall (2 bytes: 0x0f, 0x05)
            code[idx++] = 0x0f; code[idx++] = 0x05;
            // ret (1 byte: 0xc3)
            code[idx++] = 0xc3;
            break;
        }
        case ThunkSignature::Sincosf:
        case ThunkSignature::Sincos: {
            // movq rdx, xmm0 (5 bytes: 0x66, 0x48, 0x0f, 0x7e, 0xc2)
            code[idx++] = 0x66; code[idx++] = 0x48; code[idx++] = 0x0f; code[idx++] = 0x7e; code[idx++] = 0xc2;
            // mov r10, rcx (3 bytes: 0x49, 0x89, 0xca)
            code[idx++] = 0x49; code[idx++] = 0x89; code[idx++] = 0xca;
            // mov rax, id (10 bytes: 0x48, 0xb8, <8 bytes uint64_t>)
            code[idx++] = 0x48; code[idx++] = 0xb8;
            std::memcpy(&code[idx], &id, sizeof(id)); idx += sizeof(id);
            // syscall (2 bytes: 0x0f, 0x05)
            code[idx++] = 0x0f; code[idx++] = 0x05;
            // ret (1 byte: 0xc3)
            code[idx++] = 0xc3;
            break;
        }
        case ThunkSignature::Auto:
        case ThunkSignature::IntOnly:
        default: {
            // Default synthetic x86-64 syscall trampoline:
            // mov r10, rcx (3 bytes: 0x49, 0x89, 0xca)
            code[idx++] = 0x49;
            code[idx++] = 0x89;
            code[idx++] = 0xca;

            // mov rax, id (10 bytes: 0x48, 0xb8, <8 bytes uint64_t>)
            code[idx++] = 0x48;
            code[idx++] = 0xb8;
            std::memcpy(&code[idx], &id, sizeof(id));
            idx += sizeof(id);

            // syscall (2 bytes: 0x0f, 0x05)
            code[idx++] = 0x0f;
            code[idx++] = 0x05;

            // ret (1 byte: 0xc3)
            code[idx++] = 0xc3;
            break;
        }
    }

    Common::VirtualMemory::FlushInstructionCache(thunk_addr, THUNK_STRIDE);

    return thunk_addr;
}

const HleFunction* FexHleBridge::FindByThunk(uint64_t guest_thunk_addr) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    if (m_thunk_base == 0 || guest_thunk_addr < m_thunk_base + THUNK_STRIDE) {
        return nullptr;
    }
    uint64_t offset = guest_thunk_addr - (m_thunk_base + THUNK_STRIDE);
    uint64_t id = offset / THUNK_STRIDE;
    if (id < m_functions.size()) {
        return m_functions[id].get();
    }
    return nullptr;
}

const HleFunction* FexHleBridge::FindById(uint64_t id) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    if (id < m_functions.size()) {
        return m_functions[id].get();
    }
    return nullptr;
}

uint64_t FexHleBridge::GetCallCount(uint64_t id) const {
    if (m_call_counts && id < 16384) {
        return m_call_counts[id].load(std::memory_order_relaxed);
    }
    return 0;
}

uint64_t FexHleBridge::GetRecentCallCount() const {
    return m_recent_calls_total.load(std::memory_order_relaxed);
}

FexHleBridge::RecentCall FexHleBridge::GetRecentCall(uint64_t index) const {
    return m_recent_calls[index % RECENT_CALLS_CAPACITY];
}

static thread_local uint64_t               s_current_caller_rip{0};
static thread_local uint64_t               s_current_guest_rsp{0};
static thread_local GuestFiberRestoreFrame s_pending_fiber_restore{};
static thread_local bool                   s_has_pending_fiber_restore{false};

void FexHleBridge::RecordHleCall(uint64_t thunk_id, const uint64_t* args, uint64_t guest_rsp, uint64_t result) {
    const auto* fn_info = FindById(thunk_id);
    if (fn_info == nullptr) return;

    uint64_t caller_rip = (s_current_caller_rip != 0)
                              ? s_current_caller_rip
                              : ((guest_rsp != 0) ? *reinterpret_cast<const uint64_t*>(guest_rsp) : 0);

    // Record in recent calls ring buffer
    uint64_t r_idx = m_recent_calls_total.fetch_add(1, std::memory_order_relaxed);
    auto& rc = m_recent_calls[r_idx % RECENT_CALLS_CAPACITY];
    rc.thunk_id = thunk_id;
    rc.caller_rip = caller_rip;
    rc.args[0] = args[0];
    rc.args[1] = args[1];
    rc.args[2] = args[2];
    rc.ret = result;
    std::strncpy(rc.name, fn_info->name.c_str(), sizeof(rc.name) - 1);
    rc.name[sizeof(rc.name) - 1] = '\0';
}

void FexHleBridge::SetInFlight(size_t slot, int32_t tid, uint64_t thunk_id, uint64_t caller_rip, uint64_t a0, uint64_t a1) {
    if (slot >= MAX_IN_FLIGHT) return;
    auto now_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    m_in_flight[slot].start_ms.store(now_ms, std::memory_order_relaxed);
    m_in_flight[slot].tid.store(tid, std::memory_order_relaxed);
    m_in_flight[slot].caller_rip.store(caller_rip, std::memory_order_relaxed);
    m_in_flight[slot].arg0.store(a0, std::memory_order_relaxed);
    m_in_flight[slot].arg1.store(a1, std::memory_order_relaxed);
    m_in_flight[slot].thunk_id.store(thunk_id, std::memory_order_release);
}

void FexHleBridge::ClearInFlight(size_t slot) {
    if (slot >= MAX_IN_FLIGHT) return;
    m_in_flight[slot].thunk_id.store(UINT64_MAX, std::memory_order_release);
}

uint64_t FexHleBridge::GetCurrentCallerRip() const {
    return s_current_caller_rip;
}

bool FexHleBridge::ReadCurrentGuestFiberContext(GuestFiberContext* out_ctx) const {
    if (out_ctx == nullptr || s_current_guest_rsp < sizeof(GuestFiberContext)) {
        return false;
    }
    std::memcpy(out_ctx, reinterpret_cast<const void*>(s_current_guest_rsp - sizeof(GuestFiberContext)),
                sizeof(GuestFiberContext));
    return true;
}

void FexHleBridge::ScheduleGuestFiberRestore(const GuestFiberContext& target_ctx, uint64_t target_rax) {
    s_pending_fiber_restore.ctx = target_ctx;
    s_pending_fiber_restore.rax = target_rax;
    s_has_pending_fiber_restore = true;
}

uint64_t FexHleBridge::DispatchHle(uint64_t thunk_id, const uint64_t* gpr, uint64_t guest_rsp) {
    const auto* fn_info = FindById(thunk_id);
    if (fn_info == nullptr || fn_info->host_fn == 0) {
        return 0;
    }

    const uint64_t    host_fn = fn_info->host_fn;
    const auto&       fn_name = fn_info->name;

    uint64_t a0 = gpr[0];
    uint64_t a1 = gpr[1];
    uint64_t a2 = gpr[2];
    uint64_t a3 = gpr[3];
    uint64_t a4 = gpr[4];
    uint64_t a5 = gpr[5];

    uint64_t call_num = 0;
    if (m_call_counts && thunk_id < 16384) {
        call_num = m_call_counts[thunk_id].fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t caller_rip = (guest_rsp != 0) ? *reinterpret_cast<const uint64_t*>(guest_rsp) : 0;
    s_current_caller_rip        = caller_rip;
    s_current_guest_rsp         = guest_rsp;
    s_has_pending_fiber_restore = false;

    // Stack arguments: [guest_rsp + 8] .. [guest_rsp + 48] (skip return address at stack[0])
    uint64_t a6  = 0;
    uint64_t a7  = 0;
    uint64_t a8  = 0;
    uint64_t a9  = 0;
    uint64_t a10 = 0;
    uint64_t a11 = 0;
    if (guest_rsp != 0) {
        const auto* stack = reinterpret_cast<const uint64_t*>(guest_rsp);
        if (((guest_rsp + 48u) ^ guest_rsp) < 0x1000u) {
            a6  = stack[1]; // 7th arg
            a7  = stack[2]; // 8th arg
            a8  = stack[3]; // 9th arg
            a9  = stack[4]; // 10th arg
            a10 = stack[5]; // 11th arg
            a11 = stack[6]; // 12th arg
        } else {
            auto safe_read_stack = [&](size_t idx) -> uint64_t {
                const uint64_t addr = guest_rsp + idx * sizeof(uint64_t);
                if (((addr ^ guest_rsp) < 0x1000u) || Libs::Graphics::HostMemoryIsReadable(addr)) {
                    return stack[idx];
                }
                return 0;
            };
            a6  = safe_read_stack(1);
            a7  = safe_read_stack(2);
            a8  = safe_read_stack(3);
            a9  = safe_read_stack(4);
            a10 = safe_read_stack(5);
            a11 = safe_read_stack(6);
        }
    }

    static thread_local size_t s_slot = []() {
        static std::atomic<size_t> next{0};
        return next.fetch_add(1) % MAX_IN_FLIGHT;
    }();

    auto* self_pt = Libs::LibKernel::PthreadSelfOrNull();
    int32_t current_tid = self_pt ? Libs::LibKernel::PthreadGetthreadid() : -1;

    SetInFlight(s_slot, current_tid, thunk_id, caller_rip, a0, a1);

    uint64_t result = 0;
    if (fn_info->desc.invoker != nullptr) {
        const uint64_t int_args[12] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11};
        uint64_t       xmm_args[8]  = {0};
        const uint8_t  xmm_count    = (fn_info->desc.num_xmm_args > 6) ? 6 : fn_info->desc.num_xmm_args;
        if (xmm_count > 0 && guest_rsp != 0) {
            for (uint8_t xi = 0; xi < xmm_count; ++xi) {
                const uint64_t slot_addr = guest_rsp - 8ULL * static_cast<uint64_t>(xi + 1);
                if (((slot_addr ^ guest_rsp) < 0x1000u) || Libs::Graphics::HostMemoryIsReadable(slot_addr)) {
                    xmm_args[xi] = *reinterpret_cast<const uint64_t*>(slot_addr);
                }
            }
        }
        result = fn_info->desc.invoker(host_fn, int_args, xmm_args, guest_rsp);
    } else switch (fn_info->sig) {
        case ThunkSignature::FloatArg0_FloatRet: {
            float x = 0.0f;
            std::memcpy(&x, &a0, sizeof(float));
            using Fn = float (*)(float);
            float res = reinterpret_cast<Fn>(host_fn)(x);
            uint32_t bits = 0;
            std::memcpy(&bits, &res, sizeof(float));
            result = bits;
            break;
        }
        case ThunkSignature::DoubleArg0_DoubleRet: {
            double x = 0.0;
            std::memcpy(&x, &a0, sizeof(double));
            using Fn = double (*)(double);
            double res = reinterpret_cast<Fn>(host_fn)(x);
            uint64_t bits = 0;
            std::memcpy(&bits, &res, sizeof(double));
            result = bits;
            break;
        }
        case ThunkSignature::FloatArg01_FloatRet: {
            float x = 0.0f, y = 0.0f;
            std::memcpy(&x, &a0, sizeof(float));
            std::memcpy(&y, &a1, sizeof(float));
            using Fn = float (*)(float, float);
            float res = reinterpret_cast<Fn>(host_fn)(x, y);
            uint32_t bits = 0;
            std::memcpy(&bits, &res, sizeof(float));
            result = bits;
            break;
        }
        case ThunkSignature::DoubleArg01_DoubleRet: {
            double x = 0.0, y = 0.0;
            std::memcpy(&x, &a0, sizeof(double));
            std::memcpy(&y, &a1, sizeof(double));
            using Fn = double (*)(double, double);
            double res = reinterpret_cast<Fn>(host_fn)(x, y);
            uint64_t bits = 0;
            std::memcpy(&bits, &res, sizeof(double));
            result = bits;
            break;
        }
        case ThunkSignature::FloatRet: {
            using Fn = float (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
            float res = reinterpret_cast<Fn>(host_fn)(a0, a1, a2, a3, a4, a5);
            uint32_t bits = 0;
            std::memcpy(&bits, &res, sizeof(float));
            result = bits;
            break;
        }
        case ThunkSignature::DoubleRet: {
            using Fn = double (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
            double res = reinterpret_cast<Fn>(host_fn)(a0, a1, a2, a3, a4, a5);
            uint64_t bits = 0;
            std::memcpy(&bits, &res, sizeof(double));
            result = bits;
            break;
        }
        case ThunkSignature::FloatArg0: {
            float x = 0.0f;
            std::memcpy(&x, &a0, sizeof(float));
            using Fn = uint64_t (*)(float, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
            result = reinterpret_cast<Fn>(host_fn)(x, a1, a2, a3, a4, a5);
            break;
        }
        case ThunkSignature::DoubleArg0: {
            double x = 0.0;
            std::memcpy(&x, &a0, sizeof(double));
            using Fn = uint64_t (*)(double, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
            result = reinterpret_cast<Fn>(host_fn)(x, a1, a2, a3, a4, a5);
            break;
        }
        case ThunkSignature::Sincosf: {
            float x = 0.0f;
            std::memcpy(&x, &a2, sizeof(float));
            auto* sinp = reinterpret_cast<float*>(a0);
            auto* cosp = reinterpret_cast<float*>(a1);
            if (sinp != nullptr) *sinp = std::sin(x);
            if (cosp != nullptr) *cosp = std::cos(x);
            result = 0;
            break;
        }
        case ThunkSignature::Sincos: {
            double x = 0.0;
            std::memcpy(&x, &a2, sizeof(double));
            auto* sinp = reinterpret_cast<double*>(a0);
            auto* cosp = reinterpret_cast<double*>(a1);
            if (sinp != nullptr) *sinp = std::sin(x);
            if (cosp != nullptr) *cosp = std::cos(x);
            result = 0;
            break;
        }
        case ThunkSignature::Difftime: {
            auto t1 = static_cast<int64_t>(a0);
            auto t0 = static_cast<int64_t>(a1);
            double res = std::difftime(t1, t0);
            uint64_t bits = 0;
            std::memcpy(&bits, &res, sizeof(double));
            result = bits;
            break;
        }
        case ThunkSignature::Auto:
        case ThunkSignature::IntOnly:
        default: {
            using HostCallable = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
            auto callable = reinterpret_cast<HostCallable>(host_fn);
            result = callable(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
            break;
        }
    }

    if (fn_info->sig == ThunkSignature::FiberSwitch) {
        if (s_has_pending_fiber_restore) {
            s_has_pending_fiber_restore = false;
            result = reinterpret_cast<uint64_t>(&s_pending_fiber_restore);
        } else {
            result = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(result)));
        }
    }

    ClearInFlight(s_slot);

    if (FexGuestEngine::IsCurrentThreadFromStaleSession()) {
        return result;
    }

    // Record in recent calls ring buffer
    uint64_t r_idx = m_recent_calls_total.fetch_add(1, std::memory_order_relaxed);
    auto& rc = m_recent_calls[r_idx % RECENT_CALLS_CAPACITY];
    rc.thunk_id = thunk_id;
    rc.caller_rip = caller_rip;
    rc.args[0] = a0;
    rc.args[1] = a1;
    rc.args[2] = a2;
    rc.ret = result;
    std::strncpy(rc.name, fn_name.c_str(), sizeof(rc.name) - 1);
    rc.name[sizeof(rc.name) - 1] = '\0';

    return result;
}

} // namespace Loader::Fex
