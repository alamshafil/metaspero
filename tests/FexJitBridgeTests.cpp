// SPDX-License-Identifier: GPL-2.0-or-later
#include "loader/fex/fexGuestEngine.h"
#include "loader/fex/fexHleBridge.h"
#include "kernel/memory.h"
#include "common/threads.h"
#include "common/virtualMemory.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void Check(bool condition, const char* msg) {
    if (!condition) {
        std::fprintf(stderr, "\n[FAIL] %s\n", msg);
        std::abort();
    }
}

// Helper to allocate executable guest memory for machine code
uint64_t AllocateGuestCodePage() {
    uint64_t addr = Libs::LibKernel::Memory::AllocateRuntimeMemory(
        0, 0x4000, Common::VirtualMemory::Mode::ExecuteReadWrite, "test_fex_code");
    Check(addr != 0, "Allocate guest code memory");
    return addr;
}

// Helper to allocate guest stack
uint64_t AllocateGuestStack() {
    uint64_t addr = Libs::LibKernel::Memory::AllocateRuntimeMemory(
        0, 0x10000, Common::VirtualMemory::Mode::ReadWrite, "test_fex_stack");
    Check(addr != 0, "Allocate guest stack memory");
    return addr;
}

uint64_t RunGuestSnippet(uint64_t entry_rip, uint64_t stack_base, uint64_t a0 = 0, uint64_t a1 = 0, uint64_t a2 = 0) {
    uint64_t stack_top = (stack_base + 0x10000 - 128) & ~0xFull;
    stack_top -= 8;
    *reinterpret_cast<uint64_t*>(stack_top) = Loader::Fex::FexHleBridge::Instance().GetHltAddress();

    auto* thread = Loader::Fex::FexGuestEngine::Instance().CreateThread(
        entry_rip, stack_top, a0, a1, a2, 0);
    Check(thread != nullptr, "CreateThread");
    uint64_t ret = Loader::Fex::FexGuestEngine::Instance().Run(thread, false);
    Loader::Fex::FexGuestEngine::Instance().DestroyThread(thread);
    return ret;
}

// Test 1: ABI Integer Args (6 GPRs + 6 Stack Args)
static uint64_t s_sum12_received = 0;
static uint64_t HostSum12(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7,
                          uint64_t a8, uint64_t a9, uint64_t a10, uint64_t a11) {
    s_sum12_received = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11;
    return s_sum12_received;
}

