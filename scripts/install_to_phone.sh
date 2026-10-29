#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Automatically signs KytyPS5.app with your local developer profile and installs it via Xcode devicectl.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

APP="${REPO_ROOT}/_Build/xcode/frontend/ios/Release-iphoneos/KytyPS5.app"
if [ ! -d "${APP}" ]; then
    APP="${REPO_ROOT}/_Build/xcode/frontend/ios/Debug-iphoneos/KytyPS5.app"
fi
if [ ! -d "${APP}" ]; then
    APP="${REPO_ROOT}/_Build/xcode/Release-iphoneos/KytyPS5.app"
fi
if [ ! -d "${APP}" ]; then
    APP="${REPO_ROOT}/_Build/xcode/Debug-iphoneos/KytyPS5.app"
fi
if [ ! -d "${APP}" ]; then
    echo "Error: KytyPS5.app not found. Please run ./scripts/build_ios_core.sh first."
    exit 1
fi

PROFILE_DIR="${HOME}/Library/Developer/Xcode/UserData/Provisioning Profiles"
PROFILE=$(find "${PROFILE_DIR}" -name "*.mobileprovision" 2>/dev/null | head -n 1)

if [ -z "${PROFILE}" ]; then
    echo "Error: No provisioning profile found in ${PROFILE_DIR}."
    exit 1
fi

# Signing identity/team come from the environment or the untracked scripts/ios_local.env.
[ -f "${SCRIPT_DIR}/ios_local.env" ] && source "${SCRIPT_DIR}/ios_local.env"
SIGN_ID="${KYTY_IOS_SIGN_ID:?Set KYTY_IOS_SIGN_ID (e.g. \"Apple Development: Name (XXXXXXXXXX)\") or add it to scripts/ios_local.env}"
TEAM_ID="${KYTY_IOS_TEAM_ID:?Set KYTY_IOS_TEAM_ID or add it to scripts/ios_local.env}"
BUNDLE_ID="${KYTY_IOS_BUNDLE_ID:-com.alamshafil.ps5emu}"

echo "=========================================================="
echo " [1/3] Embedding profile: $(basename "${PROFILE}")..."
echo "=========================================================="
cp "${PROFILE}" "${APP}/embedded.mobileprovision"

ENTITLEMENTS_TMP=$(mktemp /tmp/kyty_entitlements.XXXXXX.plist)
cat << EOF > "${ENTITLEMENTS_TMP}"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>application-identifier</key>
	<string>${TEAM_ID}.${BUNDLE_ID}</string>
	<key>com.apple.developer.team-identifier</key>
	<string>${TEAM_ID}</string>
	<key>get-task-allow</key>
	<true/>
	<key>keychain-access-groups</key>
	<array>
		<string>${TEAM_ID}.${BUNDLE_ID}</string>
		<string>com.apple.token</string>
	</array>
</dict>
</plist>
EOF

echo "=========================================================="
echo " [2/3] Codesigning framework and app bundle..."
echo "=========================================================="
if [ -d "${APP}/Frameworks/MoltenVK.framework" ]; then
    codesign -f -s "${SIGN_ID}" --timestamp=none "${APP}/Frameworks/MoltenVK.framework"
fi
codesign -f -s "${SIGN_ID}" --timestamp=none --entitlements "${ENTITLEMENTS_TMP}" "${APP}"
rm -f "${ENTITLEMENTS_TMP}"

echo "=========================================================="
echo " [3/3] Detecting connected iPhone and installing via devicectl..."
echo "=========================================================="
DEVICE_ID=$(xcrun devicectl list devices | grep -E "iPhone|available" | awk '{print $3}' | head -n 1)

if [ -z "${DEVICE_ID}" ]; then
    echo "Warning: No available iPhone found via devicectl. Make sure your phone is connected and unlocked."
    exit 1
fi

echo "Installing on device: ${DEVICE_ID}..."
xcrun devicectl device install app --device "${DEVICE_ID}" "${APP}"

echo "=========================================================="
echo " Successfully installed KytyPS5 to your iPhone!"
echo "=========================================================="
