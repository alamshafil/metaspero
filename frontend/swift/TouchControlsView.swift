// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI
import GameController
import CoreGraphics

#if os(iOS)
import UIKit
private typealias NativeColor = UIColor
private typealias NativeFont = UIFont
private func nativeColor(from cgColor: CGColor) -> UIColor { UIColor(cgColor: cgColor) }
#elseif os(macOS)
import AppKit
private typealias NativeColor = NSColor
private typealias NativeFont = NSFont
private func nativeColor(from cgColor: CGColor) -> NSColor { NSColor(cgColor: cgColor) ?? .white }
#endif

// MARK: - Shared Borderless Gamepad Model & Renderer

private enum GamepadGlyph {
    case text(String, CGFloat)
    case up, down, left, right
    case triangle, circle, cross, square
    case touchpad
}

private struct GamepadButton {
    let id: KytyButton
    let glyph: GamepadGlyph
    var rect: CGRect
    let isCircle: Bool
    let isFace: Bool
    let isDpad: Bool
    let cornerRadius: CGFloat
    let tint: CGColor
    var pressed: Bool = false
}

private struct GamepadStick {
    let isLeft: Bool
    var center: CGPoint = .zero
    var knob: CGPoint = .zero
    var baseRadius: CGFloat = 0
    var knobRadius: CGFloat = 0
    var active: Bool = false
}

private final class GamepadState {
    var buttons: [GamepadButton] = []
    var leftStick = GamepadStick(isLeft: true)
    var rightStick = GamepadStick(isLeft: false)
    var uiScale: CGFloat = 1.0

    var dpadCenter: CGPoint = .zero
    var dpadZoneRadius: CGFloat = 0
    var dpadDeadzone: CGFloat = 0

    private var l2Pressed = false
    private var r2Pressed = false

