// SPDX-License-Identifier: GPL-2.0-or-later
package com.alamshafil.ps5emu

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.util.AttributeSet
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

class VirtualGamepad @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    private val density = resources.displayMetrics.density
    private fun dp(v: Float): Float = v * density

    private val normalBgPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(97, 0, 0, 0)
        style = Paint.Style.FILL
    }
    private val pressedBgPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(102, 255, 255, 255)
        style = Paint.Style.FILL
    }

    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }
    private val chevronPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        style = Paint.Style.STROKE
        strokeWidth = dp(2.5f)
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }
    private val trianglePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(52, 199, 89)
        style = Paint.Style.STROKE
        strokeWidth = dp(2.4f)
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }
    private val squarePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(255, 115, 191)
        style = Paint.Style.STROKE
        strokeWidth = dp(2.4f)
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }
    private val circlePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(255, 59, 48)
        style = Paint.Style.STROKE
        strokeWidth = dp(2.4f)
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }
    private val crossPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(0, 122, 255)
        style = Paint.Style.STROKE
        strokeWidth = dp(2.6f)
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }
    private val padPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(153, 255, 255, 255)
        style = Paint.Style.STROKE
        strokeWidth = dp(1.8f)
        strokeCap = Paint.Cap.ROUND
    }

    private val stickBasePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(90, 0, 0, 0)
        style = Paint.Style.FILL
    }
    private val centerDotPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(46, 255, 255, 255)
        style = Paint.Style.FILL
    }
    private val stickKnobShadowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(80, 0, 0, 0)
        style = Paint.Style.FILL
    }
    private val stickKnobPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(166, 224, 224, 224)
        style = Paint.Style.FILL
    }

    private val tempPath = Path()
    private val tempRect = RectF()

    enum class ButtonGlyph {
        TEXT,
        CHEVRON_UP,
        CHEVRON_DOWN,
        CHEVRON_LEFT,
        CHEVRON_RIGHT,
        TRIANGLE,
        SQUARE,
        CIRCLE,
        CROSS,
        TOUCHPAD
    }

    private data class ButtonDef(
        val id: Int,
        val glyph: ButtonGlyph,
        val label: String = "",
        val rect: RectF = RectF(),
        val isCircle: Boolean = true,
        val cornerRadiusDp: Float = 8f,
        val symbolColor: Int = Color.WHITE,
        val textSizeSp: Float = 13f,
        var isPressed: Boolean = false,
        var pointerId: Int = -1
    )

    private val buttons = mutableListOf<ButtonDef>()

    private var leftStickCenter = Pair(0f, 0f)
    private var leftStickPos = Pair(0f, 0f)
    private var leftStickPointerId = -1

    private var rightStickCenter = Pair(0f, 0f)
    private var rightStickPos = Pair(0f, 0f)
    private var rightStickPointerId = -1

    private val stickBaseRadius: Float get() = dp(42f)
    private val stickNubRadius: Float get() = dp(20f)
    private val stickDeadzone: Float get() = dp(3.5f)

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        setupControls(w.toFloat(), h.toFloat())
    }

    private fun setupControls(width: Float, height: Float) {
        buttons.clear()

        val safeLeft = max(dp(36f), dp(44f))
        val safeRight = max(dp(36f), dp(44f))
        val topY = dp(46f)

        val bumperW = dp(50f)
        val bumperH = dp(30f)

        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_L2,
            glyph = ButtonGlyph.TEXT,
            label = "L2",
            rect = RectF(safeLeft, topY, safeLeft + bumperW, topY + bumperH),
            isCircle = false,
            cornerRadiusDp = 8f,
            textSizeSp = 13f
        ))
        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_L1,
            glyph = ButtonGlyph.TEXT,
            label = "L1",
            rect = RectF(safeLeft + bumperW + dp(8f), topY, safeLeft + bumperW * 2 + dp(8f), topY + bumperH),
            isCircle = false,
            cornerRadiusDp = 8f,
            textSizeSp = 13f
        ))

        val r2Left = width - safeRight - bumperW
        val r1Left = r2Left - dp(8f) - bumperW
        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_R1,
            glyph = ButtonGlyph.TEXT,
            label = "R1",
            rect = RectF(r1Left, topY, r1Left + bumperW, topY + bumperH),
            isCircle = false,
            cornerRadiusDp = 8f,
            textSizeSp = 13f
        ))
        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_R2,
            glyph = ButtonGlyph.TEXT,
            label = "R2",
            rect = RectF(r2Left, topY, r2Left + bumperW, topY + bumperH),
            isCircle = false,
            cornerRadiusDp = 8f,
            textSizeSp = 13f
        ))

        val utilH = dp(26f)
        val utilY = topY + dp(2f)
        val midX = width / 2f
        val mutedWhite = Color.argb(217, 255, 255, 255)

        val shareW = dp(52f)
        val stickBtnW = dp(36f)
        val padW = dp(54f)
        val optW = dp(58f)

        val padLeft = midX - padW / 2f
        val padRight = padLeft + padW
        val l3Right = padLeft - dp(8f)
        val l3Left = l3Right - stickBtnW
        val shareRight = l3Left - dp(8f)
        val shareLeft = shareRight - shareW

        val r3Left = padRight + dp(8f)
        val r3Right = r3Left + stickBtnW
        val optLeft = r3Right + dp(8f)
        val optRight = optLeft + optW

        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_SHARE,
            glyph = ButtonGlyph.TEXT,
            label = "SHARE",
            rect = RectF(shareLeft, utilY, shareRight, utilY + utilH),
            isCircle = false,
            cornerRadiusDp = 6f,
            symbolColor = mutedWhite,
            textSizeSp = 10f
        ))
        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_L3,
            glyph = ButtonGlyph.TEXT,
            label = "L3",
            rect = RectF(l3Left, utilY, l3Right, utilY + utilH),
            isCircle = false,
            cornerRadiusDp = 6f,
            symbolColor = mutedWhite,
            textSizeSp = 10f
        ))
        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_TOUCH_PAD,
            glyph = ButtonGlyph.TOUCHPAD,
            label = "",
            rect = RectF(padLeft, utilY, padRight, utilY + utilH),
            isCircle = false,
            cornerRadiusDp = 6f
        ))
        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_R3,
            glyph = ButtonGlyph.TEXT,
            label = "R3",
            rect = RectF(r3Left, utilY, r3Right, utilY + utilH),
            isCircle = false,
            cornerRadiusDp = 6f,
            symbolColor = mutedWhite,
            textSizeSp = 10f
        ))
        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_OPTIONS,
            glyph = ButtonGlyph.TEXT,
            label = "OPTIONS",
            rect = RectF(optLeft, utilY, optRight, utilY + utilH),
            isCircle = false,
            cornerRadiusDp = 6f,
            symbolColor = mutedWhite,
            textSizeSp = 10f
        ))

        val btnR = dp(21f)
        val isLargeScreen = height >= dp(460f)
        val stickCenterY = if (isLargeScreen) height - dp(85f) else height - dp(65f)
        val stickToDpadGap = if (isLargeScreen) dp(120f) else dp(105f)
        val clusterCenterY = stickCenterY - stickToDpadGap

        val dpadCenterX = safeLeft + dp(52f)
        val actionCenterX = width - safeRight - dp(52f)

        val vertOffset = dp(46f)
        val horizOffset = dp(31f)

        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_UP,
            glyph = ButtonGlyph.CHEVRON_UP,
            rect = RectF(dpadCenterX - btnR, clusterCenterY - vertOffset - btnR, dpadCenterX + btnR, clusterCenterY - vertOffset + btnR),
            isCircle = true
        ))
        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_DOWN,
            glyph = ButtonGlyph.CHEVRON_DOWN,
            rect = RectF(dpadCenterX - btnR, clusterCenterY + vertOffset - btnR, dpadCenterX + btnR, clusterCenterY + vertOffset + btnR),
            isCircle = true
        ))
        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_LEFT,
            glyph = ButtonGlyph.CHEVRON_LEFT,
            rect = RectF(dpadCenterX - horizOffset - btnR, clusterCenterY - btnR, dpadCenterX - horizOffset + btnR, clusterCenterY + btnR),
            isCircle = true
        ))
        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_RIGHT,
            glyph = ButtonGlyph.CHEVRON_RIGHT,
            rect = RectF(dpadCenterX + horizOffset - btnR, clusterCenterY - btnR, dpadCenterX + horizOffset + btnR, clusterCenterY + btnR),
            isCircle = true
        ))

        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_TRIANGLE,
            glyph = ButtonGlyph.TRIANGLE,
            rect = RectF(actionCenterX - btnR, clusterCenterY - vertOffset - btnR, actionCenterX + btnR, clusterCenterY - vertOffset + btnR),
            isCircle = true
        ))
        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_CROSS,
            glyph = ButtonGlyph.CROSS,
            rect = RectF(actionCenterX - btnR, clusterCenterY + vertOffset - btnR, actionCenterX + btnR, clusterCenterY + vertOffset + btnR),
            isCircle = true
        ))
        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_SQUARE,
            glyph = ButtonGlyph.SQUARE,
            rect = RectF(actionCenterX - horizOffset - btnR, clusterCenterY - btnR, actionCenterX - horizOffset + btnR, clusterCenterY + btnR),
            isCircle = true
        ))
        buttons.add(ButtonDef(
            id = NativeBridge.BUTTON_CIRCLE,
            glyph = ButtonGlyph.CIRCLE,
            rect = RectF(actionCenterX + horizOffset - btnR, clusterCenterY - btnR, actionCenterX + horizOffset + btnR, clusterCenterY + btnR),
            isCircle = true
        ))

        leftStickCenter = Pair(dpadCenterX, stickCenterY)
        leftStickPos = leftStickCenter

        rightStickCenter = Pair(actionCenterX, stickCenterY)
        rightStickPos = rightStickCenter
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        for (btn in buttons) {
            val cx = btn.rect.centerX()
            val cy = btn.rect.centerY()

            canvas.save()
            if (btn.isPressed) {
                canvas.scale(0.93f, 0.93f, cx, cy)
            }

            val bgPaint = if (btn.isPressed) pressedBgPaint else normalBgPaint
            if (btn.isCircle) {
                canvas.drawCircle(cx, cy, btn.rect.width() / 2f, bgPaint)
            } else {
                val cr = dp(btn.cornerRadiusDp)
                canvas.drawRoundRect(btn.rect, cr, cr, bgPaint)
            }

            when (btn.glyph) {
                ButtonGlyph.TEXT -> {
                    textPaint.textSize = dp(btn.textSizeSp)
                    textPaint.color = btn.symbolColor
                    val textY = cy - (textPaint.descent() + textPaint.ascent()) / 2f
                    canvas.drawText(btn.label, cx, textY, textPaint)
                }
                ButtonGlyph.CHEVRON_UP -> {
                    val arm = dp(6.5f)
                    tempPath.reset()
                    tempPath.moveTo(cx - arm, cy + arm * 0.4f)
                    tempPath.lineTo(cx, cy - arm * 0.5f)
                    tempPath.lineTo(cx + arm, cy + arm * 0.4f)
                    canvas.drawPath(tempPath, chevronPaint)
                }
                ButtonGlyph.CHEVRON_DOWN -> {
                    val arm = dp(6.5f)
                    tempPath.reset()
                    tempPath.moveTo(cx - arm, cy - arm * 0.4f)
                    tempPath.lineTo(cx, cy + arm * 0.5f)
                    tempPath.lineTo(cx + arm, cy - arm * 0.4f)
                    canvas.drawPath(tempPath, chevronPaint)
                }
                ButtonGlyph.CHEVRON_LEFT -> {
                    val arm = dp(6.5f)
                    tempPath.reset()
                    tempPath.moveTo(cx + arm * 0.4f, cy - arm)
                    tempPath.lineTo(cx - arm * 0.5f, cy)
                    tempPath.lineTo(cx + arm * 0.4f, cy + arm)
                    canvas.drawPath(tempPath, chevronPaint)
                }
                ButtonGlyph.CHEVRON_RIGHT -> {
                    val arm = dp(6.5f)
                    tempPath.reset()
                    tempPath.moveTo(cx - arm, cy - arm * 0.4f)
                    tempPath.lineTo(cx + arm * 0.5f, cy)
                    tempPath.lineTo(cx - arm * 0.4f, cy + arm)
                    canvas.drawPath(tempPath, chevronPaint)
                }
                ButtonGlyph.TRIANGLE -> {
                    val r = dp(8.5f)
                    tempPath.reset()
                    tempPath.moveTo(cx, cy - r)
                    tempPath.lineTo(cx + r * 0.866f, cy + r * 0.5f)
                    tempPath.lineTo(cx - r * 0.866f, cy + r * 0.5f)
                    tempPath.close()
                    canvas.drawPath(tempPath, trianglePaint)
                }
                ButtonGlyph.SQUARE -> {
                    val s = dp(7f)
                    tempRect.set(cx - s, cy - s, cx + s, cy + s)
                    canvas.drawRoundRect(tempRect, dp(2f), dp(2f), squarePaint)
                }
                ButtonGlyph.CIRCLE -> {
                    val r = dp(7.5f)
                    canvas.drawCircle(cx, cy, r, circlePaint)
                }
                ButtonGlyph.CROSS -> {
                    val c = dp(6.5f)
                    canvas.drawLine(cx - c, cy - c, cx + c, cy + c, crossPaint)
                    canvas.drawLine(cx - c, cy + c, cx + c, cy - c, crossPaint)
                }
                ButtonGlyph.TOUCHPAD -> {
                    val pw = dp(13f)
                    val ph = dp(6.5f)
                    tempRect.set(cx - pw, cy - ph, cx + pw, cy + ph)
                    canvas.drawRoundRect(tempRect, dp(3f), dp(3f), padPaint)
                    canvas.drawLine(cx, cy - ph + dp(2f), cx, cy - ph + dp(4.5f), padPaint)
                }
            }
            canvas.restore()
        }

        drawThumbstick(canvas, leftStickCenter.first, leftStickCenter.second, leftStickPos.first, leftStickPos.second)
        drawThumbstick(canvas, rightStickCenter.first, rightStickCenter.second, rightStickPos.first, rightStickPos.second)
    }

    private fun drawThumbstick(canvas: Canvas, baseX: Float, baseY: Float, knobX: Float, knobY: Float) {
        canvas.drawCircle(baseX, baseY, stickBaseRadius, stickBasePaint)
        canvas.drawCircle(baseX, baseY, dp(7f), centerDotPaint)
        canvas.drawCircle(knobX, knobY + dp(2f), stickNubRadius + dp(1f), stickKnobShadowPaint)
        canvas.drawCircle(knobX, knobY, stickNubRadius, stickKnobPaint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val actionMasked = event.actionMasked
        val actionIndex = event.actionIndex
        val pointerId = event.getPointerId(actionIndex)

        when (actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val x = event.getX(actionIndex)
                val y = event.getY(actionIndex)
                handlePointerDown(pointerId, x, y)
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) {
                    val pid = event.getPointerId(i)
                    val px = event.getX(i)
                    val py = event.getY(i)
                    handlePointerMove(pid, px, py)
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                handlePointerUp(pointerId)
            }
            MotionEvent.ACTION_CANCEL -> {
                resetAllControls()
            }
        }
        invalidate()
        return true
    }

    private fun handlePointerDown(pointerId: Int, x: Float, y: Float) {
        for (btn in buttons) {
            if (btn.rect.contains(x, y) && btn.pointerId == -1) {
                btn.isPressed = true
                btn.pointerId = pointerId
                triggerHaptic()

                NativeBridge.nativeSendButton(btn.id, true)
                if (btn.id == NativeBridge.BUTTON_L2) {
                    NativeBridge.nativeSendTrigger(NativeBridge.TRIGGER_L2, 1.0f)
                } else if (btn.id == NativeBridge.BUTTON_R2) {
                    NativeBridge.nativeSendTrigger(NativeBridge.TRIGGER_R2, 1.0f)
                }
                return
            }
        }

        val ldx = x - leftStickCenter.first
        val ldy = y - leftStickCenter.second
        if (sqrt(ldx * ldx + ldy * ldy) <= stickBaseRadius * 1.5f && leftStickPointerId == -1) {
            leftStickPointerId = pointerId
            triggerHaptic()
            updateStick(isLeft = true, x, y)
            return
        }

        val rdx = x - rightStickCenter.first
        val rdy = y - rightStickCenter.second
        if (sqrt(rdx * rdx + rdy * rdy) <= stickBaseRadius * 1.5f && rightStickPointerId == -1) {
            rightStickPointerId = pointerId
            triggerHaptic()
            updateStick(isLeft = false, x, y)
            return
        }
    }

    private fun handlePointerMove(pointerId: Int, x: Float, y: Float) {
        val slop = dp(15f)
        for (btn in buttons) {
            if (btn.pointerId == pointerId) {
                val expanded = RectF(
                    btn.rect.left - slop,
                    btn.rect.top - slop,
                    btn.rect.right + slop,
                    btn.rect.bottom + slop
                )
                val stillInside = expanded.contains(x, y)
                if (btn.isPressed != stillInside) {
                    btn.isPressed = stillInside
                    NativeBridge.nativeSendButton(btn.id, stillInside)
                    if (btn.id == NativeBridge.BUTTON_L2) {
                        NativeBridge.nativeSendTrigger(NativeBridge.TRIGGER_L2, if (stillInside) 1.0f else 0.0f)
                    } else if (btn.id == NativeBridge.BUTTON_R2) {
                        NativeBridge.nativeSendTrigger(NativeBridge.TRIGGER_R2, if (stillInside) 1.0f else 0.0f)
                    }
                }
            }
        }

        if (pointerId == leftStickPointerId) {
            updateStick(isLeft = true, x, y)
        } else if (pointerId == rightStickPointerId) {
            updateStick(isLeft = false, x, y)
        }
    }

    private fun handlePointerUp(pointerId: Int) {
        for (btn in buttons) {
            if (btn.pointerId == pointerId) {
                btn.isPressed = false
                btn.pointerId = -1
                NativeBridge.nativeSendButton(btn.id, false)
                if (btn.id == NativeBridge.BUTTON_L2) {
                    NativeBridge.nativeSendTrigger(NativeBridge.TRIGGER_L2, 0.0f)
                } else if (btn.id == NativeBridge.BUTTON_R2) {
                    NativeBridge.nativeSendTrigger(NativeBridge.TRIGGER_R2, 0.0f)
                }
            }
        }

        if (pointerId == leftStickPointerId) {
            leftStickPointerId = -1
            leftStickPos = leftStickCenter
            NativeBridge.nativeSendStick(NativeBridge.STICK_LEFT, 0f, 0f)
        } else if (pointerId == rightStickPointerId) {
            rightStickPointerId = -1
            rightStickPos = rightStickCenter
            NativeBridge.nativeSendStick(NativeBridge.STICK_RIGHT, 0f, 0f)
        }
    }

    private fun resetAllControls() {
        for (btn in buttons) {
            if (btn.isPressed) {
                NativeBridge.nativeSendButton(btn.id, false)
                if (btn.id == NativeBridge.BUTTON_L2) {
                    NativeBridge.nativeSendTrigger(NativeBridge.TRIGGER_L2, 0.0f)
                } else if (btn.id == NativeBridge.BUTTON_R2) {
                    NativeBridge.nativeSendTrigger(NativeBridge.TRIGGER_R2, 0.0f)
                }
            }
            btn.isPressed = false
            btn.pointerId = -1
        }
        if (leftStickPointerId != -1) {
            leftStickPointerId = -1
            leftStickPos = leftStickCenter
            NativeBridge.nativeSendStick(NativeBridge.STICK_LEFT, 0f, 0f)
        }
        if (rightStickPointerId != -1) {
            rightStickPointerId = -1
            rightStickPos = rightStickCenter
            NativeBridge.nativeSendStick(NativeBridge.STICK_RIGHT, 0f, 0f)
        }
    }

    private fun updateStick(isLeft: Boolean, touchX: Float, touchY: Float) {
        val center = if (isLeft) leftStickCenter else rightStickCenter
        val dx = touchX - center.first
        val dy = touchY - center.second
        val dist = sqrt(dx * dx + dy * dy)
        val maxDist = stickBaseRadius

        val clampedDist = min(dist, maxDist)
        val angle = atan2(dy, dx)
        val clampedX = center.first + clampedDist * cos(angle)
        val clampedY = center.second + clampedDist * sin(angle)

        val stickId = if (isLeft) NativeBridge.STICK_LEFT else NativeBridge.STICK_RIGHT

        if (isLeft) {
            leftStickPos = Pair(clampedX, clampedY)
        } else {
            rightStickPos = Pair(clampedX, clampedY)
        }

        if (dist < stickDeadzone) {
            NativeBridge.nativeSendStick(stickId, 0f, 0f)
        } else {
            val normX = max(-1.0f, min(1.0f, (clampedX - center.first) / maxDist))
            val normY = max(-1.0f, min(1.0f, -(clampedY - center.second) / maxDist))
            NativeBridge.nativeSendStick(stickId, normX, normY)
        }
    }

    private fun triggerHaptic() {
        try {
            performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
        } catch (_: Exception) {}
    }
}
