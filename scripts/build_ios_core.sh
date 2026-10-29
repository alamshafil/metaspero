#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Thin wrapper around the CMake "ios" preset: configure, build the SwiftUI app (which builds kyty_core and all
# dependencies), and package an unsigned IPA. Safe to call from automation; all real logic lives in CMake.
#
# Usage: scripts/build_ios_core.sh [--debug] [--no-ipa]
# Optional signing config: KYTY_IOS_TEAM_ID / KYTY_IOS_BUNDLE_ID (env or scripts/ios_local.env, which is untracked).

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

[ -f "${SCRIPT_DIR}/ios_local.env" ] && source "${SCRIPT_DIR}/ios_local.env"

CONFIG=Release
BUILD_PRESET=ios
MAKE_IPA=1
for arg in "$@"; do
    case "${arg}" in
        --debug)  CONFIG=Debug; BUILD_PRESET=ios-debug ;;
        --no-ipa) MAKE_IPA=0 ;;
        *) echo "Unknown option: ${arg}" >&2; exit 2 ;;
    esac
done

CONFIGURE_ARGS=()
[ -n "${KYTY_IOS_TEAM_ID:-}" ]   && CONFIGURE_ARGS+=("-DKYTY_IOS_TEAM_ID=${KYTY_IOS_TEAM_ID}")
[ -n "${KYTY_IOS_BUNDLE_ID:-}" ] && CONFIGURE_ARGS+=("-DKYTY_IOS_BUNDLE_ID=${KYTY_IOS_BUNDLE_ID}")

cd "${REPO_ROOT}"

echo "==> [1/3] Configuring (preset: ios)"
cmake --preset ios "${CONFIGURE_ARGS[@]}"

echo "==> [2/3] Building KytyPS5 (${CONFIG})"
cmake --build --preset "${BUILD_PRESET}" -- \
    CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO

if [ "${MAKE_IPA}" = "1" ]; then
    echo "==> [3/3] Packaging IPA"
    BUILD_DIR="${REPO_ROOT}/_Build/xcode"
    rm -rf "${BUILD_DIR}/Payload"
    mkdir -p "${BUILD_DIR}/Payload"
    APP_SRC="${BUILD_DIR}/frontend/ios/${CONFIG}-iphoneos/KytyPS5.app"
    [ ! -d "${APP_SRC}" ] && APP_SRC="${BUILD_DIR}/${CONFIG}-iphoneos/KytyPS5.app"
    cp -R "${APP_SRC}" "${BUILD_DIR}/Payload/"
    rm -f "${BUILD_DIR}/KytyPS5.ipa"
    (cd "${BUILD_DIR}" && zip -r -q KytyPS5.ipa Payload)
    rm -rf "${BUILD_DIR}/Payload"
    echo "Generated: ${BUILD_DIR}/KytyPS5.ipa"
fi
