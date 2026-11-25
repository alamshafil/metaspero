# Changelog

## Metaspero v0.2.0

WARNING: This is an early-stage experimental build. Expect crashes, visual glitches, and low frame rates.

Metaspero is an experimental PlayStation 5 emulator and dynamic translation layer for ARM64 (iOS, macOS Apple Silicon, and Android).

### What's New
- Added Android ARM64 support (uses custom Adreno/Turnip drivers)
- Merged 600+ commits from upstream KytyPS5 (df5b9b9 to 58ca8d1)
- FEXCore fixes allowing Unreal Engine 4 (UE4) games and Astro's Playroom to run
- Added MetalFX Spatial upscaling, render scale, and 4K scaling on macOS and iOS
- Added MoltenVK Mesh Shader fork support on macOS and iOS
- Redesigned touch controls for iOS and Android
- Added keyboard input, live performance overlay, in-game log console, and updated Settings
- Lower memory usage on mobile, fixed video flickering, and cleaner game exit/relaunch

### Performance
- Major CPU, GPU, and shader speedups on Apple Silicon
- Relaxed Shader Float is on by default on Apple Silicon (can be turned off in Graphics Settings if you see glitches)
- Shader compilation will still stutter on first run, and 3D games still need lots of RAM (12-16 GB recommended)

### Links
- Changes: https://github.com/alamshafil/metaspero/compare/v0.1.0...v0.2.0
- Upstream KytyPS5 merge: https://github.com/KytyPS5/KytyPS5/compare/df5b9b9...58ca8d1

Compatibility Reports:
Submit reports to https://github.com/alamshafil/metaspero-site and paste the output into a GitHub issue, or submit a pull request.

This is an independent research project not affiliated with Sony. No copyrighted BIOS, firmware, or game assets are included.

## Metaspero v0.1.0

WARNING: This is an early-stage experimental build. Expect crashes, visual glitches, and low frame rates.

Metaspero is an experimental PlayStation 5 emulator and dynamic translation layer for Apple Silicon (iOS and native macOS ARM64).

Compatibility Reports:
Submit reports to https://github.com/alamshafil/metaspero-site and paste the output into a GitHub issue, or submit a pull request.

This is an independent research project not affiliated with Sony. No copyrighted BIOS, firmware, or game assets are included.
