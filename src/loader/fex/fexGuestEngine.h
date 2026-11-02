// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef KYTY_LOADER_FEX_GUEST_ENGINE_H_
#define KYTY_LOADER_FEX_GUEST_ENGINE_H_

#include <cstdint>
#include <memory>

namespace Loader::Fex {

struct GuestThreadState {
    void*    internal_thread{nullptr};
    uint64_t rip{0};
    uint64_t rsp{0};
    uint64_t rbp{0};
    uint64_t rdi{0};
    uint64_t rsi{0};
    uint64_t rdx{0};
    uint64_t rcx{0};
    uint64_t r8{0};
    uint64_t r9{0};
    uint64_t fs_base{0};
};

struct GuestRegisterSnapshot {
    uint64_t rax{0}, rbx{0}, rcx{0}, rdx{0}, rsi{0}, rdi{0}, rbp{0}, rsp{0};
    uint64_t r8{0}, r9{0}, r10{0}, r11{0}, r12{0}, r13{0}, r14{0}, r15{0};
    uint64_t rip{0};
};

class FexGuestEngine {
public:
    static FexGuestEngine& Instance();

    bool Initialize();
    void Shutdown();
    void StopAllThreads();
    void InvalidateCodeRange(uint64_t start, uint64_t length);

    static bool IsCurrentThreadFromStaleSession() noexcept;
    static void BindCurrentThreadToActiveSession() noexcept;
    static void CleanupCurrentThreadBeforeExit() noexcept;
    // Captures the guest CPU registers of the innermost guest thread running on the calling host thread.
    static bool GetCurrentThreadRegisters(GuestRegisterSnapshot* out) noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    GuestThreadState* CreateThread(uint64_t entry_rip, uint64_t stack_top, uint64_t arg0 = 0, uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t fs_base = 0);
    GuestThreadState* CreateThreadWithArgs(uint64_t entry_rip, uint64_t stack_top,
                                           uint64_t arg0 = 0, uint64_t arg1 = 0, uint64_t arg2 = 0,
                                           uint64_t arg3 = 0, uint64_t arg4 = 0, uint64_t arg5 = 0,
                                           uint64_t fs_base = 0);
    void DestroyThread(GuestThreadState* thread);

    uint64_t Run(GuestThreadState* thread, bool enable_watchdog = true);

    uint64_t CallGuestFunction(uint64_t rip,
                               uint64_t arg0 = 0, uint64_t arg1 = 0, uint64_t arg2 = 0,
                               uint64_t arg3 = 0, uint64_t arg4 = 0, uint64_t arg5 = 0);

    static bool HandleHostSignal(int sig, void* si, void* uctx);

private:
    FexGuestEngine() = default;
    ~FexGuestEngine();

    bool  m_initialized{false};
    void* m_fex_context{nullptr};
    void* m_signal_delegator{nullptr};
    void* m_syscall_handler{nullptr};
};

} // namespace Loader::Fex

#endif // KYTY_LOADER_FEX_GUEST_ENGINE_H_
