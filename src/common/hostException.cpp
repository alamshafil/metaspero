#include "common/hostException.h"

#include <atomic>
#include <cstdio>

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#include <windows.h> // IWYU pragma: keep
#else
#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <initializer_list>
#include <unistd.h>
#if defined(__APPLE__)
#include "common/assert.h"
#include <TargetConditionals.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <setjmp.h>
#include <sys/ucontext.h>
#else
#include <setjmp.h>
#include <ucontext.h> // IWYU pragma: keep
#endif
#endif

// IWYU pragma: no_include <errhandlingapi.h>
// IWYU pragma: no_include <excpt.h>
// IWYU pragma: no_include <minwinbase.h>
// IWYU pragma: no_include <minwindef.h>
// IWYU pragma: no_include <wtypes.h>

namespace Common::HostException {

static std::atomic<FexSignalHandler_t> g_fex_signal_handler{nullptr};
static std::atomic<bool> g_emulation_shutting_down{false};

void RegisterFexSignalHandler(FexSignalHandler_t handler) {
	g_fex_signal_handler.store(handler, std::memory_order_release);
}

void SetEmulationShuttingDown(bool shutting_down) {
	g_emulation_shutting_down.store(shutting_down, std::memory_order_release);
}

bool IsEmulationShuttingDown() {
	return g_emulation_shutting_down.load(std::memory_order_acquire);
}

#if KYTY_PLATFORM != KYTY_PLATFORM_WINDOWS
static thread_local bool g_allocator_selftest_running = false;
static thread_local sigjmp_buf g_allocator_selftest_jump;

void SetAllocatorSelftestRunning(bool running) {
	g_allocator_selftest_running = running;
}

bool IsAllocatorSelftestRunning() {
	return g_allocator_selftest_running;
}

void* GetAllocatorSelftestJumpBuf() {
	return &g_allocator_selftest_jump;
}

[[noreturn]] void JumpAllocatorSelftest() {
	g_allocator_selftest_running = false;
	siglongjmp(g_allocator_selftest_jump, 1);
}
#else
void SetAllocatorSelftestRunning(bool /*running*/) {}
bool IsAllocatorSelftestRunning() { return false; }
void* GetAllocatorSelftestJumpBuf() { return nullptr; }
[[noreturn]] void JumpAllocatorSelftest() { std::abort(); }
#endif

#if !defined(__APPLE__)

static std::atomic<Handler> g_handler {nullptr};
static std::atomic_uint32_t g_install_state {0};

static_assert(decltype(g_handler)::is_always_lock_free);
static_assert(decltype(g_install_state)::is_always_lock_free);
#endif

#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX

// macOS uses the same POSIX platform setting and needs a signal stack too.
class ThreadSignalStack {
public:
	ThreadSignalStack() {
		const auto page_size = static_cast<size_t>(::getpagesize());
		const auto stack_size =
		    (std::max<size_t>(64 * 1024, MINSIGSTKSZ) + page_size - 1) & ~(page_size - 1);
		if (::posix_memalign(&m_memory, page_size, stack_size) != 0) {
			return;
		}

		stack_t stack {};
		stack.ss_sp   = m_memory;
		stack.ss_size = stack_size;
		if (::sigaltstack(&stack, &m_previous) != 0) {
			std::free(m_memory);
			m_memory = nullptr;
		}
	}

	~ThreadSignalStack() {
		if (m_memory != nullptr && ::sigaltstack(&m_previous, nullptr) == 0) {
			std::free(m_memory);
		}
	}

	[[nodiscard]] bool IsInitialized() const { return m_memory != nullptr; }