void TestAbiIntegerArgs() {
    std::printf("[TEST] Running ABI_IntegerArgs_6Gpr_6Stack...\n");

    uint64_t thunk_addr = Loader::Fex::FexHleBridge::Instance().RegisterFunction(
        reinterpret_cast<uint64_t>(HostSum12), "HostSum12", Loader::Fex::ThunkSignature::IntOnly);
    Check(thunk_addr != 0, "Register HostSum12 thunk");

    uint64_t code_addr = AllocateGuestCodePage();
    uint64_t stack_addr = AllocateGuestStack();

    // Assemble x86-64 caller:
    // sub rsp, 56            (48 bytes for 6 args + 8 bytes to keep 16-byte stack alignment)
    // mov qword ptr [rsp+40], 12
    // mov qword ptr [rsp+32], 11
    // mov qword ptr [rsp+24], 10
    // mov qword ptr [rsp+16], 9
    // mov qword ptr [rsp+8], 8
    // mov qword ptr [rsp], 7
    // mov rdi, 1
    // mov rsi, 2
    // mov rdx, 3
    // mov rcx, 4
    // mov r8, 5
    // mov r9, 6
    // mov rax, <thunk_addr>
    // call rax
    // add rsp, 56
    // ret
    uint8_t* p = reinterpret_cast<uint8_t*>(code_addr);
    size_t idx = 0;

    // sub rsp, 56 (48 83 ec 38)
    p[idx++] = 0x48; p[idx++] = 0x83; p[idx++] = 0xec; p[idx++] = 0x38;

    auto emit_store_stack = [&](uint8_t offset, uint32_t val) {
        // mov qword ptr [rsp+offset], val (48 c7 44 24 <offset> <4-byte imm>)
        p[idx++] = 0x48; p[idx++] = 0xc7; p[idx++] = 0x44; p[idx++] = 0x24; p[idx++] = offset;
        std::memcpy(&p[idx], &val, sizeof(val)); idx += sizeof(val);
    };

    emit_store_stack(40, 12);
    emit_store_stack(32, 11);
    emit_store_stack(24, 10);
    emit_store_stack(16, 9);
    emit_store_stack(8, 8);
    emit_store_stack(0, 7);

    // mov edi, 1 (bf 01 00 00 00)
    p[idx++] = 0xbf; p[idx++] = 0x01; p[idx++] = 0x00; p[idx++] = 0x00; p[idx++] = 0x00;
    // mov esi, 2 (be 02 00 00 00)
    p[idx++] = 0xbe; p[idx++] = 0x02; p[idx++] = 0x00; p[idx++] = 0x00; p[idx++] = 0x00;
    // mov edx, 3 (ba 03 00 00 00)
    p[idx++] = 0xba; p[idx++] = 0x03; p[idx++] = 0x00; p[idx++] = 0x00; p[idx++] = 0x00;
    // mov ecx, 4 (b9 04 00 00 00)
    p[idx++] = 0xb9; p[idx++] = 0x04; p[idx++] = 0x00; p[idx++] = 0x00; p[idx++] = 0x00;
    // mov r8d, 5 (41 b8 05 00 00 00)
    p[idx++] = 0x41; p[idx++] = 0xb8; p[idx++] = 0x05; p[idx++] = 0x00; p[idx++] = 0x00; p[idx++] = 0x00;
    // mov r9d, 6 (41 b9 06 00 00 00)
    p[idx++] = 0x41; p[idx++] = 0xb9; p[idx++] = 0x06; p[idx++] = 0x00; p[idx++] = 0x00; p[idx++] = 0x00;

    // mov rax, thunk_addr (48 b8 <8 bytes>)
    p[idx++] = 0x48; p[idx++] = 0xb8;
    std::memcpy(&p[idx], &thunk_addr, sizeof(thunk_addr)); idx += sizeof(thunk_addr);

    // call rax (ff d0)
    p[idx++] = 0xff; p[idx++] = 0xd0;

    // add rsp, 56 (48 83 c4 38)
    p[idx++] = 0x48; p[idx++] = 0x83; p[idx++] = 0xc4; p[idx++] = 0x38;

    // ret (c3)
    p[idx++] = 0xc3;

    Common::VirtualMemory::FlushInstructionCache(code_addr, idx);

    s_sum12_received = 0;
    uint64_t ret = RunGuestSnippet(code_addr, stack_addr);
    Check(s_sum12_received == 78, "HostSum12 received sum must be 78");
    Check(ret == 78, "Guest received RAX return must be 78");
    std::printf("  -> [PASS] ABI_IntegerArgs: 12 args matched correctly (sum=%llu)\n",
                static_cast<unsigned long long>(ret));
}

// Test 2: ABI Float / Double Args and Return in XMM0
static float HostFloatAdd(float a, float b) {
    return a + b;
}

static double HostDoubleMul(double a, double b) {
    return a * b;
}

