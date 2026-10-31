# Version Bumping and Release Packaging

This document describes how to update the version number and build release packages for iOS and macOS.

## 1. Version Bumping

The application version and build numbers are set in two files:

### frontend/ios/CMakeLists.txt

Update the default version cache variables:

```cmake
set(KYTY_APP_VERSION "0.1.0" CACHE STRING "SwiftUI app version")
set(KYTY_APP_BUILD "1" CACHE STRING "SwiftUI app build number")
```

### frontend/ios/Info.plist

Update the version strings to match:

```xml
<key>CFBundleShortVersionString</key>
<string>0.1.0</string>
<key>CFBundleVersion</key>
<string>1</string>
```

### Upstream Base Commit Tracking

When syncing changes from upstream KytyPS5, update the base commit hash displayed in the UI in `frontend/ios/Views/AboutView.swift`:

```swift
static let upstreamCommit = "df5b9b9"
```

## 2. iOS Release Build and Packaging

Configure and compile the release binary:

```bash
cmake --preset ios
cmake --build --preset ios --config Release --target KytyPS5 -- \
  CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO
```

Package the app bundle into an IPA file:

```bash
mkdir -p _Build/xcode/Payload
cp -R _Build/xcode/frontend/ios/Release-iphoneos/KytyPS5.app _Build/xcode/Payload/
cd _Build/xcode
zip -r -q KytyPS5-iOS-arm64.ipa Payload
rm -rf Payload
```

Output: `_Build/xcode/KytyPS5-iOS-arm64.ipa`

This package can be installed directly with TrollStore, AltStore, or SideStore.

## 3. macOS Release Build and Packaging

Configure and build the native macOS application:

```bash
cmake --preset macos-swiftui -DCMAKE_BUILD_TYPE=Release
cmake --build --preset macos-swiftui --config Release --target KytyPS5
```

Package the application bundle into a zip archive:

```bash
cd _Build/xcode-mac/frontend/ios/Release
zip -r -q KytyPS5-macOS-arm64.zip KytyPS5.app
```

Output: `_Build/xcode-mac/frontend/ios/Release/KytyPS5-macOS-arm64.zip`
