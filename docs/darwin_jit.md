# Darwin ARM64 JIT and Runtime Architecture (macOS & iOS)

This document describes the design, implementation, and build process for running KytyPS5 on Apple Silicon Darwin platforms (macOS ARM64 and iOS ARM64) using the custom FEXCore x86-64 to ARM64 JIT execution engine.

---

## 1. Architectural Overview

KytyPS5 on Darwin separates platform UI from core emulation:

- Frontend Layer:
  - macOS: Native CLI executable (`kyty_emulator`) or native SwiftUI application (`KytyPS5.app`).
  - iOS: SwiftUI application (`frontend/ios/`) containing game library management and full-screen Metal presentation (`CAMetalLayer`).
- Objective-C++ Bridge (`frontend/ios/Bridge/KytyBridge.mm`):
  - Bridges Swift UI states with the C++ emulator core.
  - Redirects standard output through a UNIX pipe for real-time UI logging.
  - Exposes dynamic MoltenVK version detection and JIT status monitoring.
- Emulation Core (`src/`):
  - Operating system HLE: LibKernel, POSIX threads, synchronization primitives, dynamic module linker (`runtimeLinker.cpp`), and memory management (`memory.cpp`).
  - Graphics: Vulkan 1.3 translation via MoltenVK targeting Apple Metal.
  - Audio and multimedia: LibAudio2 and AvPlayer HLE.
- CPU Translation Engine:
  - Custom FEXCore engine (`KYTY_ENABLE_FEX_GUEST_CPU` / `KYTY_ARM64_APPLE`) executing guest x86-64 code directly on host ARM64 hardware.
  - Synthetic HLE thunks and compile-time template ABI dispatcher (`src/loader/fex/fexHleBridge.cpp`).

---

## 2. JIT Execution Model and Dual-Mapping

Apple Darwin kernels enforce strict Write-XOR-Execute (W^X) policies. An application cannot directly write to a page and then execute from it without kernel-level permissions.

### Dual-Mapping Architecture (`iosJitAllocator.cpp`)

To avoid W^X violations and costly runtime memory permission toggles:

1. RW Mapping:
   An anonymous private memory buffer is mapped with read and write permissions:
   `mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)`
2. RX Mapping via `vm_remap`:
   A secondary virtual address range is mapped to the identical underlying physical pages using the Mach kernel API:
   `vm_remap(mach_task_self(), &rx_addr, size, 0, VM_FLAGS_ANYWHERE, mach_task_self(), rw_addr, FALSE, &cur_prot, &max_prot, VM_INHERIT_NONE)`
3. Executable Protection:
   The secondary mapping is marked executable:
   `vm_protect(mach_task_self(), rx_addr, size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE)`
4. Execution Flow:
   FEXCore emits generated ARM64 instructions into the writable pointer (`rw_addr`). The CPU executes those instructions from the executable pointer (`rx_addr`).

### Trap Instruction Handling

Under Darwin kernels, software breakpoint instructions (`brk #0x69`, `brk #0xf00d`) generate Mach exceptions (`EXC_BREAKPOINT`). If attached to LLDB, the debugger intercepts these before user POSIX signal handlers can run. When unattached, CrashReporter terminates the process. Consequently, all explicit userland `brk` trap instructions are avoided; control transfer relies strictly on normal function calls, signals, or return codes.

### JIT Self-Verification

Before translating guest game code, the JIT allocator performs a live two-instruction execution verification on physical hardware:
- Assembles `movz w0, #42` (opcode `0x52800540`) and `ret` (opcode `0xD65F03C0`) into the RW mapping.
- Invalidates the instruction cache using `sys_icache_invalidate`.
- Executes through the RX mapping under a `sigsetjmp` / `sigaction` guard catching `SIGBUS` and `SIGSEGV`.
- If the return value matches 42, the JIT dual-mapping is confirmed active.

---

## 3. iOS JIT Activation and Entitlements

### Why JIT Activation is Needed

iOS does not allow userland applications to allocate or execute JIT memory unless a debugger is attached to the process with debugging permissions (`get-task-allow`). On iOS 17 and later, this can be activated directly on-device using LocalDevVPN and StikDebug.

### How to Activate JIT

1. Connect LocalDevVPN:
   Open LocalDevVPN on your device and tap Connect to establish the local developer connection.
2. Launch KytyPS5:
   Open KytyPS5 and note the Process ID (PID) displayed in Settings or the JIT prompt.
3. Attach with StikDebug:
   Open StikDebug, select KytyPS5 (or match its PID), and tap Enable JIT.
4. Return to KytyPS5:
   The JIT badge will show as Active, allowing games to launch.

### Required Entitlements (`src/ios_jit.entitlements`)

- `get-task-allow`: Enables debugger attachment required for JIT compilation.
- `com.apple.developer.kernel.increased-memory-limit`: Grants access to extended unified device RAM, preventing iOS Jetsam from terminating the app during gameplay.

