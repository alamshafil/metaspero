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
import android.os.Process
import android.os.SystemClock
import android.text.Spannable
import android.text.SpannableStringBuilder
import android.text.style.ForegroundColorSpan
import android.view.Display
import android.view.Gravity
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.PopupMenu
import android.widget.ScrollView
import android.widget.TextView
import java.io.File
import java.util.Locale

class EmulationActivity : Activity(), SurfaceHolder.Callback {

    private lateinit var surfaceView: SurfaceView
    private lateinit var virtualGamepad: VirtualGamepad
    private lateinit var tvStatsOverlay: TextView
    private lateinit var btnMenu: TextView
    private lateinit var logOverlay: FrameLayout
    private lateinit var logScrollView: ScrollView
    private lateinit var logTextView: TextView
    private lateinit var closingOverlay: FrameLayout
    private var gamePath: String = ""
    private var resolution: String = "720p"
    private var presentMode: String = "Mailbox"
    private var skipMeshShaders: Boolean = false
    private var enableSrtArm64Jit: Boolean = false
    private var perfOverlayEnabled: Boolean = false
    private var renderScalePercent: Int = 100
    private var relaxedShaderFloat: Boolean = false
    private var enableTouchControls: Boolean = false
    private var isStopping: Boolean = false
    private var lastVisibleStatsMode: Int = 0

    private val density: Float get() = resources.displayMetrics.density
    private fun dp(v: Float): Int = (v * density).toInt()

    private val statsHandler = Handler(Looper.getMainLooper())
    private var statsOverlayMode = 0 // 0: full, 1: minimal, 2: hidden

    private val statsRunnable = object : Runnable {
        override fun run() {
            updateStatsOverlay()
            statsHandler.postDelayed(this, 500)
        }
    }

    private val logRefreshRunnable = object : Runnable {
        override fun run() {
            refreshLogOverlay()
            statsHandler.postDelayed(this, 1000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Ensure native bridge and working directory are initialized
        NativeBridge.nativeInit(filesDir.absolutePath, cacheDir.absolutePath)

        org.libsdl.app.SDL.setupJNI()
        org.libsdl.app.SDL.initialize()
        org.libsdl.app.SDL.setContext(this)

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
        enableTouchControls = prefs.getBoolean("enableTouchOverlay", true)
        val enableHud = prefs.getBoolean("enableMetalHud", false)
        val hudDetailLevel = prefs.getString("hudDetailLevel", "Full") ?: "Full"
        skipMeshShaders = prefs.getBoolean("skipMeshShaders", false)
        enableSrtArm64Jit = prefs.getBoolean("enableSrtArm64Jit", false)
        perfOverlayEnabled = prefs.getBoolean("perfOverlayEnabled", false)
        renderScalePercent = prefs.getInt("renderScalePercent", 100).coerceIn(25, 100)
        relaxedShaderFloat = prefs.getBoolean("relaxedShaderFloat", false)

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
        lastVisibleStatsMode = if (statsOverlayMode == 2) 0 else statsOverlayMode

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
                if (statsOverlayMode != 2) {
                    lastVisibleStatsMode = statsOverlayMode
                }
                visibility = if (statsOverlayMode != 2) View.VISIBLE else View.GONE
            }
        }
        rootLayout.addView(tvStatsOverlay)

        // In-game options menu
        btnMenu = TextView(this).apply {
            text = "⋮"
            textSize = 22f
            setTextColor(Color.WHITE)
            gravity = Gravity.CENTER
            contentDescription = "Game options"
            layoutParams = FrameLayout.LayoutParams(dp(34f), dp(34f)).apply {
                gravity = Gravity.TOP or Gravity.END
                setMargins(0, dp(6f), dp(8f), 0)
            }
            val menuBg = GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(Color.argb(140, 30, 30, 30))
                setStroke((1f * density).toInt(), Color.argb(80, 255, 255, 255))
            }
            background = menuBg
            setOnClickListener { showOptionsMenu(this) }
        }
        rootLayout.addView(btnMenu)

        setupLogOverlay(rootLayout)
        setupClosingOverlay(rootLayout)

