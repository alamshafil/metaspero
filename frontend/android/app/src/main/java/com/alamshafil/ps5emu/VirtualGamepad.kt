// SPDX-License-Identifier: GPL-2.0-or-later
package com.alamshafil.ps5emu

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.os.Build
import android.util.AttributeSet
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.view.WindowInsets
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

class VirtualGamepad @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    var hapticsEnabled: Boolean = true

    private val density = resources.displayMetrics.density
    private fun dp(v: Float): Float = v * density

    private var insetLeft = 0f
    private var insetTop = 0f
    private var insetRight = 0f
    private var insetBottom = 0f

    private var uiScale = 1f

    private val bgNormal = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(96, 18, 20, 26)
        style = Paint.Style.FILL
    }
    private val bgPressed = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(96, 255, 255, 255)
        style = Paint.Style.FILL
    }
    private val stickBasePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(86, 18, 20, 26)
        style = Paint.Style.FILL
    }
    private val stickCenterDotPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(42, 255, 255, 255)
        style = Paint.Style.FILL
    }
    private val stickKnobIdlePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(145, 232, 236, 244)
        style = Paint.Style.FILL
    }
    private val stickKnobActivePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(205, 245, 248, 255)
        style = Paint.Style.FILL
    }

    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }
    private val glyphPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }

    private val tempPath = Path()
    private val tempRect = RectF()

    private enum class Glyph {
        TEXT,
        UP, DOWN, LEFT, RIGHT,
        TRIANGLE, CIRCLE, CROSS, SQUARE,
        TOUCHPAD
    }

    private data class Button(
        val id: Int,
        val glyph: Glyph,
        val label: String = "",
        val rect: RectF = RectF(),
        val isCircle: Boolean = false,
        val isFace: Boolean = false,
        val isDpad: Boolean = false,
        val cornerRadius: Float = 0f,
        val tint: Int = Color.WHITE,
        val textSizePx: Float = 0f,
        var pressed: Boolean = false,
        var pointerId: Int = -1
    )

    private data class Stick(
        val id: Int,
        var cx: Float = 0f,
        var cy: Float = 0f,
        var knobX: Float = 0f,
        var knobY: Float = 0f,
        var baseRadius: Float = 0f,
        var knobRadius: Float = 0f,
        var pointerId: Int = -1
    )

    private val buttons = mutableListOf<Button>()
    private val leftStick = Stick(NativeBridge.STICK_LEFT)
    private val rightStick = Stick(NativeBridge.STICK_RIGHT)

    private var dpadCx = 0f
    private var dpadCy = 0f
    private var dpadZoneRadius = 0f
    private var dpadDeadzone = 0f
    private var dpadPointerId = -1

    override fun onApplyWindowInsets(insets: WindowInsets): WindowInsets {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val cutout = insets.getInsets(WindowInsets.Type.displayCutout())
            insetLeft = cutout.left.toFloat()
            insetTop = cutout.top.toFloat()
            insetRight = cutout.right.toFloat()
            insetBottom = cutout.bottom.toFloat()
        } else {
            @Suppress("DEPRECATION")
            val cutout = insets.displayCutout
            insetLeft = (cutout?.safeInsetLeft ?: 0).toFloat()
            insetTop = (cutout?.safeInsetTop ?: 0).toFloat()
            insetRight = (cutout?.safeInsetRight ?: 0).toFloat()
            insetBottom = (cutout?.safeInsetBottom ?: 0).toFloat()
        }
        if (width > 0 && height > 0) {
            layoutControls(width.toFloat(), height.toFloat())
            invalidate()
        }
        return super.onApplyWindowInsets(insets)
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (w > 0 && h > 0) {
            layoutControls(w.toFloat(), h.toFloat())
        }
    }

    private fun layoutControls(w: Float, h: Float) {
        releaseAllInputs()
        buttons.clear()

        val s = min(w / dp(820f), h / dp(390f)).coerceIn(0.78f, 1.25f)
        uiScale = s
        fun sc(v: Float): Float = dp(v) * s

        val safeL = max(insetLeft + sc(10f), sc(20f))
        val safeR = max(insetRight + sc(10f), sc(20f))
        val safeT = max(insetTop + sc(8f), sc(12f))
        val safeB = max(insetBottom + sc(10f), sc(16f))

        val shoulderY = max(safeT + sc(36f), dp(46f))
        val bumperW = sc(54f)
        val bumperH = sc(30f)
        val bumperGap = sc(8f)
        val bumperCorner = bumperH * 0.42f
        val bumperText = sc(12.5f)

        addPill(NativeBridge.BUTTON_L2, "L2", safeL, shoulderY, bumperW, bumperH, bumperCorner, Color.WHITE, bumperText)
        addPill(NativeBridge.BUTTON_L1, "L1", safeL + bumperW + bumperGap, shoulderY, bumperW, bumperH, bumperCorner, Color.WHITE, bumperText)

        val r2X = w - safeR - bumperW
        addPill(NativeBridge.BUTTON_R1, "R1", r2X - bumperGap - bumperW, shoulderY, bumperW, bumperH, bumperCorner, Color.WHITE, bumperText)
        addPill(NativeBridge.BUTTON_R2, "R2", r2X, shoulderY, bumperW, bumperH, bumperCorner, Color.WHITE, bumperText)

        val utilH = sc(25f)
        val utilY = safeT + sc(2f)
        val utilGap = sc(6f)
        val utilCorner = utilH * 0.45f
        val utilText = sc(9.5f)
        val mutedWhite = Color.argb(215, 255, 255, 255)

        val shareW = sc(48f)
        val l3W = sc(34f)
        val padW = sc(52f)
        val r3W = sc(34f)
        val optW = sc(56f)
        val totalUtilW = shareW + l3W + padW + r3W + optW + utilGap * 4f
        var ux = (w - totalUtilW) * 0.5f

        addPill(NativeBridge.BUTTON_SHARE, "SHARE", ux, utilY, shareW, utilH, utilCorner, mutedWhite, utilText)
        ux += shareW + utilGap
        addPill(NativeBridge.BUTTON_L3, "L3", ux, utilY, l3W, utilH, utilCorner, mutedWhite, utilText)
        ux += l3W + utilGap
        buttons.add(
            Button(
                id = NativeBridge.BUTTON_TOUCH_PAD,
                glyph = Glyph.TOUCHPAD,
                rect = RectF(ux, utilY, ux + padW, utilY + utilH),
                cornerRadius = utilCorner,
                tint = mutedWhite
            )
        )
        ux += padW + utilGap
        addPill(NativeBridge.BUTTON_R3, "R3", ux, utilY, r3W, utilH, utilCorner, mutedWhite, utilText)
        ux += r3W + utilGap
        addPill(NativeBridge.BUTTON_OPTIONS, "OPTIONS", ux, utilY, optW, utilH, utilCorner, mutedWhite, utilText)

        // Ergonomic DualShock/DualSense landscape arc:
        // Outer-left D-Pad + inner-bottom-left Stick; Outer-right Face buttons + inner-bottom-right Stick
        val dpadBtnR = sc(22f)
        val dpadSpread = sc(38f)
        dpadCx = safeL + dpadSpread + dpadBtnR + sc(4f)
        dpadCy = h - safeB - sc(108f)
        dpadZoneRadius = dpadSpread + dpadBtnR + sc(12f)
        dpadDeadzone = sc(11f)

        addCircle(NativeBridge.BUTTON_UP, Glyph.UP, dpadCx, dpadCy - dpadSpread, dpadBtnR, Color.WHITE, isDpad = true)
        addCircle(NativeBridge.BUTTON_DOWN, Glyph.DOWN, dpadCx, dpadCy + dpadSpread, dpadBtnR, Color.WHITE, isDpad = true)
        addCircle(NativeBridge.BUTTON_LEFT, Glyph.LEFT, dpadCx - dpadSpread, dpadCy, dpadBtnR, Color.WHITE, isDpad = true)
        addCircle(NativeBridge.BUTTON_RIGHT, Glyph.RIGHT, dpadCx + dpadSpread, dpadCy, dpadBtnR, Color.WHITE, isDpad = true)

        val faceBtnR = sc(23f)
        val faceSpread = sc(41f)
        val faceCx = w - safeR - faceSpread - faceBtnR - sc(4f)
        val faceCy = dpadCy

        addCircle(NativeBridge.BUTTON_TRIANGLE, Glyph.TRIANGLE, faceCx, faceCy - faceSpread, faceBtnR, Color.rgb(74, 222, 128), isFace = true)
        addCircle(NativeBridge.BUTTON_CROSS, Glyph.CROSS, faceCx, faceCy + faceSpread, faceBtnR, Color.rgb(96, 165, 250), isFace = true)
        addCircle(NativeBridge.BUTTON_SQUARE, Glyph.SQUARE, faceCx - faceSpread, faceCy, faceBtnR, Color.rgb(244, 114, 182), isFace = true)
        addCircle(NativeBridge.BUTTON_CIRCLE, Glyph.CIRCLE, faceCx + faceSpread, faceCy, faceBtnR, Color.rgb(255, 107, 107), isFace = true)

        val stickBaseR = sc(44f)
        val stickKnobR = sc(21f)
        val stickY = h - safeB - stickBaseR - sc(6f)

        leftStick.apply {
            cx = dpadCx + sc(106f)
            cy = stickY
            knobX = cx
            knobY = cy
            baseRadius = stickBaseR
            knobRadius = stickKnobR
            pointerId = -1
        }
        rightStick.apply {
            cx = faceCx - sc(108f)
            cy = stickY
            knobX = cx
            knobY = cy
            baseRadius = stickBaseR
            knobRadius = stickKnobR
            pointerId = -1
        }
    }

    private fun addPill(
        id: Int,
        label: String,
        x: Float,
        y: Float,
        w: Float,
        h: Float,
        corner: Float,
        tint: Int,
        textSizePx: Float
    ) {
        buttons.add(
            Button(
                id = id,
                glyph = Glyph.TEXT,
                label = label,
                rect = RectF(x, y, x + w, y + h),
                cornerRadius = corner,
                tint = tint,
                textSizePx = textSizePx
            )
        )
    }

    private fun addCircle(
        id: Int,
        glyph: Glyph,
        cx: Float,
        cy: Float,
        radius: Float,
        tint: Int,
        isFace: Boolean = false,
        isDpad: Boolean = false
    ) {
        buttons.add(
            Button(
                id = id,
                glyph = glyph,
                rect = RectF(cx - radius, cy - radius, cx + radius, cy + radius),
                isCircle = true,
                isFace = isFace,
                isDpad = isDpad,
                tint = tint
            )
        )
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        for (btn in buttons) {
            drawButton(canvas, btn)
        }
        drawStick(canvas, leftStick)
        drawStick(canvas, rightStick)
    }

    private fun drawButton(canvas: Canvas, btn: Button) {
        val cx = btn.rect.centerX()
        val cy = btn.rect.centerY()

        canvas.save()
        if (btn.pressed) {
            canvas.scale(0.92f, 0.92f, cx, cy)
        }

        val bg = if (btn.pressed) bgPressed else bgNormal
        if (btn.isCircle) {
            canvas.drawCircle(cx, cy, btn.rect.width() * 0.5f, bg)
        } else {
            canvas.drawRoundRect(btn.rect, btn.cornerRadius, btn.cornerRadius, bg)
        }

        val strokeW = dp(2.3f) * uiScale
        glyphPaint.color = btn.tint
        glyphPaint.strokeWidth = strokeW

        when (btn.glyph) {
            Glyph.TEXT -> {
                textPaint.textSize = btn.textSizePx
                textPaint.color = btn.tint
                val baseline = cy - (textPaint.descent() + textPaint.ascent()) * 0.5f
                canvas.drawText(btn.label, cx, baseline, textPaint)
            }
            Glyph.UP -> drawChevron(canvas, cx, cy, 0f)
            Glyph.RIGHT -> drawChevron(canvas, cx, cy, 90f)
            Glyph.DOWN -> drawChevron(canvas, cx, cy, 180f)
            Glyph.LEFT -> drawChevron(canvas, cx, cy, 270f)
            Glyph.TRIANGLE -> {
                val r = dp(8.5f) * uiScale
                tempPath.reset()
                tempPath.moveTo(cx, cy - r)
                tempPath.lineTo(cx + r * 0.866f, cy + r * 0.55f)
                tempPath.lineTo(cx - r * 0.866f, cy + r * 0.55f)
                tempPath.close()
                canvas.drawPath(tempPath, glyphPaint)
            }
            Glyph.SQUARE -> {
                val s = dp(7f) * uiScale
                tempRect.set(cx - s, cy - s, cx + s, cy + s)
                canvas.drawRoundRect(tempRect, dp(2f) * uiScale, dp(2f) * uiScale, glyphPaint)
            }
            Glyph.CIRCLE -> {
                canvas.drawCircle(cx, cy, dp(7.6f) * uiScale, glyphPaint)
            }
            Glyph.CROSS -> {
                val c = dp(6.6f) * uiScale
                canvas.drawLine(cx - c, cy - c, cx + c, cy + c, glyphPaint)
                canvas.drawLine(cx - c, cy + c, cx + c, cy - c, glyphPaint)
            }
            Glyph.TOUCHPAD -> {
                glyphPaint.strokeWidth = dp(1.7f) * uiScale
                val pw = dp(12f) * uiScale
                val ph = dp(6f) * uiScale
                tempRect.set(cx - pw, cy - ph, cx + pw, cy + ph)
                canvas.drawRoundRect(tempRect, dp(3f) * uiScale, dp(3f) * uiScale, glyphPaint)
            }
        }

        canvas.restore()
    }

    private fun drawChevron(canvas: Canvas, cx: Float, cy: Float, rotationDeg: Float) {
        val arm = dp(6f) * uiScale
        canvas.save()
        canvas.rotate(rotationDeg, cx, cy)
        tempPath.reset()
        tempPath.moveTo(cx - arm, cy + arm * 0.35f)
        tempPath.lineTo(cx, cy - arm * 0.55f)
        tempPath.lineTo(cx + arm, cy + arm * 0.35f)
        canvas.drawPath(tempPath, glyphPaint)
        canvas.restore()
    }

    private fun drawStick(canvas: Canvas, stick: Stick) {
        canvas.drawCircle(stick.cx, stick.cy, stick.baseRadius, stickBasePaint)
        canvas.drawCircle(stick.cx, stick.cy, dp(5.5f) * uiScale, stickCenterDotPaint)
        val knobPaint = if (stick.pointerId != -1) stickKnobActivePaint else stickKnobIdlePaint
        canvas.drawCircle(stick.knobX, stick.knobY, stick.knobRadius, knobPaint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val index = event.actionIndex
        val pointerId = event.getPointerId(index)

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                handlePointerDown(pointerId, event.getX(index), event.getY(index))
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) {
                    handlePointerMove(event.getPointerId(i), event.getX(i), event.getY(i))
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                handlePointerUp(pointerId)
            }
            MotionEvent.ACTION_CANCEL -> {
                releaseAllInputs()
            }
        }
        invalidate()
        return true
    }

    private fun handlePointerDown(pointerId: Int, x: Float, y: Float) {
        // 1. Check unified 8-way D-pad zone
        if (dpadPointerId == -1 && hypot(x - dpadCx, y - dpadCy) <= dpadZoneRadius) {
            dpadPointerId = pointerId
            updateDpadFromPoint(x, y, triggerHapticOnChange = true)
            return
        }

        // 2. Check non-D-pad buttons
        val hitSlop = dp(6f) * uiScale
        val hit = findButtonAt(x, y, hitSlop)
        if (hit != null && hit.pointerId == -1) {
            setButtonState(hit, pressed = true, pointerId = pointerId)
            triggerHaptic()
            return
        }

        // 3. Check analog sticks (generous capture radius)
        if (tryGrabStick(leftStick, pointerId, x, y)) return
        tryGrabStick(rightStick, pointerId, x, y)
    }

    private fun handlePointerMove(pointerId: Int, x: Float, y: Float) {
        if (pointerId == dpadPointerId) {
            updateDpadFromPoint(x, y, triggerHapticOnChange = true)
            return
        }
        if (pointerId == leftStick.pointerId) {
            updateStick(leftStick, x, y)
            return
        }
        if (pointerId == rightStick.pointerId) {
            updateStick(rightStick, x, y)
            return
        }

        val current = buttons.firstOrNull { it.pointerId == pointerId } ?: return
        if (current.isFace) {
            val target = findButtonAt(x, y, dp(8f) * uiScale, onlyFace = true)
            if (target !== current) {
                setButtonState(current, pressed = false, pointerId = -1)
                if (target != null && target.pointerId == -1) {
                    setButtonState(target, pressed = true, pointerId = pointerId)
                    triggerHaptic()
                }
            }
            return
        }

        val slop = dp(14f) * uiScale
        val stillInside = containsWithSlop(current, x, y, slop)
        if (current.pressed != stillInside) {
            setButtonState(current, pressed = stillInside, pointerId = pointerId)
        }
    }

    private fun handlePointerUp(pointerId: Int) {
        if (pointerId == dpadPointerId) {
            dpadPointerId = -1
            clearDpad()
            return
        }
        if (pointerId == leftStick.pointerId) {
            resetStick(leftStick)
            return
        }
        if (pointerId == rightStick.pointerId) {
            resetStick(rightStick)
            return
        }

        for (btn in buttons) {
            if (btn.pointerId == pointerId) {
                setButtonState(btn, pressed = false, pointerId = -1)
            }
        }
    }

    private fun findButtonAt(x: Float, y: Float, slop: Float, onlyFace: Boolean = false): Button? {
        var best: Button? = null
        var bestDist = Float.MAX_VALUE
        for (btn in buttons) {
            if (btn.isDpad) continue
            if (onlyFace && !btn.isFace) continue
            if (!containsWithSlop(btn, x, y, slop)) continue
            val d = hypot(x - btn.rect.centerX(), y - btn.rect.centerY())
            if (d < bestDist) {
                bestDist = d
                best = btn
            }
        }
        return best
    }

    private fun containsWithSlop(btn: Button, x: Float, y: Float, slop: Float): Boolean {
        return if (btn.isCircle) {
            val r = btn.rect.width() * 0.5f + slop
            hypot(x - btn.rect.centerX(), y - btn.rect.centerY()) <= r
        } else {
            x >= btn.rect.left - slop && x <= btn.rect.right + slop &&
                y >= btn.rect.top - slop && y <= btn.rect.bottom + slop
        }
    }

    private fun updateDpadFromPoint(x: Float, y: Float, triggerHapticOnChange: Boolean) {
        val dx = x - dpadCx
        val dy = y - dpadCy
        val dist = hypot(dx, dy)

        var up = false
        var down = false
        var left = false
        var right = false

        if (dist >= dpadDeadzone) {
            val angleDeg = atan2(dy.toDouble(), dx.toDouble()) * 180.0 / PI
            right = abs(angleDeg) <= 67.5
            left = abs(angleDeg) >= 112.5
            down = angleDeg in 22.5..157.5
            up = angleDeg in -157.5..-22.5
        }

        var changed = false
        for (btn in buttons) {
            if (!btn.isDpad) continue
            val want = when (btn.id) {
                NativeBridge.BUTTON_UP -> up
                NativeBridge.BUTTON_DOWN -> down
                NativeBridge.BUTTON_LEFT -> left
                NativeBridge.BUTTON_RIGHT -> right
                else -> false
            }
            if (btn.pressed != want) {
                btn.pressed = want
                NativeBridge.nativeSendButton(btn.id, want)
                if (want) changed = true
            }
        }
        if (changed && triggerHapticOnChange) {
            triggerHaptic()
        }
    }

    private fun clearDpad() {
        for (btn in buttons) {
            if (btn.isDpad && btn.pressed) {
                btn.pressed = false
                NativeBridge.nativeSendButton(btn.id, false)
            }
        }
    }

    private fun setButtonState(btn: Button, pressed: Boolean, pointerId: Int) {
        btn.pointerId = pointerId
        if (btn.pressed == pressed) return
        btn.pressed = pressed
        NativeBridge.nativeSendButton(btn.id, pressed)
        when (btn.id) {
            NativeBridge.BUTTON_L2 -> NativeBridge.nativeSendTrigger(NativeBridge.TRIGGER_L2, if (pressed) 1f else 0f)
            NativeBridge.BUTTON_R2 -> NativeBridge.nativeSendTrigger(NativeBridge.TRIGGER_R2, if (pressed) 1f else 0f)
        }
    }

    private fun tryGrabStick(stick: Stick, pointerId: Int, x: Float, y: Float): Boolean {
        if (stick.pointerId != -1) return false
        if (hypot(x - stick.cx, y - stick.cy) > stick.baseRadius * 1.45f) return false
        stick.pointerId = pointerId
        triggerHaptic()
        updateStick(stick, x, y)
        return true
    }

    private fun updateStick(stick: Stick, touchX: Float, touchY: Float) {
        val dx = touchX - stick.cx
        val dy = touchY - stick.cy
        val dist = hypot(dx, dy)
        val maxDist = stick.baseRadius
        val clamped = min(dist, maxDist)
        val angle = atan2(dy, dx)

        stick.knobX = stick.cx + clamped * cos(angle)
        stick.knobY = stick.cy + clamped * sin(angle)

        val deadzone = maxDist * 0.09f
        if (dist < deadzone) {
            NativeBridge.nativeSendStick(stick.id, 0f, 0f)
        } else {
            val normX = ((stick.knobX - stick.cx) / maxDist).coerceIn(-1f, 1f)
            val normY = (-(stick.knobY - stick.cy) / maxDist).coerceIn(-1f, 1f)
            NativeBridge.nativeSendStick(stick.id, normX, normY)
        }
    }

    private fun resetStick(stick: Stick) {
        stick.pointerId = -1
        stick.knobX = stick.cx
        stick.knobY = stick.cy
        NativeBridge.nativeSendStick(stick.id, 0f, 0f)
    }

    fun releaseAllInputs() {
        dpadPointerId = -1
        for (btn in buttons) {
            if (btn.pressed) {
                btn.pressed = false
                NativeBridge.nativeSendButton(btn.id, false)
                when (btn.id) {
                    NativeBridge.BUTTON_L2 -> NativeBridge.nativeSendTrigger(NativeBridge.TRIGGER_L2, 0f)
                    NativeBridge.BUTTON_R2 -> NativeBridge.nativeSendTrigger(NativeBridge.TRIGGER_R2, 0f)
                }
            }
            btn.pointerId = -1
        }
        if (leftStick.pointerId != -1) resetStick(leftStick)
        if (rightStick.pointerId != -1) resetStick(rightStick)
        invalidate()
    }

    private fun triggerHaptic() {
        if (!hapticsEnabled) return
        try {
            performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
        } catch (_: Exception) {}
    }
}
