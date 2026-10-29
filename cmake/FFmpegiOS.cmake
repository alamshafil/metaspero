# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds FFmpeg (static, LGPL) for iOS devices (arm64) from the official source release and exposes the
# result through FFMPEG_PREBUILT_DIR, which 3rdparty/ffmpeg-core already uses for the other platforms.
#
# ffmpeg-core only publishes macOS prebuilts for APPLE, and those cannot be linked into an iOS binary.
# The component list mirrors 3rdparty/ffmpeg-core/ffmpeg.patch, so the same decoders are available
# everywhere (only the build driver differs: vcpkg is not used here). The first configure compiles FFmpeg (several minutes), the result is cached
# in the build directory.
# Device (arm64) only, the iOS simulator is not supported.

include_guard(GLOBAL)

# Same FFmpeg release that the vcpkg port used by ffmpeg-core builds (source hash from that port).
set(KYTY_FFMPEG_IOS_VERSION "7.1.1" CACHE STRING "FFmpeg release built for iOS")
set(KYTY_FFMPEG_IOS_URL
	"https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${KYTY_FFMPEG_IOS_VERSION}.tar.gz"
	CACHE STRING "FFmpeg source archive")
set(KYTY_FFMPEG_IOS_SHA512
	"6b9a5ee501be41d6abc7579a106263b31f787321cbc45dedee97abf992bf8236cdb2394571dd256a74154f4a20018d429ae7e7f0409611ddc4d6f529d924d175"
	CACHE STRING "SHA512 of the FFmpeg source archive (n7.1.1)")

file(READ "${CMAKE_CURRENT_LIST_DIR}/ios_min_version.txt" _kyty_ios_min)
string(STRIP "${_kyty_ios_min}" _kyty_ios_min)

# Rebuild when the version, deployment target or this file (the configure options) changes.
file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" _kyty_ffmpeg_script_hash)
set(_kyty_ffmpeg_stamp "${KYTY_FFMPEG_IOS_VERSION}|${_kyty_ios_min}|${_kyty_ffmpeg_script_hash}")

set(_kyty_ffmpeg_root "${CMAKE_BINARY_DIR}/externals/ffmpeg-ios-arm64")
set(_kyty_ffmpeg_prefix "${_kyty_ffmpeg_root}/install")
set(_kyty_ffmpeg_stamp_file "${_kyty_ffmpeg_root}/build.stamp")

set(_kyty_ffmpeg_up_to_date FALSE)
if(EXISTS "${_kyty_ffmpeg_stamp_file}" AND EXISTS "${_kyty_ffmpeg_prefix}/lib/libavcodec.a")
	file(READ "${_kyty_ffmpeg_stamp_file}" _kyty_ffmpeg_old_stamp)
	if(_kyty_ffmpeg_old_stamp STREQUAL _kyty_ffmpeg_stamp)
		set(_kyty_ffmpeg_up_to_date TRUE)
	endif()
endif()

