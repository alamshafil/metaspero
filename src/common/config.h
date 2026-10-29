#ifndef KYTY_COMMON_CONFIG_H_
#define KYTY_COMMON_CONFIG_H_

#define KYTY_PLATFORM_WINDOWS 1
#define KYTY_PLATFORM_LINUX   5

#define KYTY_COMPILER_GCC   1
#define KYTY_COMPILER_CLANG 2

#define KYTY_LINKER_LD       1
#define KYTY_LINKER_LLD      2
#define KYTY_LINKER_LLD_LINK 3

#define KYTY_BUILD_DEBUG   1
#define KYTY_BUILD_RELEASE 2

#define KYTY_ENDIAN_BIG    1
#define KYTY_ENDIAN_LITTLE 2

#include "cmake_config.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#if defined(__arm64__) || defined(__aarch64__) || defined(KYTY_ENABLE_FEX_GUEST_CPU) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
#define KYTY_ARM64_APPLE 1
#endif
#endif

#endif /* KYTY_COMMON_CONFIG_H_ */
