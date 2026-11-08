// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef KYTY_ANDROID_JNI_H_
#define KYTY_ANDROID_JNI_H_

#if defined(__ANDROID__)

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

void*       KytyGetHostNativeWindow(void);
void        KytySetHostNativeWindow(void* window);
void*       KytyGetVkGetInstanceProcAddr(void);
const char* KytyGetCustomVulkanPath(void);

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeInit(
    JNIEnv* env, jclass clazz, jstring files_dir, jstring cache_dir);

JNIEXPORT jboolean JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeSetCustomDriver(
    JNIEnv* env, jclass clazz, jstring driver_dir, jstring driver_name, jstring hook_dir);

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeSetSurface(
    JNIEnv* env, jclass clazz, jobject surface);

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeDestroySurface(
    JNIEnv* env, jclass clazz);

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeSurfaceChanged(
    JNIEnv* env, jclass clazz, jint width, jint height);

JNIEXPORT jboolean JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeStartGame(
    JNIEnv* env, jclass clazz, jstring game_path, jstring resolution, jstring present_mode);

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeStopGame(
    JNIEnv* env, jclass clazz);

JNIEXPORT jboolean JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeIsRunning(
    JNIEnv* env, jclass clazz);

JNIEXPORT jstring JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeGetVersion(
    JNIEnv* env, jclass clazz);

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeSendButton(
    JNIEnv* env, jclass clazz, jint button, jboolean pressed);

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeSendStick(
    JNIEnv* env, jclass clazz, jint stick, jfloat x, jfloat y);

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeSendTrigger(
    JNIEnv* env, jclass clazz, jint trigger, jfloat value);

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeSendTouch(
    JNIEnv* env, jclass clazz, jint finger, jboolean down, jfloat x, jfloat y);

#ifdef __cplusplus
}
#endif

#endif // defined(__ANDROID__)
#endif // KYTY_ANDROID_JNI_H_
