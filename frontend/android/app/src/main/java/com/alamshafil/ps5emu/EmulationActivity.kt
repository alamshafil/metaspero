// SPDX-License-Identifier: GPL-2.0-or-later
package com.alamshafil.ps5emu

import android.app.Activity
import android.app.ActivityManager
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.os.Debug
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.TextView
import java.util.Locale

class EmulationActivity : Activity(), SurfaceHolder.Callback {

    private lateinit var surfaceView: SurfaceView
    private lateinit var virtualGamepad: VirtualGamepad
    private lateinit var tvStatsOverlay: TextView
    private var gamePath: String = ""
    private var resolution: String = "720p"
    private var presentMode: String = "Mailbox"

    private val statsHandler = Handler(Looper.getMainLooper())
    private var statsOverlayMode = 0 // 0: full, 1: minimal, 2: hidden

    private val statsRunnable = object : Runnable {
        override fun run() {
            updateStatsOverlay()
            statsHandler.postDelayed(this, 500)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        org.libsdl.app.SDL.setContext(this)

        // Ensure native bridge and working directory are initialized
        NativeBridge.nativeInit(filesDir.absolutePath, cacheDir.absolutePath)

        // Apply selected GPU driver (Turnip or System Default)
        val savedDriverId = GpuDriverManager.getSelectedDriverId(this)
        val drivers = GpuDriverManager.getInstalledDrivers(this)
        val currentDriver = drivers.find { it.id == savedDriverId } ?: GpuDriverManager.DEFAULT_DRIVER
        GpuDriverManager.applyDriver(this, currentDriver)

        // Keep screen on during gameplay
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        gamePath = intent.getStringExtra("EXTRA_GAME_PATH") ?: ""
        resolution = intent.getStringExtra("EXTRA_RESOLUTION") ?: "720p"
        presentMode = intent.getStringExtra("EXTRA_PRESENT_MODE") ?: "Mailbox"

        val rootLayout = FrameLayout(this).apply {
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
            setBackgroundColor(Color.BLACK)
        }

        surfaceView = SurfaceView(this).apply {
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
            holder.addCallback(this@EmulationActivity)
        }
        rootLayout.addView(surfaceView)

        // Read user preferences for touch gamepad and HUD
        val prefs = getSharedPreferences("defaults", Context.MODE_PRIVATE)
        val enableTouchControls = prefs.getBoolean("enableTouchOverlay", true)
        val enableHud = prefs.getBoolean("enableMetalHud", false)
        val hudDetailLevel = prefs.getString("hudDetailLevel", "Full") ?: "Full"

        // Virtual On-Screen Gamepad
        virtualGamepad = VirtualGamepad(this).apply {
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
            visibility = if (enableTouchControls) View.VISIBLE else View.GONE
        }
        rootLayout.addView(virtualGamepad)

        // Performance & FPS HUD Overlay
        statsOverlayMode = when {
            !enableHud -> 2 // hidden
            hudDetailLevel == "Minimal" -> 1 // minimal
            else -> 0 // full
        }

        val density = resources.displayMetrics.density
        fun dp(v: Float) = (v * density).toInt()

        tvStatsOverlay = TextView(this).apply {
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT
            ).apply {
                gravity = Gravity.TOP or Gravity.START
                setMargins(dp(16f), dp(16f), 0, 0)
            }
            setPadding(dp(10f), dp(6f), dp(10f), dp(6f))
            typeface = Typeface.MONOSPACE
            textSize = 11f
            setTextColor(Color.WHITE)

            val bg = GradientDrawable().apply {
                setColor(Color.argb(175, 12, 16, 24))
                cornerRadius = 14f * density
                setStroke((1f * density).toInt(), Color.argb(60, 255, 255, 255))
            }
            background = bg
            visibility = if (statsOverlayMode != 2) View.VISIBLE else View.GONE

            setOnClickListener {
                statsOverlayMode = (statsOverlayMode + 1) % 3
                visibility = if (statsOverlayMode != 2) View.VISIBLE else View.GONE
            }
        }
        rootLayout.addView(tvStatsOverlay)

        // In-game Exit Button (floating top-right)
        val btnExit = TextView(this).apply {
            text = "✕"
            textSize = 16f
            setTextColor(Color.WHITE)
            gravity = Gravity.CENTER
            layoutParams = FrameLayout.LayoutParams(dp(36f), dp(36f)).apply {
                gravity = Gravity.TOP or Gravity.END
                setMargins(0, dp(14f), dp(16f), 0)
            }
            val exitBg = GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(Color.argb(140, 30, 30, 30))
                setStroke((1f * density).toInt(), Color.argb(80, 255, 255, 255))
            }
            background = exitBg
            setOnClickListener {
                com.google.android.material.dialog.MaterialAlertDialogBuilder(this@EmulationActivity)
                    .setTitle("Exit Game")
                    .setMessage("Are you sure you want to stop emulation?")
                    .setPositiveButton("Exit") { _, _ ->
                        NativeBridge.nativeStopGame()
                        finish()
                    }
                    .setNegativeButton("Cancel", null)
                    .show()
            }
        }
        rootLayout.addView(btnExit)