void TestAbiFloatArgsAndReturn() {
    std::printf("[TEST] Running ABI_FloatArgsAndReturn_XMM...\n");

    uint64_t float_add_thunk = Loader::Fex::FexHleBridge::Instance().RegisterFunction(
        reinterpret_cast<uint64_t>(HostFloatAdd), "HostFloatAdd", Loader::Fex::ThunkSignature::FloatArg01_FloatRet);
    Check(float_add_thunk != 0, "Register HostFloatAdd thunk");

    uint64_t double_mul_thunk = Loader::Fex::FexHleBridge::Instance().RegisterFunction(
        reinterpret_cast<uint64_t>(HostDoubleMul), "HostDoubleMul", Loader::Fex::ThunkSignature::DoubleArg01_DoubleRet);
    Check(double_mul_thunk != 0, "Register HostDoubleMul thunk");

    uint64_t code_addr = AllocateGuestCodePage();
    uint64_t stack_addr = AllocateGuestStack();

    // Test 2a: FloatAdd(3.25f, 1.5f) -> 4.75f
    // mov eax, 0x40500000 (3.25f)
    // movd xmm0, eax
    // mov eax, 0x3fc00000 (1.5f)
    // movd xmm1, eax
    // mov rax, <float_add_thunk>
    // call rax
    // movd eax, xmm0
    // ret
    uint8_t* p = reinterpret_cast<uint8_t*>(code_addr);
    size_t idx = 0;

    uint32_t f1_bits = std::bit_cast<uint32_t>(3.25f);
    uint32_t f2_bits = std::bit_cast<uint32_t>(1.5f);

    p[idx++] = 0xb8; std::memcpy(&p[idx], &f1_bits, 4); idx += 4; // mov eax, f1
    p[idx++] = 0x66; p[idx++] = 0x0f; p[idx++] = 0x6e; p[idx++] = 0xc0; // movd xmm0, eax

    p[idx++] = 0xb8; std::memcpy(&p[idx], &f2_bits, 4); idx += 4; // mov eax, f2
    p[idx++] = 0x66; p[idx++] = 0x0f; p[idx++] = 0x6e; p[idx++] = 0xc8; // movd xmm1, eax

    p[idx++] = 0x48; p[idx++] = 0xb8; std::memcpy(&p[idx], &float_add_thunk, 8); idx += 8; // mov rax, thunk
    p[idx++] = 0xff; p[idx++] = 0xd0; // call rax
    p[idx++] = 0x66; p[idx++] = 0x0f; p[idx++] = 0x7e; p[idx++] = 0xc0; // movd eax, xmm0
    p[idx++] = 0xc3; // ret

    Common::VirtualMemory::FlushInstructionCache(code_addr, idx);
    int ret_bits = RunGuestSnippet(code_addr, stack_addr);
    float ret_float = std::bit_cast<float>(static_cast<uint32_t>(ret_bits));
    Check(std::fabs(ret_float - 4.75f) < 0.0001f, "HostFloatAdd result must be 4.75f");
    std::printf("  -> [PASS] FloatArg01_FloatRet: 3.25f + 1.5f = %f\n", ret_float);

    // Test 2b: DoubleMul(2.5, 4.0) -> 10.0
    uint64_t code_addr_b = AllocateGuestCodePage();
    uint64_t stack_addr_b = AllocateGuestStack();
    uint8_t* p_b = reinterpret_cast<uint8_t*>(code_addr_b);
    idx = 0;
    uint64_t d1_bits = std::bit_cast<uint64_t>(2.5);
    uint64_t d2_bits = std::bit_cast<uint64_t>(4.0);

    p_b[idx++] = 0x48; p_b[idx++] = 0xb8; std::memcpy(&p_b[idx], &d1_bits, 8); idx += 8; // mov rax, d1
    p_b[idx++] = 0x66; p_b[idx++] = 0x48; p_b[idx++] = 0x0f; p_b[idx++] = 0x6e; p_b[idx++] = 0xc0; // movq xmm0, rax

    p_b[idx++] = 0x48; p_b[idx++] = 0xb8; std::memcpy(&p_b[idx], &d2_bits, 8); idx += 8; // mov rax, d2
    p_b[idx++] = 0x66; p_b[idx++] = 0x48; p_b[idx++] = 0x0f; p_b[idx++] = 0x6e; p_b[idx++] = 0xc8; // movq xmm1, rax

    p_b[idx++] = 0x48; p_b[idx++] = 0xb8; std::memcpy(&p_b[idx], &double_mul_thunk, 8); idx += 8; // mov rax, thunk
    p_b[idx++] = 0xff; p_b[idx++] = 0xd0; // call rax
    p_b[idx++] = 0x66; p_b[idx++] = 0x48; p_b[idx++] = 0x0f; p_b[idx++] = 0x7e; p_b[idx++] = 0xc0; // movq rax, xmm0
    p_b[idx++] = 0xc3; // ret

    Common::VirtualMemory::FlushInstructionCache(code_addr_b, idx);
    uint64_t ret_dbl_bits = RunGuestSnippet(code_addr_b, stack_addr_b);
    double ret_double = std::bit_cast<double>(ret_dbl_bits);
    Check(std::fabs(ret_double - 10.0) < 0.0001, "HostDoubleMul result must be 10.0");
    std::printf("  -> [PASS] DoubleArg01_DoubleRet: 2.5 * 4.0 = %f\n", ret_double);
}

