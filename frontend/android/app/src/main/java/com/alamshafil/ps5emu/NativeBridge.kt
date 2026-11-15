// SPDX-License-Identifier: GPL-2.0-or-later
package com.alamshafil.ps5emu

import android.view.Surface

object NativeBridge {
    init {
        try {
            System.loadLibrary("c++_shared")
        } catch (_: UnsatisfiedLinkError) {}
        System.loadLibrary("kyty_android")
    }

    const val BUTTON_CROSS = 0
    const val BUTTON_CIRCLE = 1
    const val BUTTON_SQUARE = 2
    const val BUTTON_TRIANGLE = 3
    const val BUTTON_UP = 4
    const val BUTTON_DOWN = 5
    const val BUTTON_LEFT = 6
    const val BUTTON_RIGHT = 7
    const val BUTTON_L1 = 8
    const val BUTTON_R1 = 9
    const val BUTTON_L2 = 10
    const val BUTTON_R2 = 11
    const val BUTTON_L3 = 12
    const val BUTTON_R3 = 13
    const val BUTTON_OPTIONS = 14
    const val BUTTON_TOUCH_PAD = 15
    const val BUTTON_SHARE = 16
    const val BUTTON_PS = 17

    const val STICK_LEFT = 0
    const val STICK_RIGHT = 1

    const val TRIGGER_L2 = 0
    const val TRIGGER_R2 = 1

    @JvmStatic
    external fun nativeInit(filesDir: String, cacheDir: String)

    @JvmStatic
    external fun nativeSetCustomDriver(driverDir: String?, driverName: String?, hookDir: String?): Boolean

    @JvmStatic
    external fun nativeSetSurface(surface: Surface)

    @JvmStatic
    external fun nativeDestroySurface()

    @JvmStatic
    external fun nativeSurfaceChanged(width: Int, height: Int)

    @JvmStatic
    external fun nativeStartGame(gamePath: String, resolution: String, presentMode: String): Boolean

    @JvmStatic
    external fun nativeStopGame()

    @JvmStatic
    external fun nativeIsRunning(): Boolean

    @JvmStatic
    external fun nativeGetVersion(): String

    @JvmStatic
    external fun nativeSendButton(button: Int, pressed: Boolean)

    @JvmStatic
    external fun nativeSendStick(stick: Int, x: Float, y: Float)

    @JvmStatic
    external fun nativeSendTrigger(trigger: Int, value: Float)

    @JvmStatic
    external fun nativeSendTouch(finger: Int, down: Boolean, x: Float, y: Float)

    @JvmStatic
    external fun nativeGetFps(): Float

    @JvmStatic
    external fun nativeGetFrameTimeMs(): Float
}