---

## 4. Virtual Memory and Address Space Architecture

### Unified Direct Memory Pool

- PS5 unified memory allows direct physical memory allocation (`[DMEM]`).
- On Darwin ARM64, `PhysicalMemory::TotalSize()` provides 13.5 GB (`13824 * 1024 * 1024`) to match real hardware.
- Memory is backed by `GuestBackingStore` using a sparse file on APFS (`ftruncate`), consuming physical RAM only as pages are committed.
- Direct memory slices are mapped on-demand into guest virtual address space using `mmap(..., MAP_SHARED | MAP_FIXED, fd, ...)`.

### Address Space Isolation

To avoid memory collisions between guest system heaps and emulator internal structures:

- Guest User Base: `0x200000000` to `0x700000000` is reserved for standard guest application heaps and direct memory mappings.
- High User Space Allocation (`0x00000007ea000000`):
  Module startup stacks (`AllocateGuestStackMemory`) and guest runtime allocations (`AllocateRuntimeMemory` with `search_addr = 0`) default their search base to `0x7ea000000`, located directly below `PTHREAD_STACK_TOP` (`0x7efff8000`). This prevents games mapping large direct memory blocks (`MAP_FIXED` / `0x10`) at `0x300000000` from unmapping runtime stacks or TLS regions.
- System Reserved Region (`0x800000000`):
  HLE thunk tables (`fex_hle_thunks`) and unresolved import trampolines are placed in the 32 GB system-reserved space. `ReplaceFixedRangeWithReserved` strictly disallows overwriting ranges named `fex_hle_thunks`.

### Safe Memory Probing and Address Translation

- Dynamic VM Probing:
  Before decoding guest instructions, FEX queries `QueryGuestExecutableRange`. On macOS, this uses `mach_vm_region`; on iOS, it uses `vm_region_64`. Unmapped or non-readable addresses immediately trigger guest `SIGSEGV` dispatch rather than dereferencing unmapped host pointers.
- Physical-to-Virtual Translation (`KernelTranslateAddress`):
  Direct memory callbacks emitted by GPU runtime libraries (e.g. command buffer grow callbacks) are translated from physical addresses to guest virtual addresses and verified for `KERNEL_PROT_CPU_EXEC` before invoking the JIT.

---

## 5. HLE Thunk System and ABI Marshalling

### Problem Definition

The PS5 guest environment uses the System V AMD64 ABI:
- Integer arguments: `RDI`, `RSI`, `RDX`, `RCX`, `R8`, `R9`.
- Floating-point arguments: `XMM0` through `XMM7`.
- Return values: `RAX` for integer/pointer, `XMM0` for float/double, `RAX` + `RDX` for 16-byte structs.

The Darwin host environment uses the ARM64 AAPCS calling convention:
- Integer arguments: `X0` through `X7`.
- Floating-point arguments: `S0` through `S7` / `D0` through `D7`.
- Return values: `X0` for integer/pointer, `D0` / `S0` for float/double.

### Compile-Time Template ABI Bridge

KytyPS5 uses template introspection (`HleDescriptor` / `Detail::HleTraits` in `src/loader/fex/fexHleBridge.h`):

1. Signature Detection:
   When `LIB_FUNC` registers a C++ function pointer `R (*)(Args...)`, template traits determine the number of integer and floating-point parameters at compile time without relying on runtime name string searches.
2. Argument Extraction:
   `Detail::UnpackHleArg` unpacks arguments from a unified structure:
   - Floating-point arguments (`float`, `double`) are retrieved sequentially from saved XMM registers.
   - Vector structures (e.g. 12-byte 3-float vectors such as `AudioOut2Position`) are unpacked across consecutive XMM registers according to the System V AMD64 specification.
   - Integer, pointer, and small struct arguments are retrieved from the 6 register arguments (`rdi`..`r9`) followed by stack overflow slots at `[guest_rsp + 8]`.
3. Return Value Handling:
   - Float and double returns are bit-cast to integers and written to `XMM0`.
   - 16-byte structs (e.g. `SizeAlign`) return the low 64 bits in `RAX` and the high 64 bits in `RDX`.
4. Variadic Functions:
   Variadic calls (`VA_ARGS` / `VA_CONTEXT`) pass pointer and register states along with `overflow_arg_area = guest_rsp + 8`, enabling format-string handlers (e.g. `guestPrintf`) to read arguments directly from the guest stack.

### Synthetic x86-64 Thunk Generator

- Each registered HLE function receives a 64-byte thunk stub in `fex_hle_thunks`.
- If floating-point parameters are present, the thunk saves `XMM0` through `XMMn` into the guest stack red zone (`[rsp - 8 * (i + 1)]`) before issuing `syscall (0x0F 0x05)`, ensuring integer registers (`rdi`..`r9`) are preserved without clobbering.
- Thread Safety: Function metadata is stored as `std::vector<std::unique_ptr<HleFunction>>`. Pointers returned during symbol lookup remain stable even when background threads register new modules concurrently.