// Test 3: Unaligned Atomic Operations (lock cmpxchg on unaligned boundary)
void TestUnalignedAtomics() {
    std::printf("[TEST] Running Unaligned_Atomics_LockCmpxchg...\n");

    alignas(64) uint8_t target_buffer[64];
    std::memset(target_buffer, 0, sizeof(target_buffer));

    // Intentionally offset by 3 bytes to guarantee unaligned access across 8-byte boundary
    uint64_t* unaligned_ptr = reinterpret_cast<uint64_t*>(&target_buffer[3]);
    const uint64_t kInitialVal = 0x1122334455667788ULL;
    const uint64_t kNewVal     = 0x99AABBCCDDEEFF00ULL;
    *unaligned_ptr = kInitialVal;

    uint64_t code_addr = AllocateGuestCodePage();
    uint64_t stack_addr = AllocateGuestStack();

    // Assemble x86-64 code:
    // mov rdi, unaligned_ptr (passed in rdi via a0)
    // mov rax, kInitialVal
    // mov rsi, kNewVal
    // lock cmpxchg qword ptr [rdi], rsi
    // ret
    uint8_t* p = reinterpret_cast<uint8_t*>(code_addr);
    size_t idx = 0;

    // mov rax, kInitialVal (48 b8 ...)
    p[idx++] = 0x48; p[idx++] = 0xb8; std::memcpy(&p[idx], &kInitialVal, 8); idx += 8;

    // mov rsi, kNewVal (48 be ...)
    p[idx++] = 0x48; p[idx++] = 0xbe; std::memcpy(&p[idx], &kNewVal, 8); idx += 8;

    // lock cmpxchg qword ptr [rdi], rsi (f0 48 0f b1 37)
    p[idx++] = 0xf0; p[idx++] = 0x48; p[idx++] = 0x0f; p[idx++] = 0xb1; p[idx++] = 0x37;

    // ret (c3)
    p[idx++] = 0xc3;

    Common::VirtualMemory::FlushInstructionCache(code_addr, idx);

    int ret_val = RunGuestSnippet(code_addr, stack_addr, reinterpret_cast<uint64_t>(unaligned_ptr));
    (void)ret_val;

    Check(*unaligned_ptr == kNewVal, "Unaligned atomic update failed to write new value");
    std::printf("  -> [PASS] Unaligned lock cmpxchg successfully emulated without crash (*ptr=0x%016llx)\n",
                static_cast<unsigned long long>(*unaligned_ptr));
}

// Test 4: Nested Guest-to-Host-to-Guest Callback Re-entrancy
static uint64_t s_nested_guest_code_addr = 0;

static uint64_t HostNestedStep2(uint64_t val) {
    return val + 7;
}

static uint64_t HostNestedStep1(uint64_t guest_callback, uint64_t val) {
    uint64_t nested_stack = AllocateGuestStack();
    uint64_t stack_top = (nested_stack + 0x10000 - 128) & ~0xFull;
    stack_top -= 8;
    *reinterpret_cast<uint64_t*>(stack_top) = Loader::Fex::FexHleBridge::Instance().GetHltAddress();

    auto* thread = Loader::Fex::FexGuestEngine::Instance().CreateThread(
        guest_callback, stack_top, val * 3, 0, 0, 0);
    Check(thread != nullptr, "CreateThread for nested callback");
    int inner_res = Loader::Fex::FexGuestEngine::Instance().Run(thread, false);
    Loader::Fex::FexGuestEngine::Instance().DestroyThread(thread);

    return static_cast<uint64_t>(inner_res) * 2;
}