    func layout(size: CGSize, insets: (left: CGFloat, top: CGFloat, right: CGFloat, bottom: CGFloat)) {
        releaseAll()
        buttons.removeAll(keepingCapacity: true)

        guard size.width > 0, size.height > 0 else { return }

        let s = min(max(min(size.width / 820.0, size.height / 390.0), 0.78), 1.25)
        uiScale = s
        func sc(_ v: CGFloat) -> CGFloat { v * s }

        let safeL = max(insets.left + sc(10), sc(20))
        let safeR = max(insets.right + sc(10), sc(20))
        let safeT = max(insets.top + sc(8), sc(12))
        let safeB = max(insets.bottom + sc(10), sc(16))

        let white = NativeColor.white.cgColor
        let muted = NativeColor.white.withAlphaComponent(0.84).cgColor

        // Top shoulders (positioned below top-right menu & top-left HUD)
        let shoulderY = max(safeT + sc(36), 48)
        let bumperW = sc(54)
        let bumperH = sc(30)
        let bumperGap = sc(8)
        let bumperCorner = bumperH * 0.42
        let bumperFont = sc(12.5)

        addPill(.L2, "L2", x: safeL, y: shoulderY, w: bumperW, h: bumperH, corner: bumperCorner, tint: white, fontSize: bumperFont)
        addPill(.L1, "L1", x: safeL + bumperW + bumperGap, y: shoulderY, w: bumperW, h: bumperH, corner: bumperCorner, tint: white, fontSize: bumperFont)

        let r2X = size.width - safeR - bumperW
        addPill(.R1, "R1", x: r2X - bumperGap - bumperW, y: shoulderY, w: bumperW, h: bumperH, corner: bumperCorner, tint: white, fontSize: bumperFont)
        addPill(.R2, "R2", x: r2X, y: shoulderY, w: bumperW, h: bumperH, corner: bumperCorner, tint: white, fontSize: bumperFont)

        // Top-center compact utility row
        let utilH = sc(25)
        let utilY = safeT + sc(2)
        let utilGap = sc(6)
        let utilCorner = utilH * 0.45
        let utilFont = sc(9.5)

        let shareW = sc(48)
        let l3W = sc(34)
        let padW = sc(52)
        let r3W = sc(34)
        let optW = sc(56)
        let totalUtilW = shareW + l3W + padW + r3W + optW + utilGap * 4
        var ux = (size.width - totalUtilW) * 0.5

        addPill(.share, "SHARE", x: ux, y: utilY, w: shareW, h: utilH, corner: utilCorner, tint: muted, fontSize: utilFont)
        ux += shareW + utilGap
        addPill(.L3, "L3", x: ux, y: utilY, w: l3W, h: utilH, corner: utilCorner, tint: muted, fontSize: utilFont)
        ux += l3W + utilGap
        buttons.append(GamepadButton(
            id: .touchpad,
            glyph: .touchpad,
            rect: CGRect(x: ux, y: utilY, width: padW, height: utilH),
            isCircle: false,
            isFace: false,
            isDpad: false,
            cornerRadius: utilCorner,
            tint: muted
        ))
        ux += padW + utilGap
        addPill(.R3, "R3", x: ux, y: utilY, w: r3W, h: utilH, corner: utilCorner, tint: muted, fontSize: utilFont)
        ux += r3W + utilGap
        addPill(.options, "OPTIONS", x: ux, y: utilY, w: optW, h: utilH, corner: utilCorner, tint: muted, fontSize: utilFont)

        let dpadBtnR = sc(22)
        let dpadSpread = sc(38)
        dpadCenter = CGPoint(x: safeL + dpadSpread + dpadBtnR + sc(4), y: size.height - safeB - sc(108))
        dpadZoneRadius = dpadSpread + dpadBtnR + sc(12)
        dpadDeadzone = sc(11)

        addCircle(.dpadUp, .up, cx: dpadCenter.x, cy: dpadCenter.y - dpadSpread, r: dpadBtnR, tint: white, isDpad: true)
        addCircle(.dpadDown, .down, cx: dpadCenter.x, cy: dpadCenter.y + dpadSpread, r: dpadBtnR, tint: white, isDpad: true)
        addCircle(.dpadLeft, .left, cx: dpadCenter.x - dpadSpread, cy: dpadCenter.y, r: dpadBtnR, tint: white, isDpad: true)
        addCircle(.dpadRight, .right, cx: dpadCenter.x + dpadSpread, cy: dpadCenter.y, r: dpadBtnR, tint: white, isDpad: true)

        let faceBtnR = sc(23)
        let faceSpread = sc(41)
        let faceCenter = CGPoint(x: size.width - safeR - faceSpread - faceBtnR - sc(4), y: dpadCenter.y)

        let green = CGColor(red: 74 / 255, green: 222 / 255, blue: 128 / 255, alpha: 1)
        let blue = CGColor(red: 96 / 255, green: 165 / 255, blue: 250 / 255, alpha: 1)
        let pink = CGColor(red: 244 / 255, green: 114 / 255, blue: 182 / 255, alpha: 1)
        let red = CGColor(red: 255 / 255, green: 107 / 255, blue: 107 / 255, alpha: 1)

        addCircle(.triangle, .triangle, cx: faceCenter.x, cy: faceCenter.y - faceSpread, r: faceBtnR, tint: green, isFace: true)
        addCircle(.cross, .cross, cx: faceCenter.x, cy: faceCenter.y + faceSpread, r: faceBtnR, tint: blue, isFace: true)
        addCircle(.square, .square, cx: faceCenter.x - faceSpread, cy: faceCenter.y, r: faceBtnR, tint: pink, isFace: true)
        addCircle(.circle, .circle, cx: faceCenter.x + faceSpread, cy: faceCenter.y, r: faceBtnR, tint: red, isFace: true)

        let stickBaseR = sc(44)
        let stickKnobR = sc(21)
        let stickY = size.height - safeB - stickBaseR - sc(6)

        leftStick = GamepadStick(
            isLeft: true,
            center: CGPoint(x: dpadCenter.x + sc(106), y: stickY),
            knob: CGPoint(x: dpadCenter.x + sc(106), y: stickY),
            baseRadius: stickBaseR,
            knobRadius: stickKnobR
        )
        rightStick = GamepadStick(
            isLeft: false,
            center: CGPoint(x: faceCenter.x - sc(108), y: stickY),
            knob: CGPoint(x: faceCenter.x - sc(108), y: stickY),
            baseRadius: stickBaseR,
            knobRadius: stickKnobR
        )
    }

