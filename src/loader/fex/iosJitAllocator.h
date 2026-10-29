// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef KYTY_LOADER_FEX_IOS_JIT_ALLOCATOR_H_
#define KYTY_LOADER_FEX_IOS_JIT_ALLOCATOR_H_

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__)

#include <cstddef>
#include <cstdint>

namespace Loader::Fex {

struct DualMappedRegion {
    uint8_t* rw_addr{nullptr}; ///< Writable address for code generation.
    uint8_t* rx_addr{nullptr}; ///< Executable address (different virtual address, same physical pages).
    size_t   size{0};          ///< Size in bytes.

    [[nodiscard]] bool IsValid() const noexcept {
        return rw_addr != nullptr && rx_addr != nullptr && size > 0;
    }

    /// Allocate a dual-mapped JIT region using BreakpointJIT (StikDebug) or MAP_JIT fallback.
    static DualMappedRegion Allocate(size_t size) noexcept;

    DualMappedRegion() noexcept = default;
    DualMappedRegion(uint8_t* rw, uint8_t* rx, size_t sz) noexcept
        : rw_addr(rw), rx_addr(rx), size(sz) {}
    ~DualMappedRegion();

    DualMappedRegion(const DualMappedRegion&) = delete;
    DualMappedRegion& operator=(const DualMappedRegion&) = delete;
    DualMappedRegion(DualMappedRegion&& other) noexcept;
    DualMappedRegion& operator=(DualMappedRegion&& other) noexcept;
};

/// Returns true if JIT execution is available on this iOS device.
bool IsIosJitAvailable() noexcept;

/// Attempts to acquire and preallocate the JIT memory pool (64 MB) via Xcode or StikDebug.
bool KytyAcquireJitPool() noexcept;

/// Checks if the JIT memory pool has been verified and is ready for allocations.
bool KytyIsJitPoolReady() noexcept;

/// Returns a human-readable diagnostic description of the JIT pool status.
const char* KytyGetJitPoolStatus() noexcept;

/// Resets per-thread JIT pool allocations for a new game session while preserving the global FEX dispatcher.
void KytyResetJitPoolForNewSession() noexcept;

} // namespace Loader::Fex

extern "C" {
__attribute__((visibility("default"))) bool KytyAcquireJitPool(void) noexcept;
__attribute__((visibility("default"))) bool KytyIsJitPoolReady(void) noexcept;
__attribute__((visibility("default"))) const char* KytyGetJitPoolStatus(void) noexcept;
__attribute__((visibility("default"))) void KytyResetJitPoolForNewSession(void) noexcept;
__attribute__((visibility("default"))) bool KytyDualMappedAlloc(size_t size, void** out_rw, void** out_rx) noexcept;
__attribute__((visibility("default"))) void KytyDualMappedFree(void* rw, void* rx, size_t size) noexcept;
__attribute__((visibility("default"))) void* BreakGetJITMapping(void* addr, size_t len) noexcept;
__attribute__((visibility("default"))) void BreakJITDetach(void) noexcept;
}

#endif // defined(__APPLE__)
#endif // KYTY_LOADER_FEX_IOS_JIT_ALLOCATOR_H_
