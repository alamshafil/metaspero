# Darwin ARM64 JIT and Runtime Architecture

This document describes the design and runtime architecture of KytyPS5 on Apple Silicon (macOS and iOS) using the custom FEXCore JIT engine.

## 1. Architecture Overview

KytyPS5 on Darwin separates UI from core emulation:

- Frontend: SwiftUI app on iOS and macOS (`frontend/ios/`), with an optional CLI binary on macOS (`kyty_emulator`).
- Bridge: Objective-C++ bridge (`frontend/ios/Bridge/KytyBridge.mm`) connects Swift UI to the C++ core and pipes standard output to the UI log viewer.
- Core emulation: OS HLE (LibKernel, pthreads, memory management), graphics translation via MoltenVK, and multimedia subsystems.
- CPU translation: Custom FEXCore fork (`alamshafil/fexcore-darwin`) translating guest x86_64 to ARM64 at runtime, based on the porting work from AetherPS4.

### Compatibility Triage

- PC works, fork fails: Usually caused by ARM64 FEX translation issues (unsupported instructions, traps, or memory ordering) or host RAM limits.
- macOS works, iOS fails: CPU translation is functional. Failures are usually caused by iOS Jetsam memory caps or mobile Metal/MoltenVK feature limits.

## 2. JIT Execution and Dual-Mapping

Darwin enforces W^X (Write-XOR-Execute) restrictions. Pages cannot be simultaneously writable and executable.

### Dual-Mapping (`iosJitAllocator.cpp`)

1. Allocate writable memory: Anonymous private memory is mapped as read/write via `mmap`.
2. Remap as executable: Mach `vm_remap` maps the same physical pages to a secondary virtual address, marked executable via `vm_protect`.
3. Execution: FEXCore emits ARM64 instructions to the writable pointer, the CPU executes them from the executable pointer.

### Traps and Self-Verification

- Software breakpoints (`brk`) trigger Mach exceptions that crash or stall under debuggers on Darwin. All userland `brk` instructions are avoided in favor of normal function returns and signals.
- At startup, the JIT allocator executes a small test instruction pair under a signal guard to confirm dual-mapping works before running guest code.

## 3. iOS JIT Activation and Entitlements

### Activation

iOS requires an attached debugger for JIT permissions (`get-task-allow`). Connect LocalDevVPN on-device and attach using StikDebug before launching games.

### Entitlements (`src/ios_jit.entitlements`)

- `get-task-allow`: Enables debugger attachment needed for JIT execution.
- `com.apple.developer.kernel.increased-memory-limit`: Allows the app to use extended device RAM. Without it, the iOS Jetsam daemon terminates the process around 2 GB to 3 GB of usage.
- An in-app toast monitor (`MemoryWarningToastView.swift`) tracks memory pressure in real time.

## 4. Virtual Memory Management

### Direct Memory Backing (RAM-Backed Mach VM Remap)

- PS5 unified direct memory allows multiple virtual addresses to alias the same physical pages.
- Memory backing is allocated in RAM using anonymous shared memory (`mmap(MAP_ANON | MAP_SHARED)`). Views are remapped into guest virtual space using Mach `vm_remap` (`VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE`), eliminating disk I/O.
- Note: Earlier builds temporarily used APFS sparse files with `ftruncate` before switching to Mach VM remap.

### Address Space Layout

- Guest user base (`0x200000000` to `0x700000000`): General heaps and direct memory mappings.
- High user space (`0x7ea000000`): Stacks and dynamic allocations default here to avoid collisions with low fixed mappings.
- System reserved (`0x800000000`): Holds HLE thunk tables and trampolines, protected against overwrite.

## 5. HLE Thunk System

HLE thunks bridge System V AMD64 guest calling conventions to host ARM64 AAPCS:

- Template traits detect argument counts and types at compile time.
- Integer and pointer arguments are mapped from guest registers (`rdi`..`r9`) and stack slots.
- Floating-point arguments are read from saved XMM registers.
- Thunk stubs save floating-point registers to the guest stack red zone before invoking syscalls to avoid register corruption.

## 6. Guest Fiber Context Switching

PS5 games often use userland cooperative threading (fibers) that manipulate x86_64 registers directly:

- A dedicated x86_64 thunk saves caller registers to the stack red zone and executes a syscall.
- If a context switch is scheduled, target registers are written to a thread-local structure.
- Registers are restored directly from host thread-local storage rather than the departing fiber's stack, preventing cross-thread race conditions.

## 7. Concurrency and Synchronization

Apple Silicon uses a weak memory ordering model:

- Standard memory accesses run in No-TSO mode without global barriers to avoid alignment faults on unaligned loads and stores.
- Explicit memory barriers (`dmb ish`) execute at HLE synchronization primitives (mutexes, event flags, fences).
- Unaligned atomic instructions that raise Darwin `SIGBUS` are caught by a signal handler and serialized through striped mutex locks.
- Guest `exit` calls terminate only the guest thread via `pthread_exit`, keeping host runtime services alive.

## 8. Graphics and Logging

- Graphics: Emulated PS5 Vulkan commands are translated to Metal via MoltenVK and displayed in SwiftUI using `CAMetalLayer`.
- Logging: High-frequency debug logs are silenced on iOS to reduce overhead. Standard output is captured via a pipe and displayed in the UI log view.