    private func addPill(_ id: KytyButton, _ title: String, x: CGFloat, y: CGFloat, w: CGFloat, h: CGFloat, corner: CGFloat, tint: CGColor, fontSize: CGFloat) {
        buttons.append(GamepadButton(
            id: id,
            glyph: .text(title, fontSize),
            rect: CGRect(x: x, y: y, width: w, height: h),
            isCircle: false,
            isFace: false,
            isDpad: false,
            cornerRadius: corner,
            tint: tint
        ))
    }

    private func addCircle(_ id: KytyButton, _ glyph: GamepadGlyph, cx: CGFloat, cy: CGFloat, r: CGFloat, tint: CGColor, isFace: Bool = false, isDpad: Bool = false) {
        buttons.append(GamepadButton(
            id: id,
            glyph: glyph,
            rect: CGRect(x: cx - r, y: cy - r, width: r * 2, height: r * 2),
            isCircle: true,
            isFace: isFace,
            isDpad: isDpad,
            cornerRadius: r,
            tint: tint
        ))
    }

    func setButton(at index: Int, pressed: Bool) {
        guard buttons.indices.contains(index), buttons[index].pressed != pressed else { return }
        buttons[index].pressed = pressed
        let id = buttons[index].id
        KytyBridge.setButton(id, pressed: pressed)
        if id == .L2 || id == .R2 {
            if id == .L2 { l2Pressed = pressed }
            if id == .R2 { r2Pressed = pressed }
            KytyBridge.setTriggerL2(l2Pressed ? 1.0 : 0.0, r2: r2Pressed ? 1.0 : 0.0)
        }
    }

    @discardableResult
    func updateDpad(at point: CGPoint) -> Bool {
        let dx = point.x - dpadCenter.x
        let dy = point.y - dpadCenter.y
        let dist = hypot(dx, dy)

        var up = false, down = false, left = false, right = false
        if dist >= dpadDeadzone {
            let deg = atan2(dy, dx) * 180.0 / .pi
            right = abs(deg) <= 67.5
            left = abs(deg) >= 112.5
            down = deg >= 22.5 && deg <= 157.5
            up = deg <= -22.5 && deg >= -157.5
        }

        var newlyPressed = false
        for i in buttons.indices where buttons[i].isDpad {
            let want: Bool
            switch buttons[i].id {
            case .dpadUp: want = up
            case .dpadDown: want = down
            case .dpadLeft: want = left
            case .dpadRight: want = right
            default: want = false
            }
            if buttons[i].pressed != want {
                buttons[i].pressed = want
                KytyBridge.setButton(buttons[i].id, pressed: want)
                if want { newlyPressed = true }
            }
        }
        return newlyPressed
    }

    func clearDpad() {
        for i in buttons.indices where buttons[i].isDpad && buttons[i].pressed {
            buttons[i].pressed = false
            KytyBridge.setButton(buttons[i].id, pressed: false)
        }
    }

    func updateStick(isLeft: Bool, point: CGPoint) {
        var stick = isLeft ? leftStick : rightStick
        let dx = point.x - stick.center.x
        let dy = point.y - stick.center.y
        let dist = hypot(dx, dy)
        let maxR = stick.baseRadius
        let clamped = min(dist, maxR)
        let angle = atan2(dy, dx)

        stick.active = true
        stick.knob = CGPoint(x: stick.center.x + clamped * cos(angle), y: stick.center.y + clamped * sin(angle))
        if isLeft { leftStick = stick } else { rightStick = stick }

        let deadzone = maxR * 0.09
        let nx: Float
        let ny: Float
        if dist < deadzone || maxR <= 0 {
            nx = 0; ny = 0
        } else {
            nx = max(-1, min(1, Float((stick.knob.x - stick.center.x) / maxR)))
            ny = max(-1, min(1, Float(-(stick.knob.y - stick.center.y) / maxR)))
        }
        if isLeft {
            KytyBridge.setLeftStickX(nx, y: ny)
        } else {
            KytyBridge.setRightStickX(nx, y: ny)
        }
    }

    func resetStick(isLeft: Bool) {
        if isLeft {
            leftStick.active = false
            leftStick.knob = leftStick.center
            KytyBridge.setLeftStickX(0, y: 0)
        } else {
            rightStick.active = false
            rightStick.knob = rightStick.center
            KytyBridge.setRightStickX(0, y: 0)
        }
    }

