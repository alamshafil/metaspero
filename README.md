# KytyPS5-Apple

KytyPS5-Apple is an experimental PlayStation 5 emulator and dynamic translation layer for Apple Silicon, with iOS as the primary target platform and support for macOS.

The project is forked from KytyPS5 (which is based on Kyty), adapting the original Windows and Linux x86_64 codebase to run natively on ARM64 Apple Silicon devices. Upstream documentation is kept in `README.upstream.md`.

## Core Architecture

- CPU Translation: Uses a custom fork of FEXCore adapted for the ARM64 Darwin kernel, translating x86_64 instructions into native ARM64 at runtime.
- Graphics: Maps PlayStation 5 Vulkan commands and SPIR-V shaders to Apple Metal using MoltenVK.
- User Interface: Built with SwiftUI for iPhone, iPad, and Mac, featuring touch controls, game library detection, controller support, and live logs.
- JIT Execution: Uses dual-mapped memory, attached on iOS via debug servers such as StikDebug with universal.js over LocalDevVPN.

## iOS Memory Limit Entitlement

By default, the iOS kernel limits individual user apps to roughly 2 GB to 5 GB of RAM through the Jetsam daemon. If an app exceeds this threshold, iOS terminates the process immediately.

Emulating the PlayStation 5 requires allocating large blocks of unified memory for direct physical guest RAM, Vulkan and Metal render surfaces, shader caches, and JIT translation buffers.

To run games without being killed by Jetsam, the app requires the increased memory limit entitlement in src/ios_jit.entitlements:

```xml
<key>com.apple.developer.kernel.increased-memory-limit</key>
<true/>
<key>get-task-allow</key>
<true/>
```

When building and deploying:
- Apple Developer Account: In the Apple Developer portal, ensure your App ID has the Increased Memory Limit capability enabled before generating your provisioning profile.
- Xcode: Link src/ios_jit.entitlements under Signing & Capabilities.
- TrollStore: Automatically provides elevated memory allowances.
- AltStore / SideStore: Ensure your installation setup preserves custom entitlements.

The app also includes a memory warning monitor (frontend/ios/Views/MemoryWarningToastView.swift) that alerts you if the device experiences severe memory pressure during emulation.

## Website and Compatibility Reports

The companion website and game compatibility database live in:
- Local path: /Users/shafil/Documents/Projects/ps5-emu-site
- Repository: https://github.com/shafilalam/kyty-apple

The website maintains a searchable compatibility list for games tested on Apple hardware.

### Submitting Game Reports

You can submit reports through either method:

1. Web Report Tool:
   Visit /report on the companion website, fill out the form, and paste the generated markdown into a new GitHub issue.

2. Pull Request to the Website:
   Add a Markdown file under src/content/compatibility/<game-slug>.md in the website repository using this format:

```markdown
---
title: "Game Title"
titleId: "PPSA01234"
platform: "iOS"
status: "In game"
testedDevice: "iPhone 16 Pro"
osVersion: "iOS 18"
fps: "30 FPS"
jitMethod: "StikDebug (universal.js via LocalDevVPN)"
emulatorVersion: "v0.3.0-alpha"
dateReported: "2026-10-28"
reporter: "YourName"
summary: "Short summary of how the game runs."
notes:
  - "Requires increased memory limit entitlement"
knownIssues:
  - "Any graphical or audio issues observed"
---

Additional test details, logs, or notes go here.
```

Status ratings:
- Playable: Runs without critical issues from start to finish.
- In game: Enters 3D gameplay but has performance issues or crashes.
- Main menu: Reaches title or menu screen but cannot enter gameplay.
- Logo: Shows developer or engine logos and stops.
- Doesn't boot: Crashes during launch or fails to load.

## Directory Layout

- 3rdparty/fex: Custom FEXCore Darwin ARM64 headers and precompiled libraries
- 3rdparty/patches: Build patches for third-party dependencies
- cmake: CMake platform modules and iOS version configuration
- docs: Architecture notes, iOS porting guide, and technical summaries
- frontend/ios: SwiftUI application and Objective-C++ bridge (KytyBridge)
- src/graphics: Vulkan presentation, Metal integration, and GPU processing
- src/kernel: Darwin memory management, threads, and synchronization
- src/libs: High-level emulation libraries (libKernel, libPad, AGC)
- src/loader/fex: FEXCore guest engine and iOS dual-mapping allocator
- README.upstream.md: Original upstream KytyPS5 Windows and Linux README