---

## 6. Guest Fiber Context-Switching on ARM64

### Architectural Challenge

In PS5 games using userland cooperative threading (fibers), context switching routines (`FiberRun`, `FiberSwitch`, `FiberReturnToThread`) manipulate CPU register state directly. On x86-64 hosts, this is accomplished via inline assembly. On Apple Silicon, the host CPU is ARM64 while guest fibers are x86-64 code running under JIT.

### Zero-Overhead Guest Trampoline (`fexHleBridge.cpp`, `libKernel.cpp`)

1. Shared Trampoline Allocation:
   A dedicated x86-64 machine code routine is placed in executable thunk memory.
2. State Preservation Before Syscall:
   When a fiber switch is requested, the trampoline pushes caller callee-saved registers (`rbx, rbp, rdi, rsi, r12, r13, r14, r15`), `rsp`, and return address `rip` into the caller stack red zone (`[rsp - 80]`) and executes `syscall`.
3. Context Switch Scheduling:
   - If the HLE handler encounters an error, it returns a negative status code in `RAX`.
   - If a switch is scheduled, `FexHleBridge::ScheduleGuestFiberRestore` populates a thread-local structure (`GuestFiberRestoreFrame`) with target registers and returns the frame pointer in `RAX` (`rax > 0`).
4. Context Restoration After Syscall:
   On `rax > 0`, the trampoline restores `rbx, rbp, rdi, rsi, r12-r15, rsp`, target `rip` (`r11`), and target return value `rax` directly from the host thread-local restore frame, then performs `jmp r11`.
5. Thread-Safety Benefit:
   Because target state is restored from thread-local host memory rather than the departing fiber's stack, the departing fiber's stack is never accessed once marked idle, preventing cross-thread race conditions during fiber migration.

---

## 7. Concurrency, Memory Ordering, and Signals

### Memory Model on Apple Silicon

Apple Silicon implements a weak memory ordering model:

- Hardware TSO (`ACTLR_EL1.TSO`) is inaccessible in userland applications on iOS (attempting to write to `ACTLR_EL1` produces `SIGILL`).
- FEX Software TSO (`CONFIG_TSOENABLED=1`) introduces store/load barrier instructions (`LDAR`/`STLR`) across all memory accesses. Because games frequently perform unaligned accesses, this causes unrecoverable `SIGBUS (BUS_ADRALN)` signal storms.

### Hybrid Concurrency Strategy

1. Standard Memory Accesses (No-TSO Mode):
   General loads and stores run without global barriers. Native ARM64 instructions (`LDR`/`STR`) handle unaligned memory addresses without traps.
2. Synchronizing Barriers at HLE Boundaries:
   Explicit data memory barriers (`dmb ish`) are executed at synchronization primitives (`scePthreadMutexUnlock`, `sceKernelSignalEventFlag`, `AGC_WaitForFences`). `DMB` enforces ordering without taking memory operands, avoiding alignment faults.
3. Striped Locks for Unaligned Atomics:
   Unaligned atomic instructions (`lock cmpxchg`, etc.) raise Darwin `SIGBUS` (`BUS_ADRALN`). The signal handler catches this, serializes the atomic operation through a 256-bucket cacheline-aligned striped mutex table (`s_striped_atomic_locks`), advances the guest PC, and resumes execution without terminating the emulator.

### Thread Entry and Process Termination

- Host vs Guest Entry Detection:
  When worker threads are spawned via `PthreadCreate`, `dladdr` inspects the function pointer. If the pointer belongs to the host binary (e.g. host video/audio decoders), it executes natively on the host thread rather than entering the FEX x86-64 translator.
- Clean Guest Termination:
  On macOS and iOS, guest `LibC::exit` routes through `KytyNotifyGuestExit()` and terminates the calling guest thread via `pthread_exit(nullptr)`. Host `::exit()` is bypassed to prevent tearing down static C++ objects while background threads remain active.

---

## 8. Graphics, Windowing, and Logging

### Vulkan to Metal Translation (MoltenVK)

- The emulator core writes to Vulkan 1.3 command buffers.
- MoltenVK converts Vulkan commands and SPIR-V shaders directly into Apple Metal (`CAMetalLayer`, `MTLDevice`, `MTLRenderPipelineState`).
- SwiftUI hosts the rendering surface via `EmulatorMetalSurface`, binding directly to the underlying `CAMetalLayer`.

### Clean Log Routing

- Emulator core `LOGF` macros are silenced on iOS builds (`IsSilent() == true`), removing high-frequency tracing overhead.
- High-level status messages use `std::printf`.
- The Objective-C++ bridge redirects descriptor 1 (`STDOUT_FILENO`) through a UNIX pipe into the SwiftUI `LogView`.
