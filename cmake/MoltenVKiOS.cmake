# SPDX-License-Identifier: GPL-2.0-or-later
#
# Fetches the official, unmodified MoltenVK iOS release from KhronosGroup (the same upstream the macOS
# build uses for libMoltenVK.dylib) and exposes the device framework as KYTY_MOLTENVK_FRAMEWORK.
# Also exposes KYTY_MOLTENVK_MESH_FRAMEWORK if the experimental mesh shader fork is present.
# Device (ios-arm64) only, the iOS simulator is not supported.

include_guard(GLOBAL)
include(FetchContent)

set(KYTY_MOLTENVK_IOS_VERSION "1.4.2" CACHE STRING "Official MoltenVK release used for iOS")
set(KYTY_MOLTENVK_IOS_URL
	"https://github.com/KhronosGroup/MoltenVK/releases/download/v${KYTY_MOLTENVK_IOS_VERSION}/MoltenVK-ios.tar"
	CACHE STRING "Official MoltenVK iOS release archive")
set(KYTY_MOLTENVK_IOS_SHA256
	"b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1"
	CACHE STRING "SHA256 of the MoltenVK iOS release archive (v1.4.2)")

FetchContent_Declare(moltenvk_ios
	URL "${KYTY_MOLTENVK_IOS_URL}"
	URL_HASH "SHA256=${KYTY_MOLTENVK_IOS_SHA256}"
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(moltenvk_ios)

# The archive nests everything under MoltenVK/MoltenVK/. CMake may strip one level, so search for it.
file(GLOB_RECURSE _kyty_mvk_binary "${moltenvk_ios_SOURCE_DIR}/*/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework/MoltenVK"
	"${moltenvk_ios_SOURCE_DIR}/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework/MoltenVK")
list(GET _kyty_mvk_binary 0 _kyty_mvk_binary)
if(NOT _kyty_mvk_binary)
	message(FATAL_ERROR "MoltenVK.framework (ios-arm64) not found in ${moltenvk_ios_SOURCE_DIR}")
endif()
get_filename_component(KYTY_MOLTENVK_FRAMEWORK "${_kyty_mvk_binary}" DIRECTORY)
message(STATUS "MoltenVK ${KYTY_MOLTENVK_IOS_VERSION} (official, iOS): ${KYTY_MOLTENVK_FRAMEWORK}")

# Mesh shader fork MoltenVK (iOS)
set(KYTY_MOLTENVK_MESH_IOS_ARCHIVE "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/archives/MoltenVK-Mesh-iOS.tar.gz")
set(KYTY_MOLTENVK_MESH_IOS_SHA256 "1995fc0ebfa33cc273a19635cca7a6148ff4c9b194914d0ef0516108f0d8627d")

if(EXISTS "${KYTY_MOLTENVK_MESH_IOS_ARCHIVE}")
	FetchContent_Declare(moltenvk_mesh_ios
		URL "${KYTY_MOLTENVK_MESH_IOS_ARCHIVE}"
		URL_HASH "SHA256=${KYTY_MOLTENVK_MESH_IOS_SHA256}"
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
	FetchContent_MakeAvailable(moltenvk_mesh_ios)

	set(KYTY_MOLTENVK_MESH_FRAMEWORK "${moltenvk_mesh_ios_SOURCE_DIR}/dynamic/MoltenVK-Mesh.xcframework/ios-arm64/MoltenVK-Mesh.framework")
	message(STATUS "MoltenVK Mesh Shader Fork (iOS): ${KYTY_MOLTENVK_MESH_FRAMEWORK}")
endif()
