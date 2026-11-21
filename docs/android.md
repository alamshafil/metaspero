# Android Port Info

This doc gives a guide to the Android port of Metaspero, covering design, hardware requirements, GPU driver management, build info, and codebase changes.

## 1. Overview & Architecture

Metaspero runs natively on Android ARM64. Because Android is built on Linux and the 64-bit ARM architecture (AArch64), it shares significant foundational infrastructure with the Linux and Apple Silicon Darwin ports:
- CPU Translation: Guest x86_64 CPU instructions are translated at runtime to ARM64 via an integrated fork of [FEXCore](https://github.com/alamshafil/fexcore-darwin).
- Vulkan Rendering: Vulkan 1.3 replaces Apple MoltenVK, binding directly to native Android surfaces (`ANativeWindow`) via `VK_KHR_android_surface`.

### Architecture Stack

- Android APK Host Shell (`frontend/android`): Jetpack Compose, Google Material 3 theming, Gradle assembly.
- Dual-Platform UI Component (`MetasperoUI.aar`): SwiftUI frontend transpiled to Kotlin and Jetpack Compose via the Skip toolchain.
- JNI Bridge (`libkyty_android.so`): Native C++ interface (`src/android/kyty_jni.cpp` & `kyty_jni.h`) bridging Android lifecycle, ANativeWindow, and user input to the core.
- Emulation & Graphics Core: FEXCore guest CPU translation, dynamic address space probing, cooperative signal cancellation, Vulkan 1.3 presentation engine, `libadrenotools` linker namespace management, and in-tree FFmpeg 7.x decoders.

### Multi-Layer Design

1. **Native C++ Emulation Core (`libkyty_android.so`):**
   - Compiles the Kyty PS5 translation engine, x86_64 guest CPU emulation (FEXCore), guest memory manager, Vulkan presentation engine, and FFmpeg audio/video decoders.
   - Built with 16 KB page-alignment and non-executable stack flags.
   - Exposes JNI lifecycle, surface, input, and driver management bindings.
   - Stripped using the NDK's `llvm-strip` to reduce the APK binary size.

2. **Dual-Platform Frontend UI (`MetasperoUI-debug.aar`):**
   - Located in `frontend/swift/`.
   - The exact same SwiftUI source code running on the macOS and iOS frontends is transpiled into Kotlin and Jetpack Compose by the [Skip](https://skip.tools) toolchain.
   - Assembled into an Android Archive (`.aar`) library and used by the Gradle build.

3. **Android Application Host Shell (`frontend/android/`):**
   - Built with Gradle.
   - Hosts the shared UI inside Jetpack Compose in `MainActivity`.
   - Hosts the Vulkan `SurfaceView` rendering loop in `EmulationActivity` locked to landscape.
   - Bundles native libraries, custom Adreno GPU driver hooks (`libadrenotools`), on-screen virtual gamepad (`VirtualGamepad`), and Storage Access Framework directory pickers into the final APK.

## 2. Hardware Requirements

### Target Hardware Tier
- **Recommended Minimum Chipset:** Qualcomm Snapdragon 8 Gen 2 (Adreno 740), Snapdragon 8 Gen 3 (Adreno 750), or Snapdragon 8 Elite (Adreno 830).
- **RAM Requirement:** 12 GB LPDDR5X minimum (16 GB recommended). Emulating PS5 unified memory requires mapping substantial physical RAM.
- **Android OS:** Android 12+ (API level 31+) recommended, minimum Android 10 (API level 29).
- **Vulkan Driver:** Mesa Turnip 24.1.0 or newer loaded via `libadrenotools`, or capable system Vulkan 1.3 drivers.

### Memory Architecture & Dynamic Address Space Probing
- **Real-Time HUD Memory Monitoring:** The on-screen performance HUD monitors true process resident physical RAM (`VmRSS` queried via `/proc/self/status`), capturing `mmap`'d guest address space and total system physical RAM.
- **Dynamic 39-bit vs 48-bit VA Probing:** Android kernels vary between 39-bit and 48-bit virtual addressing. Kyty probes address space size at startup via `MAP_FIXED_NOREPLACE` in `src/kernel/memoryAddressSpace.inc`, configuring a 1 TiB ceiling on 48-bit kernels while defaulting safely to 256 GiB on 39-bit kernels to prevent virtual memory allocation failures.

### Vulkan Extension Cut-offs
1. **`VK_KHR_fragment_shader_barycentric`:**
   - **Mesa Turnip Status:** Unsupported
   - **Apple/MoltenVK Status:** Supported via Metal(`supportsShaderBarycentricCoordinates`).

---

## 3. GPU Driver Architecture

### Qualcomm Adreno & Mesa Turnip Focus
For development and test builds, custom GPU driver support focuses strictly on Qualcomm Snapdragon (Adreno 7xx/8xx) devices running Mesa Turnip drivers loaded via `libadrenotools`.

### Stub Libraries (`libhardware.so`, `libsync.so`)
- **Origin:** Derived from upstream Mesa 3D's compilation stubs (`include/android_stub/` and `src/android_stub/`). Mesa's Android Window System Integration (`src/vulkan/wsi/wsi_common_android.c`) historically references `hw_get_module` (HAL Gralloc) and `sync_wait` (sync fences).

## 4. SwiftUI Port to Android (Skip Architecture)

The frontend is written in SwiftUI under `frontend/swift/` and serves both Apple platforms (macOS/iOS) and Android without rewriting the UI:

1. **Transpilation Pipeline:**
   - The Skip toolchain parses the Swift syntax tree and transpiles SwiftUI views into Kotlin and Jetpack Compose composables.
   - Foundation types (`NotificationCenter`, `UserDefaults`, `Bundle`, `FileManager`) are mapped to Android equivalents via SkipFoundation.
   - SwiftUI views (`VStack`, `HStack`, `List`, `Form`, `TabView`, `Image`) are mapped to Jetpack Compose equivalents (`Column`, `Row`, `LazyColumn`, `TabRow`, `Icon`).

2. **Platform Shims (`KytyBridgeShim.swift`):**
   - Implements native bridge callbacks, notification shims, and path helpers.
   - On Android (`#if SKIP`), file access uses raw byte streaming (`readFileData`) and direct POSIX paths (`/storage/emulated/0/...`) to avoid URI encoding errors with spaces.

3. **Packaging (`.aar` Library):**
   - The transpiled code is compiled by Gradle into an Android Archive (`MetasperoUI-debug.aar`) located in `frontend/android/app/libs/`.
   - The host application (`frontend/android`) includes `MetasperoUI` as a project library dependency, launching `ContentView().Compose()` within `MainActivity`.

## 5. Prerequisites & Environment Setup

To build Metaspero for Android ARM64:

1. **Operating System:**
   - macOS (Apple Silicon) or Linux (x86_64 or arm64).

2. **Android SDK & NDK:**
   - Android NDK `r30` (LTS) (NDK `r29` or newer required for libc++ `std::atomic_ref`).
   - Export `ANDROID_NDK_HOME` in your shell environment:
     ```bash
     export ANDROID_NDK_HOME=$HOME/Library/Android/sdk/ndk/30.0.16248370
     ```
   - Android SDK with platform `37.2` and command-line tools (`adb`).

3. **Native Build Tools:**
   - CMake 3.25 or newer (`brew install cmake` or distro package manager).
   - Ninja build tool (`brew install ninja`).
   - Java Development Kit (JDK 17 or newer).

4. **Swift & Skip Toolchain:**
   - Swift 6.0 or newer (on macOS, Xcode 16+ Command Line Tools).
   - Swift Package Manager with [Skip](https://skip.tools) installed:
     ```bash
     brew install skip
     ```

## 6. Building from Source

### Build Artifacts & Output Locations
- `_Build/android-arm64/`: Native CMake configuration, object files, stripped `.so` libraries, and in-tree builds of FFmpeg and FEXCore.
- `frontend/swift/.build/`: Swift Package Manager checkout and Skip plugin cache. Skip stores intermediate transpiled Kotlin source files and `.aar` library archives here.
- `frontend/android/app/libs/`: Stores the assembled `MetasperoUI-debug.aar` and Skip runtime dependencies for Gradle.
- `frontend/android/app/build/`: Android Gradle Plugin build cache, dexed classes, and final output APK (`app-release.apk` for Release builds, `app-debug.apk` for Debug builds).

### Method 1: Automated Build

Run the CMake custom target:
```bash
cmake --preset android-arm64
cmake --build _Build/android-arm64 --target kyty_android_apk
```

This single command automatically:
1. Validates `ANDROID_NDK_HOME` and required build tools.
2. Cross-compiles `libkyty_android.so`, `libhardware.so`, and `libsync.so` with CMake and Ninja.
3. Stages and strips the native `.so` libraries into `frontend/android/app/src/main/jniLibs/arm64-v8a/` using `llvm-strip`.
4. Transpiles and packages the SwiftUI frontend into `MetasperoUI-debug.aar` using the Skip toolchain.
5. Assembles the APK using the Gradle wrapper (`assembleRelease` for Release builds, signed for sideloading, or `assembleDebug` for Debug builds).

The final generated APK is located at:
```
frontend/android/app/build/outputs/apk/release/app-release.apk
```

### Method 2: Manual Step-by-Step Build

#### Step 1: Build the Native Shared Libraries
Configure the Android arm64 preset with CMake:
```bash
cmake --preset android-arm64
```
Build the JNI shared library and stubs:
```bash
ninja -C _Build/android-arm64 kyty_android hardware sync
```

#### Step 2: Stage and Strip Native Binaries
Copy the shared library, stubs, and adreno tools hook libraries to the Android `jniLibs` directory:
```bash
mkdir -p frontend/android/app/src/main/jniLibs/arm64-v8a
cp _Build/android-arm64/src/android/libkyty_android.so frontend/android/app/src/main/jniLibs/arm64-v8a/
cp _Build/android-arm64/libhardware.so frontend/android/app/src/main/jniLibs/arm64-v8a/
cp _Build/android-arm64/libsync.so frontend/android/app/src/main/jniLibs/arm64-v8a/
cp _Build/android-arm64/3rdparty/adrenotools/libadrenotools.so frontend/android/app/src/main/jniLibs/arm64-v8a/
```
Strip debug symbols using the NDK toolchain:
```bash
$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip --strip-unneeded \
  frontend/android/app/src/main/jniLibs/arm64-v8a/*.so
```

#### Step 3: Build the Frontend UI
From the `frontend/swift` directory, transpile and prepare the Android UI package:
```bash
cd frontend/swift
swift build --target MetasperoUI
```

#### Step 4: Assemble the Android APK
Go to `frontend/android` and run Gradle:
```bash
cd frontend/android
./gradlew assembleDebug
```
The resulting APK will be at:
```
frontend/android/app/build/outputs/apk/debug/app-debug.apk
```

## 7. Codebase Implementation Reference

### Build System Files
- `CMakeLists.txt`: Treats Android as Linux, defines `KYTY_PLATFORM_ANDROID`, sets 16 KB page-alignment, force-includes Bionic compatibility headers, configures `-Wl,-Bsymbolic`, builds `hardware` and `sync` stubs, and defines the `kyty_android_apk` target.
- `3rdparty/CMakeLists.txt`: Integrates in-tree FFmpeg compilation via `cmake/FFmpegAndroid.cmake` and configures `cpuinfo` with Clang compatibility.

### Platform Support Files
- `cmake/android/bionic_compat.h`: Polyfills glibc functions absent in Bionic, including cooperative `SIGUSR2` signal-based thread cancellation.
- `cmake/android/hardware_stub.c`: Minimal `hw_get_module` symbols for Turnip driver dynamic linker compatibility.
- `cmake/android/sync_stub.c`: Minimal `sync_wait` and `sync_merge` symbols for Turnip driver dynamic linker compatibility.
- `cmake/FFmpegAndroid.cmake`: Builds FFmpeg 7.1.1 from source for Android AArch64 using the NDK toolchain.
- `src/android/kyty_jni.h` & `src/android/kyty_jni.cpp`: JNI bridge handling lifecycle, `ANativeWindow` surface attachment, custom GPU driver injection, game execution thread, and virtual gamepad input routing.

### Core Engine Adaptations
- `src/common/hostException.cpp`: Signal exception handler for Linux/Android on AArch64, mapping FEX registers and prioritizing GPU fault handling.
- `src/emulator.cpp`: Android sessions shut down in-process and return to the launcher instead of calling `std::quick_exit`, which would kill the whole app.
- `src/loader/loadExec.cpp`: Direct `close_range` system call invocation guarded for Android.
- `src/loader/fex/fexGuestEngine.cpp`: Queries `RuntimeLinker` for executable virtual memory ranges and probes native ARM64 capabilities (`getauxval`).
- `src/kernel/memoryAddressSpace.inc`: Probes kernel virtual address space (1 TiB ceiling on 48-bit kernels, 256 GiB safe ceiling on 39-bit kernels).
- `src/graphics/presentation/window/vulkanWindow.cpp`: Dynamic `vkCreateAndroidSurfaceKHR` surface creation, `VK_KHR_android_surface` enablement, and relaxed mobile feature checks.
- `src/graphics/presentation/window/window.cpp`: Weak hooks `KytyGetHostNativeWindow` and `KytyGetCustomVulkanPath` for driver redirection.
- `src/graphics/presentation/window/swapchain.cpp`: Real-time frame presentation counter exposed for accurate FPS and frame time calculations.

### Frontend Adaptations
- `frontend/android/`: Complete Android application shell including `MainActivity` (Compose library launcher), `EmulationActivity` (`SurfaceView` Vulkan renderer), `VirtualGamepad` (multi-touch on-screen controller), and `GpuDriverManager` (driver extraction and persistence).
- `EmulationActivity` mirrors the iOS in-game overlay with a top-right options menu: hide/show on-screen controls, show/hide the performance HUD, open the live log/diagnostics console, or quit the game. Quitting stops the native session in the background behind a closing overlay, so the app returns to the library instead of exiting.
- `KytyLogs` (`KytyLogs.kt`) and the JNI log pipe (`src/android/kyty_jni.cpp`) mirror the iOS bridge: stdout/stderr are redirected through a pipe into a bounded in-memory buffer shared by the SettingsView diagnostics page and the in-game log console. Logs are not written to disk on Android.
- `frontend/swift/`: Refactored all 12 views into a dual-platform Skip library (`MetasperoUI`). Added `KytyBridgeShim.swift` for cross-platform notification dispatch, raw byte streaming, and POSIX path handling.
