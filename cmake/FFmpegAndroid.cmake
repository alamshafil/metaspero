# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds FFmpeg (static, LGPL) for Android arm64-v8a from the official source release with the NDK
# toolchain and exposes it through FFMPEG_PREBUILT_DIR, which 3rdparty/ffmpeg-core already uses.
#
# ffmpeg-core's Linux prebuilt is linked against glibc (fcntl64, __errno_location, __isoc99_sscanf, ...)
# and cannot be linked against Bionic. The component list mirrors FFmpegiOS.cmake and
# 3rdparty/ffmpeg-core/ffmpeg.patch. The first configure compiles FFmpeg (several minutes), the result
# is cached in the build directory.

include_guard(GLOBAL)

set(KYTY_FFMPEG_ANDROID_VERSION "7.1.1" CACHE STRING "FFmpeg release built for Android")
set(KYTY_FFMPEG_ANDROID_URL
	"https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${KYTY_FFMPEG_ANDROID_VERSION}.tar.gz"
	CACHE STRING "FFmpeg source archive")
set(KYTY_FFMPEG_ANDROID_SHA512
	"6b9a5ee501be41d6abc7579a106263b31f787321cbc45dedee97abf992bf8236cdb2394571dd256a74154f4a20018d429ae7e7f0409611ddc4d6f529d924d175"
	CACHE STRING "SHA512 of the FFmpeg source archive (n7.1.1)")

if(NOT CMAKE_ANDROID_NDK)
	message(FATAL_ERROR "FFmpegAndroid.cmake requires the Android NDK toolchain file")
endif()
# The NDK ships exactly one host toolchain directory (darwin-x86_64, linux-x86_64, windows-x86_64).
file(GLOB _kyty_ndk_hosts LIST_DIRECTORIES TRUE "${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/*")
list(GET _kyty_ndk_hosts 0 _kyty_ndk_host)
set(_kyty_ndk_bin "${_kyty_ndk_host}/bin")
set(_kyty_ndk_sysroot "${_kyty_ndk_host}/sysroot")
set(_kyty_android_api "${ANDROID_PLATFORM_LEVEL}")
if(NOT _kyty_android_api)
	set(_kyty_android_api 29)
endif()
set(_kyty_ffmpeg_cc "${_kyty_ndk_bin}/aarch64-linux-android${_kyty_android_api}-clang")

# Rebuild when the version, API level or this file (the configure options) changes.
file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" _kyty_ffmpeg_script_hash)
set(_kyty_ffmpeg_stamp "${KYTY_FFMPEG_ANDROID_VERSION}|${_kyty_android_api}|${_kyty_ffmpeg_script_hash}")

set(_kyty_ffmpeg_root "${CMAKE_BINARY_DIR}/externals/ffmpeg-android-arm64")
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
	set(_kyty_ffmpeg_archive "${_kyty_ffmpeg_root}/ffmpeg-${KYTY_FFMPEG_ANDROID_VERSION}.tar.gz")
	set(_kyty_ffmpeg_extract "${_kyty_ffmpeg_root}/src")
	set(_kyty_ffmpeg_src "${_kyty_ffmpeg_extract}/FFmpeg-n${KYTY_FFMPEG_ANDROID_VERSION}")
	if(NOT EXISTS "${_kyty_ffmpeg_archive}")
		message(STATUS "Downloading FFmpeg ${KYTY_FFMPEG_ANDROID_VERSION} source...")
		file(DOWNLOAD "${KYTY_FFMPEG_ANDROID_URL}" "${_kyty_ffmpeg_archive}"
			EXPECTED_HASH "SHA512=${KYTY_FFMPEG_ANDROID_SHA512}" SHOW_PROGRESS)
	endif()
	if(NOT EXISTS "${_kyty_ffmpeg_src}/configure")
		file(REMOVE_RECURSE "${_kyty_ffmpeg_extract}")
		file(ARCHIVE_EXTRACT INPUT "${_kyty_ffmpeg_archive}" DESTINATION "${_kyty_ffmpeg_extract}")
	endif()
	if(NOT EXISTS "${_kyty_ffmpeg_src}/configure")
		message(FATAL_ERROR "FFmpeg source not found in ${_kyty_ffmpeg_src}")
	endif()
	cmake_host_system_information(RESULT _kyty_ffmpeg_jobs QUERY NUMBER_OF_LOGICAL_CORES)

	# Same component list as FFmpegiOS.cmake / 3rdparty/ffmpeg-core/ffmpeg.patch.
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
	message(STATUS "Building FFmpeg ${KYTY_FFMPEG_ANDROID_VERSION} for Android arm64 (first configure only, takes a few minutes)...")

	execute_process(
		COMMAND "${_kyty_ffmpeg_src}/configure" --prefix=${_kyty_ffmpeg_prefix}
			--enable-cross-compile --target-os=android --arch=aarch64
			--cc=${_kyty_ffmpeg_cc} --cxx=${_kyty_ffmpeg_cc}++ --ld=${_kyty_ffmpeg_cc}
			--ar=${_kyty_ndk_bin}/llvm-ar --ranlib=${_kyty_ndk_bin}/llvm-ranlib
			--nm=${_kyty_ndk_bin}/llvm-nm --strip=${_kyty_ndk_bin}/llvm-strip
			--sysroot=${_kyty_ndk_sysroot}
			--enable-static --disable-shared --enable-pic
			--disable-programs --disable-doc --disable-debug --disable-autodetect
			--disable-avdevice --disable-jni --disable-mediacodec --enable-runtime-cpudetect
			"--extra-ldflags=-Wl,-z,max-page-size=16384"
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

	file(COPY_FILE "${_kyty_ffmpeg_src}/COPYING.LGPLv2.1" "${_kyty_ffmpeg_prefix}/share/ffmpeg/copyright")
	file(REMOVE_RECURSE "${_kyty_ffmpeg_build}")
	file(WRITE "${_kyty_ffmpeg_stamp_file}" "${_kyty_ffmpeg_stamp}")
endif()

set(FFMPEG_PREBUILT_DIR "${_kyty_ffmpeg_prefix}" CACHE PATH "Directory containing an unpacked FFmpeg package" FORCE)
message(STATUS "FFmpeg ${KYTY_FFMPEG_ANDROID_VERSION} (built from source, Android): ${FFMPEG_PREBUILT_DIR}")