        setContentView(rootLayout)
        hideSystemUI()
    }

    override fun onResume() {
        super.onResume()
        hideSystemUI()
        statsHandler.post(statsRunnable)
        if (::logOverlay.isInitialized && logOverlay.visibility == View.VISIBLE) {
            statsHandler.post(logRefreshRunnable)
        }
    }

    override fun onPause() {
        super.onPause()
        statsHandler.removeCallbacks(statsRunnable)
        statsHandler.removeCallbacks(logRefreshRunnable)
    }

    private var lastCpuTimeMs: Long = 0L
    private var lastWallTimeMs: Long = 0L
    private val numCpuCores = Runtime.getRuntime().availableProcessors().coerceAtLeast(1)

    private val gpuBusyFiles = arrayOf(
        "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage",
        "/sys/class/kgsl/kgsl-3d0/gpubusy",
        "/sys/class/misc/mali0/device/utilization",
        "/sys/kernel/gpu/gpu_busy",
        "/proc/mali/utilization"
    )

    private fun getCpuUsagePercent(): Float {
        val currentCpuTime = Process.getElapsedCpuTime()
        val currentWallTime = SystemClock.elapsedRealtime()

        if (lastWallTimeMs == 0L) {
            lastCpuTimeMs = currentCpuTime
            lastWallTimeMs = currentWallTime
            return 0f
        }

        val deltaCpu = currentCpuTime - lastCpuTimeMs
        val deltaWall = currentWallTime - lastWallTimeMs

        lastCpuTimeMs = currentCpuTime
        lastWallTimeMs = currentWallTime

        if (deltaWall <= 0) return 0f

        val usage = (deltaCpu.toFloat() / (deltaWall * numCpuCores)) * 100f
        return usage.coerceIn(0f, 100f)
    }

    private fun getGpuUsagePercent(fps: Float): Float {
        for (path in gpuBusyFiles) {
            try {
                val file = File(path)
                if (file.exists() && file.canRead()) {
                    val text = file.readText().trim()
                    if (path.endsWith("gpubusy")) {
                        val parts = text.split("\\s+".toRegex())
                        if (parts.size >= 2) {
                            val busy = parts[0].toLongOrNull() ?: 0L
                            val total = parts[1].toLongOrNull() ?: 1L
                            if (total > 0) {
                                return ((busy.toFloat() / total) * 100f).coerceIn(0f, 100f)
                            }
                        }
                    } else {
                        val clean = text.replace("%", "").trim()
                        val value = clean.toFloatOrNull()
                        if (value != null) {
                            val pct = if (value > 100f) (value / 255f) * 100f else value
                            return pct.coerceIn(0f, 100f)
                        }
                    }
                }
            } catch (_: Exception) {}
        }

        // Fallback calculation: GPU load relative to 60 FPS frame time budget (16.67ms)
        val frameTimeMs = if (fps > 0.01f) 1000f / fps else NativeBridge.nativeGetFrameTimeMs()
        if (frameTimeMs > 0f) {
            val targetBudgetMs = 1000f / 60f
            return ((frameTimeMs / targetBudgetMs) * 100f).coerceIn(0f, 100f)
        }
        return 0f
    }

    private fun updateStatsOverlay() {
        if (statsOverlayMode == 2) return

        val fps = NativeBridge.nativeGetFps()
        val ramMb = getAppMemoryUsageMb()
        val cpuPct = getCpuUsagePercent()
        val gpuPct = getGpuUsagePercent(fps)

        val text = when (statsOverlayMode) {
            1 -> {
                // Minimal: FPS, CPU, GPU, RAM
                String.format(
                    Locale.US,
                    "%.1f FPS  |  CPU: %.0f%%  |  GPU: %.0f%%  |  %d MB",
                    fps, cpuPct, gpuPct, ramMb
                )
            }
            else -> {
                // Full HUD: Driver, FPS (Frame Time), CPU %, GPU %, Memory
                val driverName = GpuDriverManager.getInstalledDrivers(this)
                    .find { it.id == GpuDriverManager.getSelectedDriverId(this) }?.name ?: "Vulkan"
                String.format(
                    Locale.US,
                    "GPU: %s\nFPS: %.1f  (%.2f ms)\nCPU: %.0f%%  |  GPU: %.0f%%\nRAM: %d MB",
                    driverName,
                    fps,
                    if (fps > 0) 1000.0 / fps else 0.0,
                    cpuPct,
                    gpuPct,
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

    // In-game options & diagnostics

    private fun showOptionsMenu(anchor: View) {
        val popup = PopupMenu(this, anchor)

        if (enableTouchControls) {
            val label = if (virtualGamepad.visibility == View.VISIBLE) {
                "Hide On-Screen Controls"
            } else {
                "Show On-Screen Controls"
            }
            popup.menu.add(0, MENU_TOGGLE_CONTROLS, 0, label)
        }

        popup.menu.add(0, MENU_LOGS, 1, "Logs & Diagnostics")
        popup.menu.add(
            0,
            MENU_TOGGLE_HUD,
            2,
            if (statsOverlayMode == 2) "Show Performance HUD" else "Hide Performance HUD"
        )
        popup.menu.add(0, MENU_EXIT, 3, "Quit Game")

        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                MENU_TOGGLE_CONTROLS -> toggleTouchControls()
                MENU_LOGS -> showLogOverlay()
                MENU_TOGGLE_HUD -> toggleStatsOverlay()
                MENU_EXIT -> confirmExit()
            }
            true
        }
        popup.show()
    }

    private fun toggleTouchControls() {
        if (virtualGamepad.visibility == View.VISIBLE) {
            virtualGamepad.releaseAllInputs()
            virtualGamepad.visibility = View.GONE
        } else {
            virtualGamepad.visibility = View.VISIBLE
        }
    }

    private fun toggleStatsOverlay() {
        if (statsOverlayMode == 2) {
            statsOverlayMode = lastVisibleStatsMode
        } else {
            lastVisibleStatsMode = statsOverlayMode
            statsOverlayMode = 2
        }
        tvStatsOverlay.visibility = if (statsOverlayMode != 2) View.VISIBLE else View.GONE
    }

    private fun confirmExit() {
        if (isStopping) {
            return
        }
        com.google.android.material.dialog.MaterialAlertDialogBuilder(this)
            .setTitle("Exit Game")
            .setMessage("Are you sure you want to stop emulation?")
            .setPositiveButton("Exit") { _, _ -> stopAndFinish() }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun stopAndFinish() {
        if (isStopping) {
            return
        }
        isStopping = true
        hideLogOverlay()
        btnMenu.isEnabled = false
        closingOverlay.visibility = View.VISIBLE
        closingOverlay.bringToFront()

        Thread {
            try {
                NativeBridge.nativeStopGame()
            } catch (_: Throwable) {}
            runOnUiThread {
                if (!isFinishing && !isDestroyed) {
                    finish()
                }
            }
        }.start()
    }

    private fun setupLogOverlay(root: FrameLayout) {
        logOverlay = FrameLayout(this).apply {
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
            setBackgroundColor(Color.argb(242, 6, 8, 12))
            visibility = View.GONE
            isClickable = true
            isFocusable = true
        }

        val panel = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16f), dp(10f), dp(16f), dp(12f))
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        }

        fun overlayButton(label: String, sizeSp: Float, onClick: () -> Unit): TextView {
            return TextView(this).apply {
                text = label
                textSize = sizeSp
                setTextColor(Color.WHITE)
                gravity = Gravity.CENTER
                setPadding(dp(10f), dp(6f), dp(10f), dp(6f))
                setOnClickListener { onClick() }
            }
        }

        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }

        val title = TextView(this).apply {
            text = "Logs & Diagnostics"
            textSize = 16f
            setTextColor(Color.WHITE)
            setTypeface(typeface, Typeface.BOLD)
        }
        header.addView(
            title,
            LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        )
        header.addView(overlayButton("Clear", 13f) {
            KytyLogs.clear()
            refreshLogOverlay()
        })
        header.addView(overlayButton("✕", 18f) { hideLogOverlay() })

        panel.addView(
            header,
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )
        )

        logTextView = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 10f
            setTextColor(Color.rgb(235, 235, 245))
            setTextIsSelectable(true)
            setPadding(0, dp(8f), 0, dp(8f))
        }

        logScrollView = ScrollView(this).apply {
            isFillViewport = true
            addView(
                logTextView,
                FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.WRAP_CONTENT
                )
            )
        }
        panel.addView(
            logScrollView,
            LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
        )

        val footer = TextView(this).apply {
            text = "Live in-memory log. Auto-refreshing every second"
            textSize = 10f
            setTextColor(Color.argb(150, 255, 255, 255))
        }
        panel.addView(footer)

        logOverlay.addView(panel)
        root.addView(logOverlay)
    }

    private fun setupClosingOverlay(root: FrameLayout) {
        closingOverlay = FrameLayout(this).apply {
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
            setBackgroundColor(Color.argb(220, 0, 0, 0))
            visibility = View.GONE
            isClickable = true
        }

        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.CENTER
            )
        }
        content.addView(TextView(this).apply {
            text = "Closing Game..."
            textSize = 18f
            setTextColor(Color.WHITE)
            setTypeface(typeface, Typeface.BOLD)
            gravity = Gravity.CENTER
        })
        content.addView(TextView(this).apply {
            text = "Releasing GPU & system memory"
            textSize = 13f
            setTextColor(Color.argb(180, 255, 255, 255))
            gravity = Gravity.CENTER
            setPadding(0, dp(6f), 0, 0)
        })
        closingOverlay.addView(content)
        root.addView(closingOverlay)
    }

    private fun showLogOverlay() {
        logOverlay.visibility = View.VISIBLE
        logOverlay.bringToFront()
        refreshLogOverlay()
        statsHandler.removeCallbacks(logRefreshRunnable)
        statsHandler.post(logRefreshRunnable)
    }

    private fun hideLogOverlay() {
        if (!::logOverlay.isInitialized) {
            return
        }
        statsHandler.removeCallbacks(logRefreshRunnable)
        logOverlay.visibility = View.GONE
    }

    private fun refreshLogOverlay() {
        if (!::logTextView.isInitialized) {
            return
        }

        val lines = KytyLogs.recentLogLines()
        val wasAtBottom = !logScrollView.canScrollVertically(1)

        if (lines.isEmpty()) {
            logTextView.text = "No log entries available."
            return
        }

        val builder = SpannableStringBuilder()
        for (line in lines) {
            val start = builder.length
            builder.append(line).append('\n')
            builder.setSpan(
                ForegroundColorSpan(colorForLogLine(line)),
                start,
                builder.length,
                Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
            )
        }
        logTextView.text = builder

        if (wasAtBottom) {
            logScrollView.post { logScrollView.fullScroll(View.FOCUS_DOWN) }
        }
    }

    private fun colorForLogLine(line: String): Int {
        val lower = line.lowercase(Locale.US)
        return when {
            lower.contains("crash") || lower.contains("fatal") ||
                lower.contains("sigsegv") || lower.contains("sigbus") -> Color.rgb(255, 69, 58)
            lower.contains("error") || lower.contains("fail") -> Color.rgb(255, 99, 88)
            lower.contains("warn") -> Color.rgb(255, 159, 10)
            lower.contains("jit") || lower.contains("success") || lower.contains("ready") ->
                Color.rgb(48, 209, 88)
            lower.contains("fex") || lower.contains("rip=") -> Color.rgb(191, 90, 242)
            else -> Color.rgb(235, 235, 245)
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
            NativeBridge.nativeStartGame(
                gamePath, resolution, presentMode, skipMeshShaders,
                enableSrtArm64Jit, perfOverlayEnabled, renderScalePercent, relaxedShaderFloat
            )
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
        if (::logOverlay.isInitialized && logOverlay.visibility == View.VISIBLE) {
            hideLogOverlay()
            return
        }
        confirmExit()
    }

    override fun onDestroy() {
        statsHandler.removeCallbacksAndMessages(null)
        super.onDestroy()
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

    private companion object {
        const val MENU_TOGGLE_CONTROLS = 1
        const val MENU_LOGS = 2
        const val MENU_TOGGLE_HUD = 3
        const val MENU_EXIT = 4
    }
}
