#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Generates the Native macOS SwiftUI Xcode project and opens it.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$REPO_DIR"
echo "==> Generating KytyPS5 Native macOS (SwiftUI + FEX) Xcode project..."
cmake --preset macos-swiftui

echo "==> Opening KytyEmulatorCLANG.xcodeproj in Xcode..."
open "$REPO_DIR/_Build/xcode-mac/KytyEmulatorCLANG.xcodeproj"

echo "==> Done! Destination is set to 'My Mac'. Press Cmd+R in Xcode to run."
