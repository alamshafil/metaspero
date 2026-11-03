set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set_property(DIRECTORY PROPERTY FOLDER "3rdparty")
set(CMAKE_FOLDER "3rdparty")

add_subdirectory(cpuinfo)

include(FetchContent)

# The Bluetooth DualSense transports speaker audio as fixed-size Opus packets.
set(kyty_build_testing_saved "${BUILD_TESTING}")
set(BUILD_TESTING OFF)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
if(MSVC AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
	# Opus assumes MSVC accepts SSE4.1 intrinsics without per-file flags, but
	# clang-cl rejects them in its baseline x64 files. Its AVX2 dispatch also
	# depends on the SSE4.1 declarations, so retain the x64 SSE2 baseline only.
	set(OPUS_X86_MAY_HAVE_SSE4_1 OFF CACHE BOOL "" FORCE)
	set(OPUS_X86_MAY_HAVE_AVX2 OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(opus
	GIT_REPOSITORY https://github.com/xiph/opus.git
	GIT_TAG ddbe48383984d56acd9e1ab6a090c54ca6b735a6
	GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(opus)
set(BUILD_TESTING "${kyty_build_testing_saved}")
install(FILES "${opus_SOURCE_DIR}/COPYING" DESTINATION "licenses/opus")

# The guest instruction patcher needs a decoder/encoder and a small
# trampoline assembler. Pin the same revisions used by shadPS4's implementation.
set(XBYAK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	xbyak
	GIT_REPOSITORY https://github.com/herumi/xbyak.git
	GIT_TAG 44a72f369268f7d552650891b296693e91db86bb
	GIT_SHALLOW FALSE
)

set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	zydis
	GIT_REPOSITORY https://github.com/zyantific/zydis.git
	GIT_TAG 120e0e705f8e3b507dc49377ac2879979f0d545c
	GIT_SUBMODULES_RECURSE TRUE
	GIT_SHALLOW FALSE
)

FetchContent_MakeAvailable(xbyak zydis)
target_compile_definitions(xbyak INTERFACE XBYAK_NO_EXCEPTION XBYAK64 XBYAK64_GCC XBYAK_DONT_USE_MMAP_ALLOCATOR)

# ZArchive provides random-access reads for compressed game dumps. Build only
# the reader/writer library and its zstd dependency; the command-line tool is
# not part of KytyPS5's runtime distribution.
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	zstd
	GIT_REPOSITORY https://github.com/facebook/zstd.git
	GIT_TAG f8745da6ff1ad1e7bab384bd1f9d742439278e99
	GIT_SHALLOW FALSE
	SOURCE_SUBDIR build/cmake
)
FetchContent_Declare(
	zarchive_source
	GIT_REPOSITORY https://github.com/Exzap/ZArchive.git
	GIT_TAG d2c717730092c7bf8cbb033b12fd4001b7c4d932
	GIT_SHALLOW FALSE
	PATCH_COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/zarchive-reader.patch"
	SOURCE_SUBDIR kyty-library-only
)
FetchContent_MakeAvailable(zstd zarchive_source)

add_library(zarchive STATIC
	"${zarchive_source_SOURCE_DIR}/src/zarchivereader.cpp"
	"${zarchive_source_SOURCE_DIR}/src/zarchivewriter.cpp"
	"${zarchive_source_SOURCE_DIR}/src/sha_256.c"
)
add_library(ZArchive::zarchive ALIAS zarchive)
target_include_directories(zarchive SYSTEM PUBLIC "${zarchive_source_SOURCE_DIR}/include")
target_include_directories(zarchive PRIVATE "${zstd_SOURCE_DIR}/lib")
target_link_libraries(zarchive PRIVATE libzstd_static)
target_compile_features(zarchive PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(zarchive PRIVATE /W0)
else()
	target_compile_options(zarchive PRIVATE -w)
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
if(APPLE)
	# SDL's built-in iconv fallback does not support CP932.
	set(SDL_SYSTEM_ICONV ON CACHE BOOL "Use system iconv for CP932 conversion" FORCE)
endif()
add_subdirectory(SDL3)

add_library(Vulkan-Headers INTERFACE)
add_library(Vulkan::Headers ALIAS Vulkan-Headers)
target_include_directories(Vulkan-Headers INTERFACE
	"${CMAKE_CURRENT_SOURCE_DIR}/Vulkan-Headers/include"
)

add_library(imgui STATIC
	imgui/imgui.cpp
	imgui/imgui_draw.cpp
	imgui/imgui_tables.cpp
	imgui/imgui_widgets.cpp
	imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends"
)
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_link_libraries(imgui PUBLIC Vulkan::Headers)
if(MSVC)
	target_compile_options(imgui PRIVATE /W0)
else()
	target_compile_options(imgui PRIVATE -w)
endif()

set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
set(SPIRV-Headers_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SPIRV-Headers" CACHE PATH "" FORCE)
add_subdirectory(SPIRV-Tools)

add_library(spirv-tools ALIAS SPIRV-Tools-static)
add_library(spirv-tools-opt ALIAS SPIRV-Tools-opt)

set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "" FORCE)
add_subdirectory(xxHash/build/cmake)

set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_MODULE OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(fmt EXCLUDE_FROM_ALL)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
add_subdirectory(nlohmann_json EXCLUDE_FROM_ALL)

set(SPDLOG_BUILD_ALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_WARNINGS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
set(SPDLOG_NO_EXCEPTIONS ON CACHE BOOL "" FORCE)
if(WIN32)
	set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(spdlog EXCLUDE_FROM_ALL)

if (NOT TARGET FFmpeg::ffmpeg)
	# On macOS the target arch is driven by CMAKE_OSX_ARCHITECTURES, not the host
	# CMAKE_SYSTEM_PROCESSOR (which reports arm64 even for an x86_64/Rosetta build).
	if(APPLE AND CMAKE_OSX_ARCHITECTURES)
		if(CMAKE_OSX_ARCHITECTURES MATCHES "^(x86_64)$")
			set(ARCHITECTURE x86_64)
		elseif(CMAKE_OSX_ARCHITECTURES MATCHES "^(arm64)$")
			set(ARCHITECTURE arm64)
		else()
			set(ARCHITECTURE x86_64 arm64) # universal
		endif()
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
		set(ARCHITECTURE arm64)
	else()
		set(ARCHITECTURE x86_64)
	endif()
	if(IOS)
		# ffmpeg-core has no iOS package. Build FFmpeg from source and hand it over as a prebuilt dir.
		include(${CMAKE_CURRENT_LIST_DIR}/../cmake/FFmpegiOS.cmake)
	endif()
	add_subdirectory(ffmpeg-core)
	if(IOS)
		# ffmpeg-core links macOS-only frameworks (CoreServices, AudioUnit) for APPLE, use the iOS set.
		set_property(TARGET ffmpeg PROPERTY INTERFACE_LINK_LIBRARIES "")
		target_link_libraries(ffmpeg INTERFACE
			"-framework CoreFoundation" "-framework AudioToolbox" "-framework CoreAudio"
			"-framework CoreMedia" "-framework VideoToolbox" "-framework CoreVideo" "-framework Security"
			"${FFMPEG_PREBUILT_DIR}/lib/libavformat.a" "${FFMPEG_PREBUILT_DIR}/lib/libavcodec.a"
			"${FFMPEG_PREBUILT_DIR}/lib/libswscale.a" "${FFMPEG_PREBUILT_DIR}/lib/libavutil.a"
			"${FFMPEG_PREBUILT_DIR}/lib/libavfilter.a" "${FFMPEG_PREBUILT_DIR}/lib/libswresample.a"
			z iconv)
	endif()
	add_library(FFmpeg::ffmpeg ALIAS ffmpeg)
endif()

file(GLOB LIBATRAC9_SOURCES LibAtrac9/C/src/*.c)
add_library(LibAtrac9 STATIC ${LIBATRAC9_SOURCES})
target_include_directories(LibAtrac9 PUBLIC LibAtrac9/C/src)
if(MSVC)
	target_compile_options(LibAtrac9 PRIVATE /W0)
endif()

set(TRACY_STATIC ON CACHE BOOL "" FORCE)
set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
set(TRACY_NO_CRASH_HANDLER ON CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
set(TRACY_DELAYED_INIT ON CACHE BOOL "" FORCE)
set(TRACY_MANUAL_LIFETIME ON CACHE BOOL "" FORCE)
add_subdirectory(tracy EXCLUDE_FROM_ALL)
if (TARGET TracyClient)
	if (MSVC)
		target_compile_options(TracyClient PRIVATE /WX-)
	endif()
	if (CLANG)
		target_compile_options(TracyClient PRIVATE
			-Wno-error
			-Wno-braced-scalar-init
			-Wno-format
			-Wno-unused-variable
		)
	endif()
endif()

if (CLANG AND MSVC)
add_subdirectory(winpthread)
endif()

if(KYTY_ENABLE_FEX_GUEST_CPU)
	if(NOT CMAKE_SYSTEM_PROCESSOR AND CMAKE_OSX_ARCHITECTURES)
		set(CMAKE_SYSTEM_PROCESSOR "${CMAKE_OSX_ARCHITECTURES}" CACHE STRING "" FORCE)
	elseif(NOT CMAKE_SYSTEM_PROCESSOR AND (IOS OR APPLE))
		set(CMAKE_SYSTEM_PROCESSOR "arm64" CACHE STRING "" FORCE)
	endif()

	set(BUILD_FEXCORE_ONLY ON CACHE BOOL "" FORCE)
	set(BUILD_FEXCORE_SMOKE OFF CACHE BOOL "" FORCE)
	set(TUNE_CPU "none" CACHE STRING "" FORCE)
	set(TUNE_ARCH "generic" CACHE STRING "" FORCE)
	set(ENABLE_FEX_ALLOCATOR OFF CACHE BOOL "" FORCE)
	set(ENABLE_ASSERTIONS OFF CACHE BOOL "" FORCE)
	set(ENABLE_OFFLINE_TELEMETRY OFF CACHE BOOL "" FORCE)
	set(ENABLE_JEMALLOC_GLIBC_ALLOC OFF CACHE BOOL "" FORCE)
	set(ENABLE_GDB_SYMBOLS OFF CACHE BOOL "" FORCE)
	set(ENABLE_LTO OFF CACHE BOOL "" FORCE)
	set(BUILD_THUNKS OFF CACHE BOOL "" FORCE)
	set(BUILD_FEXCONFIG OFF CACHE BOOL "" FORCE)

	list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/fexcore/Data/CMake")

	add_subdirectory(fexcore EXCLUDE_FROM_ALL)

	target_compile_options(FEXCore_Base PRIVATE -frtti)
	target_compile_options(FEXCore_object PRIVATE -frtti)

	target_link_libraries(FEXCore PUBLIC
		FEXCore_Base
		JemallocLibs
		cephes_128bit
		softfloat_3e
	)
	target_include_directories(FEXCore INTERFACE
		"${CMAKE_CURRENT_SOURCE_DIR}/fexcore/FEXCore/include"
		"${CMAKE_CURRENT_SOURCE_DIR}/fexcore/FEXHeaderUtils"
		"${CMAKE_CURRENT_SOURCE_DIR}/fexcore/External/unordered_dense/include"
		"${CMAKE_CURRENT_BINARY_DIR}/fexcore/include"
	)
	target_compile_definitions(FEXCore INTERFACE
		META_NO_STD_FORWARD_DECLARATIONS
	)
endif()

if(TARGET clean-all)
	set_target_properties(clean-all PROPERTIES FOLDER "CMake")
endif()
if(TARGET uninstall)
	set_target_properties(uninstall PROPERTIES FOLDER "CMake")
endif()