    func findButton(at point: CGPoint, slop: CGFloat, onlyFace: Bool = false) -> Int? {
        var bestIndex: Int?
        var bestDist = CGFloat.greatestFiniteMagnitude
        for (i, btn) in buttons.enumerated() {
            if btn.isDpad { continue }
            if onlyFace && !btn.isFace { continue }
            if !contains(btn, point: point, slop: slop) { continue }
            let d = hypot(point.x - btn.rect.midX, point.y - btn.rect.midY)
            if d < bestDist {
                bestDist = d
                bestIndex = i
            }
        }
        return bestIndex
    }

    func contains(_ btn: GamepadButton, point: CGPoint, slop: CGFloat) -> Bool {
        if btn.isCircle {
            return hypot(point.x - btn.rect.midX, point.y - btn.rect.midY) <= btn.rect.width * 0.5 + slop
        }
        return btn.rect.insetBy(dx: -slop, dy: -slop).contains(point)
    }

    func releaseAll() {
        for i in buttons.indices where buttons[i].pressed {
            buttons[i].pressed = false
            KytyBridge.setButton(buttons[i].id, pressed: false)
        }
        if l2Pressed || r2Pressed {
            l2Pressed = false
            r2Pressed = false
            KytyBridge.setTriggerL2(0, r2: 0)
        }
        if leftStick.active { resetStick(isLeft: true) }
        if rightStick.active { resetStick(isLeft: false) }
    }

    // MARK: - Borderless CoreGraphics Drawing

    func draw(in ctx: CGContext) {
        for btn in buttons {
            drawButton(btn, in: ctx)
        }
        drawStick(leftStick, in: ctx)
        drawStick(rightStick, in: ctx)
    }

    private func drawButton(_ btn: GamepadButton, in ctx: CGContext) {
        let cx = btn.rect.midX
        let cy = btn.rect.midY

        ctx.saveGState()
        if btn.pressed {
            ctx.translateBy(x: cx, y: cy)
            ctx.scaleBy(x: 0.92, y: 0.92)
            ctx.translateBy(x: -cx, y: -cy)
        }

        let bg = btn.pressed
            ? CGColor(red: 1, green: 1, blue: 1, alpha: 0.38)
            : CGColor(red: 18 / 255, green: 20 / 255, blue: 26 / 255, alpha: 0.38)
        ctx.setFillColor(bg)
        if btn.isCircle {
            ctx.fillEllipse(in: btn.rect)
        } else {
            let path = CGPath(roundedRect: btn.rect, cornerWidth: btn.cornerRadius, cornerHeight: btn.cornerRadius, transform: nil)
            ctx.addPath(path)
            ctx.fillPath()
        }

        ctx.setStrokeColor(btn.tint)
        ctx.setLineWidth(2.3 * uiScale)
        ctx.setLineCap(.round)
        ctx.setLineJoin(.round)

        switch btn.glyph {
        case .text(let title, let size):
            let font = NativeFont.systemFont(ofSize: size, weight: .bold)
            let attrs: [NSAttributedString.Key: Any] = [
                .font: font,
                .foregroundColor: nativeColor(from: btn.tint)
            ]
            let ns = title as NSString
            let textSize = ns.size(withAttributes: attrs)
            ns.draw(at: CGPoint(x: cx - textSize.width * 0.5, y: cy - textSize.height * 0.5), withAttributes: attrs)

        case .up:    drawChevron(in: ctx, cx: cx, cy: cy, radians: 0)
        case .right: drawChevron(in: ctx, cx: cx, cy: cy, radians: .pi * 0.5)
        case .down:  drawChevron(in: ctx, cx: cx, cy: cy, radians: .pi)
        case .left:  drawChevron(in: ctx, cx: cx, cy: cy, radians: -.pi * 0.5)

        case .triangle:
            let r = 8.5 * uiScale
            ctx.beginPath()
            ctx.move(to: CGPoint(x: cx, y: cy - r))
            ctx.addLine(to: CGPoint(x: cx + r * 0.866, y: cy + r * 0.55))
            ctx.addLine(to: CGPoint(x: cx - r * 0.866, y: cy + r * 0.55))
            ctx.closePath()
            ctx.strokePath()

        case .square:
            let s = 7.0 * uiScale
            let rect = CGRect(x: cx - s, y: cy - s, width: s * 2, height: s * 2)
            ctx.addPath(CGPath(roundedRect: rect, cornerWidth: 2 * uiScale, cornerHeight: 2 * uiScale, transform: nil))
            ctx.strokePath()

        case .circle:
            let r = 7.6 * uiScale
            ctx.strokeEllipse(in: CGRect(x: cx - r, y: cy - r, width: r * 2, height: r * 2))

        case .cross:
            let c = 6.6 * uiScale
            ctx.beginPath()
            ctx.move(to: CGPoint(x: cx - c, y: cy - c))
            ctx.addLine(to: CGPoint(x: cx + c, y: cy + c))
            ctx.move(to: CGPoint(x: cx - c, y: cy + c))
            ctx.addLine(to: CGPoint(x: cx + c, y: cy - c))
            ctx.strokePath()

        case .touchpad:
            ctx.setLineWidth(1.7 * uiScale)
            let pw = 12 * uiScale
            let ph = 6 * uiScale
            let rect = CGRect(x: cx - pw, y: cy - ph, width: pw * 2, height: ph * 2)
            ctx.addPath(CGPath(roundedRect: rect, cornerWidth: 3 * uiScale, cornerHeight: 3 * uiScale, transform: nil))
            ctx.strokePath()
        }

        ctx.restoreGState()
    }

