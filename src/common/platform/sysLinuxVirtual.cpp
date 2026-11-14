#include "common/common.h"

#if KYTY_PLATFORM != KYTY_PLATFORM_LINUX
// #error "KYTY_PLATFORM != KYTY_PLATFORM_LINUX"
#else

#include "common/virtualMemory.h"

#include <sys/mman.h>
#include <unistd.h>

// IWYU pragma: no_include <asm/mman-common.h>
// IWYU pragma: no_include <asm/mman.h>
// IWYU pragma: no_include <linux/mman.h>

namespace Common::VirtualMemory {

static int get_protection_flag(Mode mode) {
	int protect = PROT_NONE;
	switch (mode) {
		case Mode::Read: protect = PROT_READ; break;
		case Mode::Write:
		case Mode::ReadWrite: protect = PROT_READ | PROT_WRITE; break; // NOLINT
#if defined(__APPLE__) && defined(__aarch64__) && KYTY_ENABLE_FEX_GUEST_CPU
		// On Apple Silicon, guest code is emulated via FEXCore JIT.
		// The host CPU never branches into guest memory directly, execution happens in FEX JIT buffers.
		case Mode::Execute:
		case Mode::ExecuteRead: protect = PROT_READ; break;
		case Mode::ExecuteWrite:
		case Mode::ExecuteReadWrite: protect = PROT_READ | PROT_WRITE; break;
#else
		case Mode::Execute: protect = PROT_EXEC; break;
		case Mode::ExecuteRead: protect = PROT_EXEC | PROT_READ; break; // NOLINT
		case Mode::ExecuteWrite:
		case Mode::ExecuteReadWrite: protect = PROT_EXEC | PROT_WRITE | PROT_READ; break; // NOLINT
#endif
		case Mode::NoAccess:
		default: protect = PROT_NONE; break;
	}
	return protect;
}

bool AllocFixed(uint64_t address, uint64_t size, Mode mode) {
	auto* addr  = reinterpret_cast<void*>(static_cast<uintptr_t>(address));
	int   flags = MAP_PRIVATE | MAP_ANON;
#ifdef MAP_FIXED_NOREPLACE
	flags |= MAP_FIXED_NOREPLACE;
#endif
	// Without MAP_FIXED_NOREPLACE (macOS) the address is a hint, and an occupied range maps
	// elsewhere.
	void* ptr = mmap(addr, size, get_protection_flag(mode), flags, -1, 0); // NOLINT
	if (ptr != MAP_FAILED && ptr != addr) {
		munmap(ptr, size);
		return false;
	}
	return ptr != MAP_FAILED;
}

bool Free(uint64_t address, uint64_t size) {
	return munmap(reinterpret_cast<void*>(static_cast<uintptr_t>(address)), size) == 0;
}

bool Protect(uint64_t address, uint64_t size, Mode mode) {
	if (size == 0) {
		return true;
	}
	const auto addr      = static_cast<uintptr_t>(address);
	const auto page_size = static_cast<uintptr_t>(getpagesize());
	const auto page_mask = page_size - 1;
	const auto start     = addr & ~page_mask;
	const auto end       = (addr + size + page_mask) & ~page_mask;
	return mprotect(reinterpret_cast<void*>(start), end - start, get_protection_flag(mode)) == 0;
}

bool FlushInstructionCache(uint64_t /*address*/, uint64_t /*size*/) {
	return true;
}

} // namespace Common::VirtualMemory

#endif
