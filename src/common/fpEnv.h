#ifndef KYTY_COMMON_FP_ENV_H_
#define KYTY_COMMON_FP_ENV_H_

#include "common/common.h"

namespace Common {

struct FpEnv {
	uint32_t mxcsr;
	uint16_t control;
};

inline constexpr FpEnv GuestFpEnv {0x9fc0, 0x037f};

#if defined(__x86_64__) || defined(_M_X64)
inline FpEnv ReadFpEnv() {
	FpEnv env {};
	asm volatile("stmxcsr %0\n\tfnstcw %1" : "=m"(env.mxcsr), "=m"(env.control) : : "memory");
	return env;
}

inline void WriteFpEnv(FpEnv env) {
	asm volatile("ldmxcsr %0\n\tfldcw %1" : : "m"(env.mxcsr), "m"(env.control) : "memory");
}
#else
// On ARM64 hosts guest x86 code runs through FEXCore, which owns the guest FP environment.
// The host MXCSR/x87 state is not the guest state here, so keep these as no-ops.
inline FpEnv ReadFpEnv() {
	return GuestFpEnv;
}

inline void WriteFpEnv(FpEnv /*env*/) {}
#endif

} // namespace Common

#endif /* KYTY_COMMON_FP_ENV_H_ */