    private func drawChevron(in ctx: CGContext, cx: CGFloat, cy: CGFloat, radians: CGFloat) {
        let arm = 6.0 * uiScale
        ctx.saveGState()
        ctx.translateBy(x: cx, y: cy)
        ctx.rotate(by: radians)
        ctx.beginPath()
        ctx.move(to: CGPoint(x: -arm, y: arm * 0.35))
        ctx.addLine(to: CGPoint(x: 0, y: -arm * 0.55))
        ctx.addLine(to: CGPoint(x: arm, y: arm * 0.35))
        ctx.strokePath()
        ctx.restoreGState()
    }

    private func drawStick(_ stick: GamepadStick, in ctx: CGContext) {
        let baseRect = CGRect(
            x: stick.center.x - stick.baseRadius,
            y: stick.center.y - stick.baseRadius,
            width: stick.baseRadius * 2,
            height: stick.baseRadius * 2
        )
        ctx.setFillColor(CGColor(red: 18 / 255, green: 20 / 255, blue: 26 / 255, alpha: 0.34))
        ctx.fillEllipse(in: baseRect)

        let dotR = 5.5 * uiScale
        ctx.setFillColor(CGColor(red: 1, green: 1, blue: 1, alpha: 0.16))
        ctx.fillEllipse(in: CGRect(x: stick.center.x - dotR, y: stick.center.y - dotR, width: dotR * 2, height: dotR * 2))

        let knobRect = CGRect(
            x: stick.knob.x - stick.knobRadius,
            y: stick.knob.y - stick.knobRadius,
            width: stick.knobRadius * 2,
            height: stick.knobRadius * 2
        )
        ctx.setFillColor(CGColor(
            red: 236 / 255,
            green: 240 / 255,
            blue: 248 / 255,
            alpha: stick.active ? 0.80 : 0.56
        ))
        ctx.fillEllipse(in: knobRect)
    }
}

// MARK: - iOS Multi-Touch Canvas

#if os(iOS)
private final class VirtualGamepadUIView: UIView {
    private let state = GamepadState()
    private let haptic = UIImpactFeedbackGenerator(style: .light)

    private enum TrackedTarget {
        case dpad
        case leftStick
        case rightStick
        case button(Int)
    }

    private var activeTouches: [ObjectIdentifier: TrackedTarget] = [:]
    private var lastLayoutSize: CGSize = .zero
    private var lastSafeInsets: UIEdgeInsets = .zero

    override init(frame: CGRect) {
        super.init(frame: frame)
        isMultipleTouchEnabled = true
        isExclusiveTouch = false
        backgroundColor = .clear
        isOpaque = false
        haptic.prepare()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        relayoutIfNeeded()
    }

    override func safeAreaInsetsDidChange() {
        super.safeAreaInsetsDidChange()
        relayoutIfNeeded()
    }

    override func willMove(toWindow newWindow: UIWindow?) {
        super.willMove(toWindow: newWindow)
        if newWindow == nil {
            releaseAll()
        }
    }

