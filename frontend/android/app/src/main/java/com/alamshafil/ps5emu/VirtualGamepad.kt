// SPDX-License-Identifier: GPL-2.0-or-later
package com.alamshafil.ps5emu

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.atan2
import kotlin.math.cos
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

    private val bgPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(65, 255, 255, 255)
        style = Paint.Style.FILL
    }
    private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(90, 255, 255, 255)
        style = Paint.Style.STROKE
        strokeWidth = dp(1.5f)
    }
    private val activePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(190, 41, 121, 255)
        style = Paint.Style.FILL
    }
    private val stickBasePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(45, 0, 0, 0)
        style = Paint.Style.FILL
    }
    private val stickNubPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(120, 255, 255, 255)
        style = Paint.Style.FILL
    }
    private val stickNubActivePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(220, 41, 121, 255)
        style = Paint.Style.FILL
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = dp(14f)
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }

    private data class ButtonDef(
        val id: Int,
        val label: String,
        val rect: RectF = RectF(),
        val isCircle: Boolean = true,
        val customColor: Int? = null,
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

    private val stickBaseRadius: Float get() = dp(50f)
    private val stickNubRadius: Float get() = dp(24f)

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        setupControls(w.toFloat(), h.toFloat())
    }

    private fun setupControls(width: Float, height: Float) {
        buttons.clear()

        val btnR = dp(24f)
        val dpadR = dp(22f)

        // D-PAD on bottom-left
        val dpadCenterX = dp(100f)
        val dpadCenterY = height - dp(115f)
        val dpadDist = dp(46f)

        buttons.add(ButtonDef(NativeBridge.BUTTON_UP, "▲", RectF(dpadCenterX - dpadR, dpadCenterY - dpadDist - dpadR, dpadCenterX + dpadR, dpadCenterY - dpadDist + dpadR)))
        buttons.add(ButtonDef(NativeBridge.BUTTON_DOWN, "▼", RectF(dpadCenterX - dpadR, dpadCenterY + dpadDist - dpadR, dpadCenterX + dpadR, dpadCenterY + dpadDist + dpadR)))
        buttons.add(ButtonDef(NativeBridge.BUTTON_LEFT, "◀", RectF(dpadCenterX - dpadDist - dpadR, dpadCenterY - dpadR, dpadCenterX - dpadDist + dpadR, dpadCenterY + dpadR)))
        buttons.add(ButtonDef(NativeBridge.BUTTON_RIGHT, "▶", RectF(dpadCenterX + dpadDist - dpadR, dpadCenterY - dpadR, dpadCenterX + dpadDist + dpadR, dpadCenterY + dpadR)))

        // Action Buttons on bottom-right (Cross, Circle, Square, Triangle)
        val actionCenterX = width - dp(100f)
        val actionCenterY = height - dp(115f)
        val actionDist = dp(46f)

        buttons.add(ButtonDef(NativeBridge.BUTTON_TRIANGLE, "▲", RectF(actionCenterX - btnR, actionCenterY - actionDist - btnR, actionCenterX + btnR, actionCenterY - actionDist + btnR), customColor = Color.argb(190, 76, 175, 80)))
        buttons.add(ButtonDef(NativeBridge.BUTTON_CROSS, "✕", RectF(actionCenterX - btnR, actionCenterY + actionDist - btnR, actionCenterX + btnR, actionCenterY + actionDist + btnR), customColor = Color.argb(190, 33, 150, 243)))
        buttons.add(ButtonDef(NativeBridge.BUTTON_SQUARE, "■", RectF(actionCenterX - actionDist - btnR, actionCenterY - btnR, actionCenterX - actionDist + btnR, actionCenterY + btnR), customColor = Color.argb(190, 233, 30, 99)))
        buttons.add(ButtonDef(NativeBridge.BUTTON_CIRCLE, "●", RectF(actionCenterX + actionDist - btnR, actionCenterY - btnR, actionCenterX + actionDist + btnR, actionCenterY + btnR), customColor = Color.argb(190, 244, 67, 54)))

        // Shoulder & Trigger Buttons
        val shoulderW = dp(64f)
        val shoulderH = dp(32f)

        buttons.add(ButtonDef(NativeBridge.BUTTON_L1, "L1", RectF(dp(20f), dp(20f), dp(20f) + shoulderW, dp(20f) + shoulderH), isCircle = false))
        buttons.add(ButtonDef(NativeBridge.BUTTON_L2, "L2", RectF(dp(95f), dp(20f), dp(95f) + shoulderW, dp(20f) + shoulderH), isCircle = false))
        buttons.add(ButtonDef(NativeBridge.BUTTON_R2, "R2", RectF(width - dp(95f) - shoulderW, dp(20f), width - dp(95f), dp(20f) + shoulderH), isCircle = false))
        buttons.add(ButtonDef(NativeBridge.BUTTON_R1, "R1", RectF(width - dp(20f) - shoulderW, dp(20f), width - dp(20f), dp(20f) + shoulderH), isCircle = false))

        // Center Option Buttons (Options, Touchpad, L3, R3)
        val centerBtnW = dp(48f)
        val centerBtnH = dp(28f)
        val screenMidX = width / 2f

        buttons.add(ButtonDef(NativeBridge.BUTTON_L3, "L3", RectF(screenMidX - dp(120f) - centerBtnW / 2, dp(20f), screenMidX - dp(120f) + centerBtnW / 2, dp(20f) + centerBtnH), isCircle = false))
        buttons.add(ButtonDef(NativeBridge.BUTTON_TOUCH_PAD, "PAD", RectF(screenMidX - dp(35f), dp(18f), screenMidX + dp(35f), dp(18f) + dp(34f)), isCircle = false))
        buttons.add(ButtonDef(NativeBridge.BUTTON_OPTIONS, "OPTIONS", RectF(screenMidX + dp(120f) - centerBtnW / 2, dp(20f), screenMidX + dp(120f) + centerBtnW / 2, dp(20f) + centerBtnH), isCircle = false))

        // Left Analog Stick
        leftStickCenter = Pair(dp(200f), height - dp(100f))
        leftStickPos = leftStickCenter

        // Right Analog Stick
        rightStickCenter = Pair(width - dp(200f), height - dp(100f))
        rightStickPos = rightStickCenter
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        // Draw buttons
        for (btn in buttons) {
            val paint = if (btn.isPressed) {
                if (btn.customColor != null) {
                    Paint(Paint.ANTI_ALIAS_FLAG).apply { color = btn.customColor; style = Paint.Style.FILL }
                } else {
                    activePaint
                }
            } else {
                bgPaint
            }

            if (btn.isCircle) {
                canvas.drawCircle(btn.rect.centerX(), btn.rect.centerY(), btn.rect.width() / 2f, paint)
                canvas.drawCircle(btn.rect.centerX(), btn.rect.centerY(), btn.rect.width() / 2f, strokePaint)
            } else {
                val cr = dp(6f)
                canvas.drawRoundRect(btn.rect, cr, cr, paint)
                canvas.drawRoundRect(btn.rect, cr, cr, strokePaint)
            }

            // Draw label
            val textY = btn.rect.centerY() - (textPaint.descent() + textPaint.ascent()) / 2
            canvas.drawText(btn.label, btn.rect.centerX(), textY, textPaint)
        }

        // Draw Left Stick
        canvas.drawCircle(leftStickCenter.first, leftStickCenter.second, stickBaseRadius, stickBasePaint)
        canvas.drawCircle(leftStickCenter.first, leftStickCenter.second, stickBaseRadius, strokePaint)
        val lNubPaint = if (leftStickPointerId != -1) stickNubActivePaint else stickNubPaint
        canvas.drawCircle(leftStickPos.first, leftStickPos.second, stickNubRadius, lNubPaint)

        // Draw Right Stick
        canvas.drawCircle(rightStickCenter.first, rightStickCenter.second, stickBaseRadius, stickBasePaint)
        canvas.drawCircle(rightStickCenter.first, rightStickCenter.second, stickBaseRadius, strokePaint)
        val rNubPaint = if (rightStickPointerId != -1) stickNubActivePaint else stickNubPaint
        canvas.drawCircle(rightStickPos.first, rightStickPos.second, stickNubRadius, rNubPaint)
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
        // Check buttons
        for (btn in buttons) {
            if (btn.rect.contains(x, y) && btn.pointerId == -1) {
                btn.isPressed = true
                btn.pointerId = pointerId
                NativeBridge.nativeSendButton(btn.id, true)
                return
            }
        }

        // Check Left Stick
        val ldx = x - leftStickCenter.first
        val ldy = y - leftStickCenter.second
        if (sqrt(ldx * ldx + ldy * ldy) <= stickBaseRadius * 1.5f && leftStickPointerId == -1) {
            leftStickPointerId = pointerId
            updateStick(isLeft = true, x, y)
            return
        }

        // Check Right Stick
        val rdx = x - rightStickCenter.first
        val rdy = y - rightStickCenter.second
        if (sqrt(rdx * rdx + rdy * rdy) <= stickBaseRadius * 1.5f && rightStickPointerId == -1) {
            rightStickPointerId = pointerId
            updateStick(isLeft = false, x, y)
            return
        }
    }

    private fun handlePointerMove(pointerId: Int, x: Float, y: Float) {
        for (btn in buttons) {
            if (btn.pointerId == pointerId) {
                val stillInside = btn.rect.contains(x, y)
                if (btn.isPressed != stillInside) {
                    btn.isPressed = stillInside
                    NativeBridge.nativeSendButton(btn.id, stillInside)
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
            }
        }

        if (pointerId == leftStickPointerId) {
            leftStickPointerId = -1
            leftStickPos = leftStickCenter
            NativeBridge.nativeSendStick(0, 0f, 0f)
        } else if (pointerId == rightStickPointerId) {
            rightStickPointerId = -1
            rightStickPos = rightStickCenter
            NativeBridge.nativeSendStick(1, 0f, 0f)
        }
    }

    private fun resetAllControls() {
        for (btn in buttons) {
            if (btn.isPressed) {
                NativeBridge.nativeSendButton(btn.id, false)
            }
            btn.isPressed = false
            btn.pointerId = -1
        }
        if (leftStickPointerId != -1) {
            leftStickPointerId = -1
            leftStickPos = leftStickCenter
            NativeBridge.nativeSendStick(0, 0f, 0f)
        }
        if (rightStickPointerId != -1) {
            rightStickPointerId = -1
            rightStickPos = rightStickCenter
            NativeBridge.nativeSendStick(1, 0f, 0f)
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

        if (isLeft) {
            leftStickPos = Pair(clampedX, clampedY)
            val normX = (clampedX - center.first) / maxDist
            val normY = (clampedY - center.second) / maxDist
            NativeBridge.nativeSendStick(0, normX, -normY)
        } else {
            rightStickPos = Pair(clampedX, clampedY)
            val normX = (clampedX - center.first) / maxDist
            val normY = (clampedY - center.second) / maxDist
            NativeBridge.nativeSendStick(1, normX, -normY)
        }
    }
}
