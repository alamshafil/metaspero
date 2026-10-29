# SPDX-License-Identifier: GPL-2.0-or-later
#
# Fetches the official MoltenVK macOS release from KhronosGroup and exposes:
# - KYTY_MOLTENVK_FRAMEWORK: path to MoltenVK.framework (macos-arm64_x86_64)
# - KYTY_MOLTENVK_DYLIB: path to libMoltenVK.dylib (macOS universal)

include_guard(GLOBAL)
include(FetchContent)

set(KYTY_MOLTENVK_MACOS_VERSION "1.4.2" CACHE STRING "Official MoltenVK release used for macOS")
set(KYTY_MOLTENVK_MACOS_URL
	"https://github.com/KhronosGroup/MoltenVK/releases/download/v${KYTY_MOLTENVK_MACOS_VERSION}/MoltenVK-macos.tar"
	CACHE STRING "Official MoltenVK macOS release archive")
set(KYTY_MOLTENVK_MACOS_SHA256
	"f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e"
	CACHE STRING "SHA256 of the MoltenVK macOS release archive (v1.4.2)")

FetchContent_Declare(moltenvk_macos
	URL "${KYTY_MOLTENVK_MACOS_URL}"
	URL_HASH "SHA256=${KYTY_MOLTENVK_MACOS_SHA256}"
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(moltenvk_macos)

file(GLOB_RECURSE _kyty_mvk_mac_binary
	"${moltenvk_macos_SOURCE_DIR}/*/dynamic/MoltenVK.xcframework/macos-arm64_x86_64/MoltenVK.framework/MoltenVK"
	"${moltenvk_macos_SOURCE_DIR}/dynamic/MoltenVK.xcframework/macos-arm64_x86_64/MoltenVK.framework/MoltenVK"
	"${moltenvk_macos_SOURCE_DIR}/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/macos-arm64_x86_64/MoltenVK.framework/MoltenVK"
)
list(GET _kyty_mvk_mac_binary 0 _kyty_mvk_mac_binary)
if(NOT _kyty_mvk_mac_binary)
	message(FATAL_ERROR "MoltenVK.framework (macos-arm64_x86_64) not found in ${moltenvk_macos_SOURCE_DIR}")
endif()
get_filename_component(KYTY_MOLTENVK_FRAMEWORK "${_kyty_mvk_mac_binary}" DIRECTORY)

file(GLOB_RECURSE _kyty_mvk_dylib
	"${moltenvk_macos_SOURCE_DIR}/*/dynamic/dylib/macOS/libMoltenVK.dylib"
	"${moltenvk_macos_SOURCE_DIR}/dynamic/dylib/macOS/libMoltenVK.dylib"
	"${moltenvk_macos_SOURCE_DIR}/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"
)
list(GET _kyty_mvk_dylib 0 KYTY_MOLTENVK_DYLIB)

message(STATUS "MoltenVK ${KYTY_MOLTENVK_MACOS_VERSION} (official, macOS): ${KYTY_MOLTENVK_FRAMEWORK}")