void TestCallbackReentrancy() {
    std::printf("[TEST] Running Callback_Reentrant_GuestHostGuest...\n");

    uint64_t host_step1_thunk = Loader::Fex::FexHleBridge::Instance().RegisterFunction(
        reinterpret_cast<uint64_t>(HostNestedStep1), "HostNestedStep1", Loader::Fex::ThunkSignature::IntOnly);
    uint64_t host_step2_thunk = Loader::Fex::FexHleBridge::Instance().RegisterFunction(
        reinterpret_cast<uint64_t>(HostNestedStep2), "HostNestedStep2", Loader::Fex::ThunkSignature::IntOnly);

    // 1. Assemble Inner Guest Callback (receives val in RDI, calls HostNestedStep2, returns result)
    s_nested_guest_code_addr = AllocateGuestCodePage();
    uint8_t* p_inner = reinterpret_cast<uint8_t*>(s_nested_guest_code_addr);
    size_t idx_inner = 0;
    // mov rax, host_step2_thunk
    p_inner[idx_inner++] = 0x48; p_inner[idx_inner++] = 0xb8;
    std::memcpy(&p_inner[idx_inner], &host_step2_thunk, 8); idx_inner += 8;
    // call rax
    p_inner[idx_inner++] = 0xff; p_inner[idx_inner++] = 0xd0;
    // ret
    p_inner[idx_inner++] = 0xc3;
    Common::VirtualMemory::FlushInstructionCache(s_nested_guest_code_addr, idx_inner);

    // 2. Assemble Outer Guest Routine:
    // mov rdi, s_nested_guest_code_addr (arg0)
    // mov rsi, 5                        (arg1 = 5)
    // mov rax, host_step1_thunk
    // call rax
    // ret
    uint64_t outer_code_addr = AllocateGuestCodePage();
    uint64_t outer_stack_addr = AllocateGuestStack();

    uint8_t* p_outer = reinterpret_cast<uint8_t*>(outer_code_addr);
    size_t idx_outer = 0;
    p_outer[idx_outer++] = 0x48; p_outer[idx_outer++] = 0xbf;
    std::memcpy(&p_outer[idx_outer], &s_nested_guest_code_addr, 8); idx_outer += 8;
    p_outer[idx_outer++] = 0x48; p_outer[idx_outer++] = 0xbe;
    uint64_t val5 = 5;
    std::memcpy(&p_outer[idx_outer], &val5, 8); idx_outer += 8;
    p_outer[idx_outer++] = 0x48; p_outer[idx_outer++] = 0xb8;
    std::memcpy(&p_outer[idx_outer], &host_step1_thunk, 8); idx_outer += 8;
    p_outer[idx_outer++] = 0xff; p_outer[idx_outer++] = 0xd0; // call rax
    p_outer[idx_outer++] = 0xc3; // ret
    Common::VirtualMemory::FlushInstructionCache(outer_code_addr, idx_outer);

    // Expected:
    // Outer passes (cb, 5).
    // HostStep1 invokes nested guest cb with (5 * 3 = 15).
    // Nested guest cb calls HostStep2(15) -> returns 15 + 7 = 22.
    // HostStep1 gets 22 -> returns 22 * 2 = 44.
    // Outer guest receives 44 and returns 44.
    uint64_t final_res = RunGuestSnippet(outer_code_addr, outer_stack_addr);
    Check(final_res == 44, "Reentrant callback execution returned unexpected value");
    std::printf("  -> [PASS] Callback_Reentrant: Outer guest received final result=%llu\n",
                static_cast<unsigned long long>(final_res));
}

} // namespace

int main() {
    std::printf("=================================================================\n");
    std::printf(" KytyPS5: ARM64 FEX JIT & Bridge Test Suite (FexJitBridgeTests)\n");
    std::printf("=================================================================\n");

    Common::InitializeThreads();
    Libs::LibKernel::Memory::Initialize();

    bool ok = Loader::Fex::FexGuestEngine::Instance().Initialize();
    Check(ok, "FexGuestEngine::Initialize failed");

    TestAbiIntegerArgs();
    TestAbiFloatArgsAndReturn();
    TestUnalignedAtomics();
    TestCallbackReentrancy();

    std::printf("\n>>> ALL FEX JIT BRIDGE TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