	KYTY_CLASS_NO_COPY(ThreadSignalStack)

private:
	void*   m_memory = nullptr;
	stack_t m_previous {};
};

bool InitializeThreadSignalStack() {
	// Keep fault handling off guest stacks, which GPU tracking can make read-only.
	thread_local ThreadSignalStack signal_stack;
	return signal_stack.IsInitialized();
}

#endif

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS

static LONG WINAPI ExceptionFilter(PEXCEPTION_POINTERS exception) noexcept {
	auto* exception_record = exception->ExceptionRecord;

	if (exception_record->ExceptionCode == DBG_PRINTEXCEPTION_C ||
	    exception_record->ExceptionCode == DBG_PRINTEXCEPTION_WIDE_C) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	if (exception_record->ExceptionCode == 0x406D1388) {
		// Set a thread name.
		return EXCEPTION_CONTINUE_EXECUTION;
	}

	ExceptionInfo info {};
	info.exception_address = reinterpret_cast<uint64_t>(exception_record->ExceptionAddress);
	info.native_code       = exception_record->ExceptionCode;
	info.native_context    = exception->ContextRecord;

	if (exception_record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
		info.type = ExceptionType::AccessViolation;
		switch (exception_record->ExceptionInformation[0]) {
			case 0: info.access_violation_type = AccessViolationType::Read; break;
			case 1: info.access_violation_type = AccessViolationType::Write; break;
			case 8: info.access_violation_type = AccessViolationType::Execute; break;
			default: info.access_violation_type = AccessViolationType::Unknown; break;
		}
		info.access_violation_vaddr = exception_record->ExceptionInformation[1];
	} else if (exception_record->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION) {
		info.type = ExceptionType::IllegalInstruction;
	} else {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	info.rax = exception->ContextRecord->Rax;
	info.rbx = exception->ContextRecord->Rbx;
	info.rcx = exception->ContextRecord->Rcx;
	info.rdx = exception->ContextRecord->Rdx;
	info.rsi = exception->ContextRecord->Rsi;
	info.rdi = exception->ContextRecord->Rdi;
	info.rbp = exception->ContextRecord->Rbp;
	info.rsp = exception->ContextRecord->Rsp;
	info.r8  = exception->ContextRecord->R8;
	info.r9  = exception->ContextRecord->R9;
	info.r10 = exception->ContextRecord->R10;
	info.r11 = exception->ContextRecord->R11;
	info.r12 = exception->ContextRecord->R12;
	info.r13 = exception->ContextRecord->R13;
	info.r14 = exception->ContextRecord->R14;
	info.r15 = exception->ContextRecord->R15;

	const auto handler = g_handler.load(std::memory_order_acquire);
	if (handler != nullptr && handler(info)) {
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

#elif defined(__APPLE__)

static std::atomic<Handler> g_handler {nullptr};
static std::atomic_uint32_t g_install_state {0};

static_assert(decltype(g_handler)::is_always_lock_free);
static_assert(decltype(g_install_state)::is_always_lock_free);

#if defined(__arm64__) || defined(__aarch64__)
#include <mach/arm/thread_status.h>

static AccessViolationType DecodeAccess(uint64_t esr) {
	uint32_t ec = (esr >> 26) & 0x3f;
	if (ec == 0x20 || ec == 0x21) {
		return AccessViolationType::Execute;
	}
	if ((esr & 0x40u) != 0) {
		return AccessViolationType::Write;
	}
	return AccessViolationType::Read;
}

static void SignalHandler(int sig, siginfo_t* si, void* uctx) {
	if (IsAllocatorSelftestRunning()) {
		JumpAllocatorSelftest();
	}

	auto*       uc = static_cast<ucontext_t*>(uctx);
	const auto* mc = uc->uc_mcontext;
	const auto& ss = mc->__ss;

	ExceptionInfo info {};
	info.exception_address = arm_thread_state64_get_pc(ss);
	info.native_code       = static_cast<uint32_t>(si->si_code);
	info.native_context    = uctx;

	if (sig == SIGILL || sig == SIGTRAP) {
		info.type = ExceptionType::IllegalInstruction;
	} else {
		info.type                   = ExceptionType::AccessViolation;
		info.access_violation_type  = DecodeAccess(mc->__es.__esr);
		info.access_violation_vaddr = reinterpret_cast<uint64_t>(si->si_addr);
	}

#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
	if (ss.__x[28] != 0) {
		info.rax = ss.__x[4];
		info.rdx = ss.__x[5];
		info.rbx = ss.__x[6];
		info.rcx = ss.__x[7];
		info.rsp = ss.__x[8];
		info.rbp = ss.__x[9];
		info.rsi = ss.__x[10];
		info.rdi = ss.__x[11];
		info.r8  = ss.__x[12];
		info.r9  = ss.__x[13];
		info.r10 = ss.__x[14];
		info.r11 = ss.__x[15];
		info.r12 = ss.__x[16];
		info.r13 = ss.__x[17];
		info.r14 = ss.__x[19];
		info.r15 = arm_thread_state64_get_fp(ss);
	} else
#endif
	{
		info.rsp = arm_thread_state64_get_sp(ss);
		info.rbp = arm_thread_state64_get_fp(ss);
		info.rax = ss.__x[0];
		info.rbx = ss.__x[1];
		info.rcx = ss.__x[2];
		info.rdx = ss.__x[3];
		info.rsi = ss.__x[4];
		info.rdi = ss.__x[5];
		info.r8  = ss.__x[6];
		info.r9  = ss.__x[7];
		info.r10 = ss.__x[8];
		info.r11 = ss.__x[9];
		info.r12 = ss.__x[10];
		info.r13 = ss.__x[11];
		info.r14 = ss.__x[12];
		info.r15 = arm_thread_state64_get_lr(ss);
	}

	// 1. If emulation is shutting down (e.g. user pressed Quit), suppress crash alert and suspend thread cleanly
	if (IsEmulationShuttingDown()) {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
		mach_port_t self = mach_thread_self();
		thread_suspend(self);
		mach_port_deallocate(mach_task_self(), self);
		while (true) {
			sleep(60);
		}
#endif
		return;
	}

	// 2. For SIGILL and SIGTRAP (illegal instruction / guest UD2 / traps), dispatch to FEX / emulator first
	if (sig == SIGILL || sig == SIGTRAP) {
		auto fex_handler = g_fex_signal_handler.load(std::memory_order_acquire);
		if (fex_handler != nullptr && fex_handler(sig, si, uctx)) {
			return;
		}
		const auto handler = g_handler.load(std::memory_order_acquire);
		if (handler != nullptr && handler(info)) {
			return;
		}
	}

	// 3. For memory access faults (SIGSEGV, SIGBUS on Darwin):
	//    On Darwin / iOS, write-protection faults on PROT_READ pages are delivered as SIGBUS (code=1).
	//    Resolving them via HandleGpuFault unprotects the page and invalidates GPU caches so the guest
	//    instruction can re-execute cleanly.
	if (info.type == ExceptionType::AccessViolation) {
		const auto handler = g_handler.load(std::memory_order_acquire);
		if (handler != nullptr && handler(info)) {
			return;
		}
	}

	// 4. Fallback to FEX handler (for unaligned accesses on device memory or atomic instructions)
	auto fex_handler = g_fex_signal_handler.load(std::memory_order_acquire);
	if (fex_handler != nullptr && fex_handler(sig, si, uctx)) {
		return;
	}

	// 5. For any other signal, try g_handler if not already tried
	if (info.type != ExceptionType::AccessViolation && sig != SIGILL && sig != SIGTRAP) {
		const auto handler = g_handler.load(std::memory_order_acquire);
		if (handler != nullptr && handler(info)) {
			return;
		}
	}

	SetEmulationShuttingDown(true);

#if defined(__APPLE__)
	char crash_buf[4096];
	uint64_t crash_pc = static_cast<uint64_t>(arm_thread_state64_get_pc(ss));
	uint64_t crash_lr = static_cast<uint64_t>(arm_thread_state64_get_lr(ss));
	uint64_t crash_fp = static_cast<uint64_t>(arm_thread_state64_get_fp(ss));
	uint64_t crash_sp = static_cast<uint64_t>(arm_thread_state64_get_sp(ss));

	int clen = std::snprintf(crash_buf, sizeof(crash_buf),
	              "[HostFatal] Signal %d (%s, code=%d) at addr=0x%016llx, PC=0x%016llx, LR=0x%016llx, FP=0x%016llx, SP=0x%016llx\n",
	              sig, strsignal(sig), si->si_code,
	              static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(si->si_addr)),
	              (unsigned long long)crash_pc,
	              (unsigned long long)crash_lr,
	              (unsigned long long)crash_fp,
	              (unsigned long long)crash_sp);

	Dl_info lr_dli {};
	if (dladdr(reinterpret_cast<void*>(crash_lr), &lr_dli) != 0 && lr_dli.dli_sname != nullptr) {
		clen += std::snprintf(crash_buf + clen, sizeof(crash_buf) - clen,
		                      "  LR symbol: %s (%s + 0x%lx)\n",
		                      lr_dli.dli_sname, lr_dli.dli_fname ? lr_dli.dli_fname : "?",
		                      static_cast<unsigned long>((uintptr_t)crash_lr - (uintptr_t)lr_dli.dli_saddr));
	}

	clen += std::snprintf(crash_buf + clen, sizeof(crash_buf) - clen,
	    "  x0=0x%016llx x1=0x%016llx x2=0x%016llx x3=0x%016llx\n"
	    "  x4=0x%016llx x5=0x%016llx x6=0x%016llx x7=0x%016llx\n"
	    "  x8=0x%016llx x9=0x%016llx x10=0x%016llx x11=0x%016llx\n"
	    "  x12=0x%016llx x13=0x%016llx x14=0x%016llx x15=0x%016llx\n"
	    "  x16=0x%016llx x17=0x%016llx x18=0x%016llx x19=0x%016llx\n"
	    "  x20=0x%016llx x21=0x%016llx x22=0x%016llx x23=0x%016llx\n"
	    "  x24=0x%016llx x25=0x%016llx x26=0x%016llx x27=0x%016llx\n"
	    "  x28=0x%016llx\n",
	    (unsigned long long)ss.__x[0], (unsigned long long)ss.__x[1], (unsigned long long)ss.__x[2], (unsigned long long)ss.__x[3],
	    (unsigned long long)ss.__x[4], (unsigned long long)ss.__x[5], (unsigned long long)ss.__x[6], (unsigned long long)ss.__x[7],
	    (unsigned long long)ss.__x[8], (unsigned long long)ss.__x[9], (unsigned long long)ss.__x[10], (unsigned long long)ss.__x[11],
	    (unsigned long long)ss.__x[12], (unsigned long long)ss.__x[13], (unsigned long long)ss.__x[14], (unsigned long long)ss.__x[15],
	    (unsigned long long)ss.__x[16], (unsigned long long)ss.__x[17], (unsigned long long)ss.__x[18], (unsigned long long)ss.__x[19],
	    (unsigned long long)ss.__x[20], (unsigned long long)ss.__x[21], (unsigned long long)ss.__x[22], (unsigned long long)ss.__x[23],
	    (unsigned long long)ss.__x[24], (unsigned long long)ss.__x[25], (unsigned long long)ss.__x[26], (unsigned long long)ss.__x[27],
	    (unsigned long long)ss.__x[28]);

	if (crash_sp != 0) {
		const auto* sp_words = reinterpret_cast<const uint64_t*>(crash_sp);
		clen += std::snprintf(crash_buf + clen, sizeof(crash_buf) - clen, "  Stack Words:\n");
		for (int i = 0; i < 24 && clen + 128 < (int)sizeof(crash_buf); ++i) {
			uint64_t val = sp_words[i];
			Dl_info dli {};
			if (dladdr(reinterpret_cast<void*>(val), &dli) != 0 && dli.dli_sname != nullptr) {
				clen += std::snprintf(crash_buf + clen, sizeof(crash_buf) - clen,
				                      "    [SP+0x%02x] 0x%016llx (%s + 0x%lx)\n",
				                      i * 8, (unsigned long long)val, dli.dli_sname,
				                      static_cast<unsigned long>((uintptr_t)val - (uintptr_t)dli.dli_saddr));
			} else {
				clen += std::snprintf(crash_buf + clen, sizeof(crash_buf) - clen,
				                      "    [SP+0x%02x] 0x%016llx\n",
				                      i * 8, (unsigned long long)val);
			}
		}
	}
	if (clen > 0) {
		write(STDOUT_FILENO, crash_buf, clen);
		write(STDERR_FILENO, crash_buf, clen);
	}
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
	Common::InvokeFatalHandler(sig, crash_buf);
	mach_port_t self = mach_thread_self();
	thread_suspend(self);
	mach_port_deallocate(mach_task_self(), self);
	while (true) {
		sleep(60);
	}
	return;
#else
	struct sigaction dfl {};
	dfl.sa_handler = SIG_DFL;
	sigemptyset(&dfl.sa_mask);
	sigaction(sig, &dfl, nullptr);
#endif
#else
	struct sigaction dfl {};
	dfl.sa_handler = SIG_DFL;
	sigemptyset(&dfl.sa_mask);
	sigaction(sig, &dfl, nullptr);
#endif
}
#else
// Translate the x86-64 page-fault error code (mcontext __es.__err) into an access type.
// bit 1 (0x2) = write, bit 4 (0x10) = instruction fetch, otherwise a read.
static AccessViolationType DecodeAccess(uint64_t err) {
	if ((err & 0x10u) != 0) {
		return AccessViolationType::Execute;
	}
	if ((err & 0x2u) != 0) {
		return AccessViolationType::Write;
	}
	return AccessViolationType::Read;
}

// POSIX signal handler that mirrors the Windows vectored handler: build an ExceptionInfo
// from the mcontext and dispatch. A resolved fault (handler returns true) simply returns,
// re-executing the faulting instruction against the now-fixed protection. An unresolved
// fault restores the default disposition so the retry terminates the process.
static void SignalHandler(int sig, siginfo_t* si, void* uctx) {
	if (IsAllocatorSelftestRunning()) {
		JumpAllocatorSelftest();
	}

	auto*       uc = static_cast<ucontext_t*>(uctx);
	const auto* mc = uc->uc_mcontext;
	const auto& ss = mc->__ss;

	ExceptionInfo info {};
	info.exception_address = ss.__rip;
	info.native_code       = static_cast<uint32_t>(si->si_code);
	info.native_context    = uctx;

	if (sig == SIGILL || sig == SIGTRAP) {
		info.type = ExceptionType::IllegalInstruction;
	} else {
		info.type                   = ExceptionType::AccessViolation;
		info.access_violation_type  = DecodeAccess(mc->__es.__err);
		info.access_violation_vaddr = reinterpret_cast<uint64_t>(si->si_addr);
	}

	info.rax = ss.__rax;
	info.rbx = ss.__rbx;
	info.rcx = ss.__rcx;
	info.rdx = ss.__rdx;
	info.rsi = ss.__rsi;
	info.rdi = ss.__rdi;
	info.rbp = ss.__rbp;
	info.rsp = ss.__rsp;
	info.r8  = ss.__r8;
	info.r9  = ss.__r9;
	info.r10 = ss.__r10;
	info.r11 = ss.__r11;
	info.r12 = ss.__r12;
	info.r13 = ss.__r13;
	info.r14 = ss.__r14;
	info.r15 = ss.__r15;

	const auto handler = g_handler.load(std::memory_order_acquire);
	if (handler != nullptr && handler(info)) {
		return; // retry the faulting instruction against the fixed mapping
	}

	// Unresolved: restore the default action so the re-executed instruction terminates.
	struct sigaction dfl {};
	dfl.sa_handler = SIG_DFL;
	sigemptyset(&dfl.sa_mask);
	sigaction(sig, &dfl, nullptr);
}
#endif

#else

// x86-64 page-fault error bits.
constexpr uint64_t PAGE_FAULT_ERROR_WRITE       = 0x02;
constexpr uint64_t PAGE_FAULT_ERROR_INSTRUCTION = 0x10;

// Let the kernel handle an unresolved fault on retry.
static void ChainToDefault(int signal_number) noexcept {
	struct sigaction restore {};
	restore.sa_handler = SIG_DFL;
	sigemptyset(&restore.sa_mask);
	restore.sa_flags = 0;
	::sigaction(signal_number, &restore, nullptr);
}

static void SignalHandler(int signal_number, siginfo_t* signal_info, void* native_context) {
	auto* context = static_cast<ucontext_t*>(native_context);
	auto* gregs   = context->uc_mcontext.gregs;

	ExceptionInfo info {};
	info.exception_address = static_cast<uint64_t>(gregs[REG_RIP]);
	info.native_code       = static_cast<uint32_t>(signal_number);
	info.native_context    = context;

	if (signal_number == SIGSEGV || signal_number == SIGBUS) {
		info.type             = ExceptionType::AccessViolation;
		const auto error_code = static_cast<uint64_t>(gregs[REG_ERR]);
		if ((error_code & PAGE_FAULT_ERROR_INSTRUCTION) != 0) {
			info.access_violation_type = AccessViolationType::Execute;
		} else if ((error_code & PAGE_FAULT_ERROR_WRITE) != 0) {
			info.access_violation_type = AccessViolationType::Write;
		} else {
			info.access_violation_type = AccessViolationType::Read;
		}
		info.access_violation_vaddr = reinterpret_cast<uint64_t>(signal_info->si_addr);
	} else if (signal_number == SIGILL) {
		info.type = ExceptionType::IllegalInstruction;
	} else {
		ChainToDefault(signal_number);
		return;
	}

	info.rax = static_cast<uint64_t>(gregs[REG_RAX]);
	info.rbx = static_cast<uint64_t>(gregs[REG_RBX]);
	info.rcx = static_cast<uint64_t>(gregs[REG_RCX]);
	info.rdx = static_cast<uint64_t>(gregs[REG_RDX]);
	info.rsi = static_cast<uint64_t>(gregs[REG_RSI]);
	info.rdi = static_cast<uint64_t>(gregs[REG_RDI]);
	info.rbp = static_cast<uint64_t>(gregs[REG_RBP]);
	info.rsp = static_cast<uint64_t>(gregs[REG_RSP]);
	info.r8  = static_cast<uint64_t>(gregs[REG_R8]);
	info.r9  = static_cast<uint64_t>(gregs[REG_R9]);
	info.r10 = static_cast<uint64_t>(gregs[REG_R10]);
	info.r11 = static_cast<uint64_t>(gregs[REG_R11]);
	info.r12 = static_cast<uint64_t>(gregs[REG_R12]);
	info.r13 = static_cast<uint64_t>(gregs[REG_R13]);
	info.r14 = static_cast<uint64_t>(gregs[REG_R14]);
	info.r15 = static_cast<uint64_t>(gregs[REG_R15]);

	const auto handler = g_handler.load(std::memory_order_acquire);
	if (handler != nullptr && handler(info)) {
		return;
	}

	ChainToDefault(signal_number);
}

#endif

bool InstallHandler(Handler handler) {
	if (handler == nullptr) {
		return false;
	}

	uint32_t expected_state = 0;
	if (!g_install_state.compare_exchange_strong(expected_state, 1, std::memory_order_acq_rel)) {
		return expected_state == 2 && g_handler.load(std::memory_order_acquire) == handler;
	}

	g_handler.store(handler, std::memory_order_release);

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	if (AddVectoredExceptionHandler(0, ExceptionFilter) == nullptr) {
		g_handler.store(nullptr, std::memory_order_release);
		g_install_state.store(0, std::memory_order_release);
		printf("AddVectoredExceptionHandler() failed\n");
		return false;
	}
#elif defined(__APPLE__)
	struct sigaction sa {};
	sa.sa_sigaction = SignalHandler;
	sa.sa_flags     = SA_SIGINFO | SA_ONSTACK;
	sigemptyset(&sa.sa_mask);
	// The guest signal-dispatch path (KernelRaiseException) interrupts threads with
	// SIGUSR1; block it while a fault is being resolved so a stop-the-world request
	// cannot preempt the handler between the protection fix and the retry.
	sigaddset(&sa.sa_mask, SIGUSR1);

	// macOS raises SIGBUS for protection faults on some paths and SIGSEGV on others;
	// SIGILL covers instructions the host cannot execute (routed to the x64 emulator);
	// SIGTRAP covers guest INT3, breakpoints, and assertion aborts.
	bool ok = sigaction(SIGSEGV, &sa, nullptr) == 0 && sigaction(SIGBUS, &sa, nullptr) == 0 &&
	          sigaction(SIGILL, &sa, nullptr) == 0 && sigaction(SIGTRAP, &sa, nullptr) == 0;
	if (!ok) {
		g_handler.store(nullptr, std::memory_order_release);
		g_install_state.store(0, std::memory_order_release);
		printf("sigaction() failed to install the host fault handler\n");
		return false;
	}
#else
	struct sigaction action {};
	action.sa_sigaction = SignalHandler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;

	for (const int signal_number: {SIGSEGV, SIGBUS, SIGILL}) {
		if (::sigaction(signal_number, &action, nullptr) != 0) {
			g_handler.store(nullptr, std::memory_order_release);
			g_install_state.store(0, std::memory_order_release);
			printf("sigaction(%d) failed\n", signal_number);
			return false;
		}
	}
#endif

	g_install_state.store(2, std::memory_order_release);
	return true;
}

} // namespace Common::HostException