    private func relayoutIfNeeded() {
        guard bounds.size != lastLayoutSize || safeAreaInsets != lastSafeInsets else { return }
        lastLayoutSize = bounds.size
        lastSafeInsets = safeAreaInsets
        activeTouches.removeAll()
        state.layout(
            size: bounds.size,
            insets: (safeAreaInsets.left, safeAreaInsets.top, safeAreaInsets.right, safeAreaInsets.bottom)
        )
        setNeedsDisplay()
    }

    override func draw(_ rect: CGRect) {
        guard let ctx = UIGraphicsGetCurrentContext() else { return }
        state.draw(in: ctx)
    }

    private func fireHaptic() {
        if UserDefaults.standard.object(forKey: SettingsKey.enableHaptics) as? Bool ?? true {
            haptic.impactOccurred()
        }
    }

    private func isTargetInUse(_ match: (TrackedTarget) -> Bool) -> Bool {
        activeTouches.values.contains(where: match)
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            let id = ObjectIdentifier(touch)
            let p = touch.location(in: self)

            // 1. Unified 8-way D-pad
            if !isTargetInUse({ if case .dpad = $0 { return true }; return false }),
               hypot(p.x - state.dpadCenter.x, p.y - state.dpadCenter.y) <= state.dpadZoneRadius {
                activeTouches[id] = .dpad
                if state.updateDpad(at: p) { fireHaptic() }
                continue
            }

            // 2. Non-D-pad buttons
            if let idx = state.findButton(at: p, slop: 6 * state.uiScale),
               !isTargetInUse({ if case .button(let b) = $0 { return b == idx }; return false }) {
                activeTouches[id] = .button(idx)
                state.setButton(at: idx, pressed: true)
                fireHaptic()
                continue
            }

            // 3. Left / Right analog sticks
            if !isTargetInUse({ if case .leftStick = $0 { return true }; return false }),
               hypot(p.x - state.leftStick.center.x, p.y - state.leftStick.center.y) <= state.leftStick.baseRadius * 1.45 {
                activeTouches[id] = .leftStick
                state.updateStick(isLeft: true, point: p)
                fireHaptic()
                continue
            }

            if !isTargetInUse({ if case .rightStick = $0 { return true }; return false }),
               hypot(p.x - state.rightStick.center.x, p.y - state.rightStick.center.y) <= state.rightStick.baseRadius * 1.45 {
                activeTouches[id] = .rightStick
                state.updateStick(isLeft: false, point: p)
                fireHaptic()
                continue
            }
        }
        setNeedsDisplay()
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            let id = ObjectIdentifier(touch)
            guard let target = activeTouches[id] else { continue }
            let p = touch.location(in: self)

            switch target {
            case .dpad:
                if state.updateDpad(at: p) { fireHaptic() }
            case .leftStick:
                state.updateStick(isLeft: true, point: p)
            case .rightStick:
                state.updateStick(isLeft: false, point: p)
            case .button(let idx):
                if state.buttons[idx].isFace {
                    let next = state.findButton(at: p, slop: 8 * state.uiScale, onlyFace: true)
                    if next != idx {
                        state.setButton(at: idx, pressed: false)
                        if let next = next,
                           !isTargetInUse({ if case .button(let b) = $0 { return b == next }; return false }) {
                            activeTouches[id] = .button(next)
                            state.setButton(at: next, pressed: true)
                            fireHaptic()
                        } else {
                            activeTouches.removeValue(forKey: id)
                        }
                    }
                } else {
                    let inside = state.contains(state.buttons[idx], point: p, slop: 14 * state.uiScale)
                    state.setButton(at: idx, pressed: inside)
                }
            }
        }
        setNeedsDisplay()
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        endTouches(touches)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        endTouches(touches)
    }

    private func endTouches(_ touches: Set<UITouch>) {
        for touch in touches {
            let id = ObjectIdentifier(touch)
            guard let target = activeTouches.removeValue(forKey: id) else { continue }
            switch target {
            case .dpad:
                state.clearDpad()
            case .leftStick:
                state.resetStick(isLeft: true)
            case .rightStick:
                state.resetStick(isLeft: false)
            case .button(let idx):
                state.setButton(at: idx, pressed: false)
            }
        }
        setNeedsDisplay()
    }

    func releaseAll() {
        activeTouches.removeAll()
        state.releaseAll()
        setNeedsDisplay()
    }
}