if(NOT _kyty_ffmpeg_up_to_date)
	set(_kyty_ffmpeg_archive "${_kyty_ffmpeg_root}/ffmpeg-${KYTY_FFMPEG_IOS_VERSION}.tar.gz")
	set(_kyty_ffmpeg_extract "${_kyty_ffmpeg_root}/src")
	# GitHub archives unpack into FFmpeg-n<version>/.
	set(_kyty_ffmpeg_src "${_kyty_ffmpeg_extract}/FFmpeg-n${KYTY_FFMPEG_IOS_VERSION}")
	if(NOT EXISTS "${_kyty_ffmpeg_archive}")
		message(STATUS "Downloading FFmpeg ${KYTY_FFMPEG_IOS_VERSION} source...")
		file(DOWNLOAD "${KYTY_FFMPEG_IOS_URL}" "${_kyty_ffmpeg_archive}"
			EXPECTED_HASH "SHA512=${KYTY_FFMPEG_IOS_SHA512}" SHOW_PROGRESS)
	endif()
	if(NOT EXISTS "${_kyty_ffmpeg_src}/configure")
		file(REMOVE_RECURSE "${_kyty_ffmpeg_extract}")
		file(ARCHIVE_EXTRACT INPUT "${_kyty_ffmpeg_archive}" DESTINATION "${_kyty_ffmpeg_extract}")
	endif()
	if(NOT EXISTS "${_kyty_ffmpeg_src}/configure")
		message(FATAL_ERROR "FFmpeg source not found in ${_kyty_ffmpeg_src}")
	endif()

	execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-path
		OUTPUT_VARIABLE _kyty_ffmpeg_sdk OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
	execute_process(COMMAND xcrun --sdk iphoneos --find clang
		OUTPUT_VARIABLE _kyty_ffmpeg_cc OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
	set(_kyty_ffmpeg_flags "-arch arm64 -isysroot ${_kyty_ffmpeg_sdk} -miphoneos-version-min=${_kyty_ios_min}")
	cmake_host_system_information(RESULT _kyty_ffmpeg_jobs QUERY NUMBER_OF_LOGICAL_CORES)

	# Same component list as 3rdparty/ffmpeg-core/ffmpeg.patch (minus the Windows-only dshow indev).
	set(_kyty_ffmpeg_components
		--enable-decoder=aac --enable-decoder=aac_latm --enable-decoder=atrac3 --enable-decoder=atrac3p
		--enable-decoder=atrac9 --enable-decoder=mp3 --enable-decoder=pcm_s16le --enable-decoder=pcm_s8
		--enable-decoder=mov --enable-decoder=h264 --enable-decoder=mpeg4 --enable-decoder=mpeg2video
		--enable-decoder=mjpeg --enable-decoder=mjpegb --enable-decoder=hevc --enable-decoder=vp8
		--enable-decoder=vp9 --enable-decoder=opus --enable-decoder=vorbis
		--enable-encoder=pcm_s16le --enable-encoder=ffv1 --enable-encoder=mpeg4 --enable-encoder=ljpeg
		--enable-encoder=mjpeg
		--enable-muxer=avi
		--enable-demuxer=h265 --enable-demuxer=h264 --enable-demuxer=m4v --enable-demuxer=mp3
		--enable-demuxer=mpegvideo --enable-demuxer=mpegps --enable-demuxer=mjpeg --enable-demuxer=mov
		--enable-demuxer=avi --enable-demuxer=aac --enable-demuxer=pmp --enable-demuxer=oma
		--enable-demuxer=pcm_s16le --enable-demuxer=pcm_s8 --enable-demuxer=wav --enable-demuxer=matroska
		--enable-parser=h264 --enable-parser=mpeg4video --enable-parser=mpegaudio --enable-parser=mpegvideo
		--enable-parser=mjpeg --enable-parser=aac --enable-parser=aac_latm --enable-parser=vp8
		--enable-parser=vp9 --enable-parser=opus --enable-parser=vorbis
		--enable-protocol=file
		--enable-bsf=mjpeg2jpeg)

	set(_kyty_ffmpeg_build "${_kyty_ffmpeg_root}/build")
	file(REMOVE_RECURSE "${_kyty_ffmpeg_build}" "${_kyty_ffmpeg_prefix}")
	file(MAKE_DIRECTORY "${_kyty_ffmpeg_build}")
	set(_kyty_ffmpeg_log "${_kyty_ffmpeg_root}/build.log")
	message(STATUS "Building FFmpeg ${KYTY_FFMPEG_IOS_VERSION} for iOS arm64 (first configure only, takes a few minutes)...")

	execute_process(
		COMMAND "${_kyty_ffmpeg_src}/configure" --prefix=${_kyty_ffmpeg_prefix}
			--enable-cross-compile --target-os=darwin --arch=arm64
			--cc=${_kyty_ffmpeg_cc} --as=${_kyty_ffmpeg_cc} --ld=${_kyty_ffmpeg_cc}
			"--extra-cflags=${_kyty_ffmpeg_flags}" "--extra-ldflags=${_kyty_ffmpeg_flags}"
			--enable-static --disable-shared --enable-pic
			--disable-programs --disable-doc --disable-debug --disable-autodetect
			--disable-avdevice --enable-runtime-cpudetect
			--enable-videotoolbox --enable-audiotoolbox
			--disable-everything ${_kyty_ffmpeg_components}
		WORKING_DIRECTORY "${_kyty_ffmpeg_build}"
		RESULT_VARIABLE _kyty_ffmpeg_result OUTPUT_FILE "${_kyty_ffmpeg_log}" ERROR_FILE "${_kyty_ffmpeg_log}")
	if(NOT _kyty_ffmpeg_result EQUAL 0)
		message(FATAL_ERROR "FFmpeg configure failed (${_kyty_ffmpeg_result}); see ${_kyty_ffmpeg_log}")
	endif()
	foreach(_kyty_ffmpeg_target IN ITEMS all install)
		execute_process(
			COMMAND make -j${_kyty_ffmpeg_jobs} ${_kyty_ffmpeg_target}
			WORKING_DIRECTORY "${_kyty_ffmpeg_build}"
			RESULT_VARIABLE _kyty_ffmpeg_result OUTPUT_QUIET ERROR_FILE "${_kyty_ffmpeg_log}")
		if(NOT _kyty_ffmpeg_result EQUAL 0)
			message(FATAL_ERROR "FFmpeg make ${_kyty_ffmpeg_target} failed (${_kyty_ffmpeg_result}); see ${_kyty_ffmpeg_log}")
		endif()
	endforeach()

	# Same layout as the prebuilt packages: include/, lib/ and share/ffmpeg/ (license text).
	file(COPY_FILE "${_kyty_ffmpeg_src}/COPYING.LGPLv2.1" "${_kyty_ffmpeg_prefix}/share/ffmpeg/copyright")
	file(REMOVE_RECURSE "${_kyty_ffmpeg_build}")
	file(WRITE "${_kyty_ffmpeg_stamp_file}" "${_kyty_ffmpeg_stamp}")
endif()

set(FFMPEG_PREBUILT_DIR "${_kyty_ffmpeg_prefix}" CACHE PATH "Directory containing an unpacked FFmpeg package" FORCE)
message(STATUS "FFmpeg ${KYTY_FFMPEG_IOS_VERSION} (built from source, iOS): ${FFMPEG_PREBUILT_DIR}")
