#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Generates the iOS Xcode project from the CMake "ios" preset and opens it.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

[ -f "${SCRIPT_DIR}/ios_local.env" ] && source "${SCRIPT_DIR}/ios_local.env"

CONFIGURE_ARGS=()
[ -n "${KYTY_IOS_TEAM_ID:-}" ]   && CONFIGURE_ARGS+=("-DKYTY_IOS_TEAM_ID=${KYTY_IOS_TEAM_ID}")
[ -n "${KYTY_IOS_BUNDLE_ID:-}" ] && CONFIGURE_ARGS+=("-DKYTY_IOS_BUNDLE_ID=${KYTY_IOS_BUNDLE_ID}")

cd "$REPO_DIR"
echo "==> Generating KytyPS5 iOS Xcode project..."
cmake --preset ios "${CONFIGURE_ARGS[@]}"

echo "==> Opening Kyty.xcodeproj in Xcode..."
open "$REPO_DIR/_Build/xcode/Kyty.xcodeproj"

echo "==> Done! Select your device and hit Run (Cmd+R) in Xcode."