private struct VirtualGamepadCanvas: UIViewRepresentable {
    func makeUIView(context: Context) -> VirtualGamepadUIView {
        VirtualGamepadUIView(frame: .zero)
    }
    func updateUIView(_ uiView: VirtualGamepadUIView, context: Context) {}
    static func dismantleUIView(_ uiView: VirtualGamepadUIView, coordinator: ()) {
        uiView.releaseAll()
    }
}

#elseif os(macOS)

// MARK: - macOS Mouse-Driven Canvas

private final class VirtualGamepadNSView: NSView {
    private let state = GamepadState()
    private var lastSize: CGSize = .zero

    private enum DragTarget {
        case dpad
        case leftStick
        case rightStick
        case button(Int)
    }
    private var activeTarget: DragTarget?

    override var isFlipped: Bool { true }

    override func layout() {
        super.layout()
        guard bounds.size != lastSize else { return }
        lastSize = bounds.size
        activeTarget = nil
        state.layout(size: bounds.size, insets: (0, 0, 0, 0))
        needsDisplay = true
    }

    override func viewWillMove(toWindow newWindow: NSWindow?) {
        super.viewWillMove(toWindow: newWindow)
        if newWindow == nil {
            releaseAll()
        }
    }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        state.draw(in: ctx)
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)

        if hypot(p.x - state.dpadCenter.x, p.y - state.dpadCenter.y) <= state.dpadZoneRadius {
            activeTarget = .dpad
            state.updateDpad(at: p)
        } else if let idx = state.findButton(at: p, slop: 6 * state.uiScale) {
            activeTarget = .button(idx)
            state.setButton(at: idx, pressed: true)
        } else if hypot(p.x - state.leftStick.center.x, p.y - state.leftStick.center.y) <= state.leftStick.baseRadius * 1.45 {
            activeTarget = .leftStick
            state.updateStick(isLeft: true, point: p)
        } else if hypot(p.x - state.rightStick.center.x, p.y - state.rightStick.center.y) <= state.rightStick.baseRadius * 1.45 {
            activeTarget = .rightStick
            state.updateStick(isLeft: false, point: p)
        } else {
            super.mouseDown(with: event)
            return
        }
        needsDisplay = true
    }

    override func mouseDragged(with event: NSEvent) {
        guard let target = activeTarget else { return }
        let p = convert(event.locationInWindow, from: nil)
        switch target {
        case .dpad:
            state.updateDpad(at: p)
        case .leftStick:
            state.updateStick(isLeft: true, point: p)
        case .rightStick:
            state.updateStick(isLeft: false, point: p)
        case .button(let idx):
            let inside = state.contains(state.buttons[idx], point: p, slop: 14 * state.uiScale)
            state.setButton(at: idx, pressed: inside)
        }
        needsDisplay = true
    }

    override func mouseUp(with event: NSEvent) {
        guard let target = activeTarget else { return }
        activeTarget = nil
        switch target {
        case .dpad: state.clearDpad()
        case .leftStick: state.resetStick(isLeft: true)
        case .rightStick: state.resetStick(isLeft: false)
        case .button(let idx): state.setButton(at: idx, pressed: false)
        }
        needsDisplay = true
    }

    func releaseAll() {
        activeTarget = nil
        state.releaseAll()
        needsDisplay = true
    }
}

private struct VirtualGamepadCanvas: NSViewRepresentable {
    func makeNSView(context: Context) -> VirtualGamepadNSView {
        VirtualGamepadNSView(frame: .zero)
    }
    func updateNSView(_ nsView: VirtualGamepadNSView, context: Context) {}
    static func dismantleNSView(_ nsView: VirtualGamepadNSView, coordinator: ()) {
        nsView.releaseAll()
    }
}
#endif

// MARK: - Physical Game Controller Manager

@MainActor
public final class PhysicalControllerManager: ObservableObject {
    public static let shared = PhysicalControllerManager()
    @Published public var hasPhysicalController: Bool = false
    private var physicalControllers: Set<GCController> = []

    private init() {
        setupObservers()
        checkControllers()
    }