## Building from Source (Without Helper Scripts)

Prerequisites:
- macOS on Apple Silicon
- Xcode 16 or newer with the iOS SDK
- CMake 3.25+ and Ninja (install via Homebrew: brew install cmake ninja)

### 1. Build FEXCore for iOS ARM64

To compile FEXCore directly with CMake and Ninja:

```bash
# Configure FEXCore
cmake -S _TempData/fex_build/source -B _TempData/fex_build/build_ios \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_FEXCORE_ONLY=ON \
  -DBUILD_FEXCORE_SMOKE=OFF \
  -DTUNE_CPU=none \
  -DTUNE_ARCH=generic \
  -DENABLE_FEX_ALLOCATOR=OFF \
  -DENABLE_ASSERTIONS=OFF \
  -DENABLE_OFFLINE_TELEMETRY=OFF \
  -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF \
  -DENABLE_GDB_SYMBOLS=OFF \
  -DENABLE_LTO=OFF \
  -DBUILD_TESTING=OFF \
  -DBUILD_THUNKS=OFF \
  -DBUILD_FEXCONFIG=OFF \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=$(xcrun --sdk iphoneos --show-sdk-path) \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_CXX_FLAGS="-DMETA_NO_STD_FORWARD_DECLARATIONS"

# Compile libraries
ninja -C _TempData/fex_build/build_ios FEXCore FEXCore_Base JemallocLibs fmt softfloat_3e xxhash cephes_128bit

# Combine into static archive
mkdir -p 3rdparty/fex/lib
libtool -static -o 3rdparty/fex/lib/libFEXCore_device.a \
  _TempData/fex_build/build_ios/FEXCore/Source/libFEXCore.a \
  _TempData/fex_build/build_ios/FEXCore/Source/libFEXCore_Base.a \
  _TempData/fex_build/build_ios/External/fmt/libfmt.a \
  _TempData/fex_build/build_ios/External/SoftFloat-3e/libsoftfloat_3e.a \
  _TempData/fex_build/build_ios/External/xxhash/cmake_unofficial/libxxhash.a
```

### 2. Configure and Build the Project

You can build using CMake presets or standard CMake commands.

Using CMake directly:

```bash
# Configure Xcode project for iOS
cmake -B _Build/xcode -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=$(xcrun --sdk iphoneos --show-sdk-path) \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DKYTY_ENABLE_FEX_GUEST_CPU=ON \
  -DKYTY_BUILD_LAUNCHER=OFF \
  -DBUILD_TESTING=OFF

# Build the release target
cmake --build _Build/xcode --config Release --target KytyPS5 -- \
  CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO
```

Or using the built-in CMake preset:

```bash
# Configure preset
cmake --preset ios

# Build preset
cmake --build --preset ios -- \
  CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO
```

### 3. Package the IPA

Package the compiled application bundle into an IPA file for installation:

```bash
mkdir -p _Build/xcode/Payload
cp -R _Build/xcode/Release-iphoneos/KytyPS5.app _Build/xcode/Payload/
cd _Build/xcode
zip -r -q KytyPS5.ipa Payload
rm -rf Payload
```

### 4. Build for macOS (Optional)

To build the native macOS ARM64 SwiftUI application:

```bash
cmake --preset macos-swiftui
cmake --build --preset macos-swiftui
```

## Running with JIT on iOS

1. Connect LocalDevVPN on your iOS device.
2. Open StikDebug, find KytyPS5 and use the universal.js script.
4. Return to KytyPS5. Once the status shows JIT active, you can launch games from the library.

## Disclaimer

KytyPS5-Apple is an independent research project and is not affiliated with or endorsed by Sony Interactive Entertainment Inc. PlayStation and related marks are trademarks of their respective owners. No proprietary code, BIOS, firmware, or game assets are included.