        setContentView(rootLayout)
        hideSystemUI()
    }

    override fun onResume() {
        super.onResume()
        hideSystemUI()
        statsHandler.post(statsRunnable)
    }

    override fun onPause() {
        super.onPause()
        statsHandler.removeCallbacks(statsRunnable)
    }

    private fun updateStatsOverlay() {
        if (statsOverlayMode == 2) return

        val fps = NativeBridge.nativeGetFps()
        val ramMb = getAppMemoryUsageMb()

        val text = when (statsOverlayMode) {
            1 -> {
                // Minimal: FPS and RAM
                String.format(Locale.US, "%.1f FPS  |  %d MB", fps, ramMb)
            }
            else -> {
                // Full HUD: Engine, Resolution, Present Mode, FPS, Memory
                val driverName = GpuDriverManager.getInstalledDrivers(this)
                    .find { it.id == GpuDriverManager.getSelectedDriverId(this) }?.name ?: "Vulkan"
                String.format(
                    Locale.US,
                    "GPU: %s\nRes: %s  |  Mode: %s\nFPS: %.1f  (%.2f ms)\nRAM: %d MB",
                    driverName,
                    resolution,
                    presentMode,
                    fps,
                    if (fps > 0) 1000.0 / fps else 0.0,
                    ramMb
                )
            }
        }
        tvStatsOverlay.text = text
    }

    private fun getAppMemoryUsageMb(): Long {
        return try {
            val memInfo = Debug.MemoryInfo()
            Debug.getMemoryInfo(memInfo)
            memInfo.totalPss.toLong() / 1024
        } catch (_: Exception) {
            val rt = Runtime.getRuntime()
            (rt.totalMemory() - rt.freeMemory()) / (1024 * 1024)
        }
    }

    private fun hideSystemUI() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                window.setDecorFitsSystemWindows(false)
                val controller = window.insetsController
                controller?.let {
                    it.hide(WindowInsets.Type.statusBars() or WindowInsets.Type.navigationBars())
                    it.systemBarsBehavior =
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                }
            } else {
                @Suppress("DEPRECATION")
                window.decorView.systemUiVisibility = (
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        or View.SYSTEM_UI_FLAG_FULLSCREEN
                        or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                )
            }
        } catch (_: Exception) {}
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        NativeBridge.nativeSetSurface(holder.surface)
        if (gamePath.isNotEmpty()) {
            NativeBridge.nativeStartGame(gamePath, resolution, presentMode)
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        NativeBridge.nativeSurfaceChanged(width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        NativeBridge.nativeStopGame()
        NativeBridge.nativeDestroySurface()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        NativeBridge.nativeStopGame()
        super.onBackPressed()
    }

    // Physical Gamepad Input Handling (Bluetooth / USB / Type-C)

    private fun mapKeyCodeToButton(keyCode: Int): Int {
        return when (keyCode) {
            KeyEvent.KEYCODE_BUTTON_A -> NativeBridge.BUTTON_CROSS
            KeyEvent.KEYCODE_BUTTON_B -> NativeBridge.BUTTON_CIRCLE
            KeyEvent.KEYCODE_BUTTON_X -> NativeBridge.BUTTON_SQUARE
            KeyEvent.KEYCODE_BUTTON_Y -> NativeBridge.BUTTON_TRIANGLE
            KeyEvent.KEYCODE_DPAD_UP -> NativeBridge.BUTTON_UP
            KeyEvent.KEYCODE_DPAD_DOWN -> NativeBridge.BUTTON_DOWN
            KeyEvent.KEYCODE_DPAD_LEFT -> NativeBridge.BUTTON_LEFT
            KeyEvent.KEYCODE_DPAD_RIGHT -> NativeBridge.BUTTON_RIGHT
            KeyEvent.KEYCODE_BUTTON_L1 -> NativeBridge.BUTTON_L1
            KeyEvent.KEYCODE_BUTTON_R1 -> NativeBridge.BUTTON_R1
            KeyEvent.KEYCODE_BUTTON_L2 -> NativeBridge.BUTTON_L2
            KeyEvent.KEYCODE_BUTTON_R2 -> NativeBridge.BUTTON_R2
            KeyEvent.KEYCODE_BUTTON_THUMBL -> NativeBridge.BUTTON_L3
            KeyEvent.KEYCODE_BUTTON_THUMBR -> NativeBridge.BUTTON_R3
            KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_MENU -> NativeBridge.BUTTON_OPTIONS
            KeyEvent.KEYCODE_BUTTON_SELECT -> NativeBridge.BUTTON_TOUCH_PAD
            else -> -1
        }
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (event.source and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD ||
            event.source and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK) {
            val btn = mapKeyCodeToButton(keyCode)
            if (btn != -1) {
                NativeBridge.nativeSendButton(btn, true)
                return true
            }
        }
        return super.onKeyDown(keyCode, event)
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (event.source and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD ||
            event.source and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK) {
            val btn = mapKeyCodeToButton(keyCode)
            if (btn != -1) {
                NativeBridge.nativeSendButton(btn, false)
                return true
            }
        }
        return super.onKeyUp(keyCode, event)
    }

    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        if (event.source and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK) {
            // Left Stick
            val lx = event.getAxisValue(MotionEvent.AXIS_X)
            val ly = event.getAxisValue(MotionEvent.AXIS_Y)
            NativeBridge.nativeSendStick(NativeBridge.STICK_LEFT, lx, -ly)

            // Right Stick
            val rx = event.getAxisValue(MotionEvent.AXIS_Z).let { if (it == 0f) event.getAxisValue(MotionEvent.AXIS_RX) else it }
            val ry = event.getAxisValue(MotionEvent.AXIS_RZ).let { if (it == 0f) event.getAxisValue(MotionEvent.AXIS_RY) else it }
            NativeBridge.nativeSendStick(NativeBridge.STICK_RIGHT, rx, -ry)

            // Triggers (L2 / R2)
            val l2 = event.getAxisValue(MotionEvent.AXIS_LTRIGGER).let { if (it == 0f) event.getAxisValue(MotionEvent.AXIS_BRAKE) else it }
            val r2 = event.getAxisValue(MotionEvent.AXIS_RTRIGGER).let { if (it == 0f) event.getAxisValue(MotionEvent.AXIS_GAS) else it }
            if (l2 > 0.05f) {
                NativeBridge.nativeSendTrigger(NativeBridge.TRIGGER_L2, l2)
                NativeBridge.nativeSendButton(NativeBridge.BUTTON_L2, l2 > 0.5f)
            }
            if (r2 > 0.05f) {
                NativeBridge.nativeSendTrigger(NativeBridge.TRIGGER_R2, r2)
                NativeBridge.nativeSendButton(NativeBridge.BUTTON_R2, r2 > 0.5f)
            }

            // D-Pad from Hat axes (some controllers report D-pad as HAT axes)
            val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
            val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
            if (hatX != 0f || hatY != 0f) {
                NativeBridge.nativeSendButton(NativeBridge.BUTTON_LEFT, hatX < -0.5f)
                NativeBridge.nativeSendButton(NativeBridge.BUTTON_RIGHT, hatX > 0.5f)
                NativeBridge.nativeSendButton(NativeBridge.BUTTON_UP, hatY < -0.5f)
                NativeBridge.nativeSendButton(NativeBridge.BUTTON_DOWN, hatY > 0.5f)
            }

            return true
        }
        return super.onGenericMotionEvent(event)
    }
}