    private func setupObservers() {
        NotificationCenter.default.addObserver(
            forName: .GCControllerDidConnect,
            object: nil,
            queue: .main
        ) { [weak self] notif in
            guard let self = self, let controller = notif.object as? GCController else { return }
            self.physicalControllers.insert(controller)
            self.hasPhysicalController = true
            self.setupGamepad(controller)
        }

        NotificationCenter.default.addObserver(
            forName: .GCControllerDidDisconnect,
            object: nil,
            queue: .main
        ) { [weak self] notif in
            guard let self = self, let controller = notif.object as? GCController else { return }
            self.physicalControllers.remove(controller)
            self.hasPhysicalController = !self.physicalControllers.isEmpty
        }
    }

    private func checkControllers() {
        let controllers = GCController.controllers()
        hasPhysicalController = !controllers.isEmpty
        for controller in controllers {
            physicalControllers.insert(controller)
            setupGamepad(controller)
        }
    }

    private func setupGamepad(_ controller: GCController) {
        guard let gamepad = controller.extendedGamepad else { return }

        gamepad.leftThumbstick.valueChangedHandler = { _, x, y in
            KytyBridge.setLeftStickX(x, y: y)
        }
        gamepad.rightThumbstick.valueChangedHandler = { _, x, y in
            KytyBridge.setRightStickX(x, y: y)
        }
        gamepad.buttonA.pressedChangedHandler = { _, _, pressed in
            KytyBridge.setButton(.cross, pressed: pressed)
        }
        gamepad.buttonB.pressedChangedHandler = { _, _, pressed in
            KytyBridge.setButton(.circle, pressed: pressed)
        }
        gamepad.buttonX.pressedChangedHandler = { _, _, pressed in
            KytyBridge.setButton(.square, pressed: pressed)
        }
        gamepad.buttonY.pressedChangedHandler = { _, _, pressed in
            KytyBridge.setButton(.triangle, pressed: pressed)
        }
        gamepad.leftShoulder.pressedChangedHandler = { _, _, pressed in
            KytyBridge.setButton(.L1, pressed: pressed)
        }
        gamepad.rightShoulder.pressedChangedHandler = { _, _, pressed in
            KytyBridge.setButton(.R1, pressed: pressed)
        }
        gamepad.leftTrigger.valueChangedHandler = { _, val, pressed in
            KytyBridge.setTriggerL2(val, r2: gamepad.rightTrigger.value)
            KytyBridge.setButton(.L2, pressed: pressed)
        }
        gamepad.rightTrigger.valueChangedHandler = { _, val, pressed in
            KytyBridge.setTriggerL2(gamepad.leftTrigger.value, r2: val)
            KytyBridge.setButton(.R2, pressed: pressed)
        }
        gamepad.dpad.up.pressedChangedHandler = { _, _, pressed in
            KytyBridge.setButton(.dpadUp, pressed: pressed)
        }
        gamepad.dpad.down.pressedChangedHandler = { _, _, pressed in
            KytyBridge.setButton(.dpadDown, pressed: pressed)
        }
        gamepad.dpad.left.pressedChangedHandler = { _, _, pressed in
            KytyBridge.setButton(.dpadLeft, pressed: pressed)
        }
        gamepad.dpad.right.pressedChangedHandler = { _, _, pressed in
            KytyBridge.setButton(.dpadRight, pressed: pressed)
        }
        gamepad.leftThumbstickButton?.pressedChangedHandler = { _, _, pressed in
            KytyBridge.setButton(.L3, pressed: pressed)
        }
        gamepad.rightThumbstickButton?.pressedChangedHandler = { _, _, pressed in
            KytyBridge.setButton(.R3, pressed: pressed)
        }
        gamepad.buttonMenu.pressedChangedHandler = { _, _, pressed in
            KytyBridge.setButton(.options, pressed: pressed)
        }
        gamepad.buttonOptions?.pressedChangedHandler = { _, _, pressed in
            KytyBridge.setButton(.share, pressed: pressed)
        }
        if #available(iOS 14.0, macOS 11.0, *) {
            gamepad.buttonHome?.pressedChangedHandler = { _, _, pressed in
                KytyBridge.setButton(.PS, pressed: pressed)
            }
        }
    }
}

// MARK: - On-Screen Touch Controls Overlay

struct TouchControlsView: View {
    var body: some View {
        VirtualGamepadCanvas()
            .edgesIgnoringSafeArea(.all)
    }
}
