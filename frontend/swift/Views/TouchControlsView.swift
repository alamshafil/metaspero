// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI
import GameController

#if os(iOS)
import UIKit
import UIKit.UIGestureRecognizerSubclass

typealias PlatformColor = UIColor
typealias PlatformFont = UIFont

// MARK: - Multi-Touch Button (UIKit-backed)

final class TouchButtonUIView: UIView {
    var onStateChanged: ((Bool) -> Void)?
    var isPressed: Bool = false {
        didSet {
            if oldValue != isPressed {
                updateVisualState()
                onStateChanged?(isPressed)
            }
        }
    }
    
    private let titleLabel = UILabel()
    private let iconImageView = UIImageView()
    private let haptic = UIImpactFeedbackGenerator(style: .light)
    private var trackingTouch: UITouch?
    
    var normalBgColor = UIColor.black.withAlphaComponent(0.38)
    var pressedBgColor = UIColor.white.withAlphaComponent(0.40)
    var isCircle: Bool = true
    var cornerRadius: CGFloat = 8.0
    
    init(
        title: String? = nil,
        systemImage: String? = nil,
        symbolColor: UIColor = .white,
        font: UIFont = .systemFont(ofSize: 17, weight: .bold),
        cornerRadius: CGFloat = 8.0,
        isCircle: Bool = false
    ) {
        self.isCircle = isCircle
        self.cornerRadius = cornerRadius
        super.init(frame: .zero)
        
        isMultipleTouchEnabled = true
        isExclusiveTouch = false
        backgroundColor = normalBgColor
        layer.masksToBounds = true
        layer.borderWidth = 0.0
        
        if let title = title {
            titleLabel.text = title
            titleLabel.textColor = symbolColor
            titleLabel.font = font
            titleLabel.textAlignment = .center
            titleLabel.translatesAutoresizingMaskIntoConstraints = false
            addSubview(titleLabel)
            NSLayoutConstraint.activate([
                titleLabel.centerXAnchor.constraint(equalTo: centerXAnchor),
                titleLabel.centerYAnchor.constraint(equalTo: centerYAnchor)
            ])
        }
        
        if let systemImage = systemImage {
            let config = UIImage.SymbolConfiguration(font: font)
            iconImageView.image = UIImage(systemName: systemImage, withConfiguration: config)
            iconImageView.tintColor = symbolColor
            iconImageView.contentMode = .scaleAspectFit
            iconImageView.translatesAutoresizingMaskIntoConstraints = false
            addSubview(iconImageView)
            NSLayoutConstraint.activate([
                iconImageView.centerXAnchor.constraint(equalTo: centerXAnchor),
                iconImageView.centerYAnchor.constraint(equalTo: centerYAnchor)
            ])
        }
        
        haptic.prepare()
    }
    
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }
    
    override func layoutSubviews() {
        super.layoutSubviews()
        layer.cornerRadius = isCircle ? bounds.width / 2.0 : cornerRadius
    }
    
    private func updateVisualState() {
        UIView.animate(withDuration: 0.05, delay: 0, options: [.beginFromCurrentState, .allowUserInteraction]) {
            self.backgroundColor = self.isPressed ? self.pressedBgColor : self.normalBgColor
            self.transform = self.isPressed ? CGAffineTransform(scaleX: 0.93, y: 0.93) : .identity
        }
    }
    
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard trackingTouch == nil, let touch = touches.first else { return }
        trackingTouch = touch
        haptic.impactOccurred()
        isPressed = true
    }
    
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = trackingTouch, touches.contains(touch) else { return }
        let loc = touch.location(in: self)
        let expanded = bounds.insetBy(dx: -15, dy: -15)
        if !expanded.contains(loc) && isPressed {
            isPressed = false
        } else if expanded.contains(loc) && !isPressed {
            isPressed = true
        }
    }
    
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = trackingTouch, touches.contains(touch) else { return }
        trackingTouch = nil
        isPressed = false
    }
    
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = trackingTouch, touches.contains(touch) else { return }
        trackingTouch = nil
        isPressed = false
    }
}

struct NativeTouchButton: UIViewRepresentable {
    let title: String?
    let systemImage: String?
    let symbolColor: PlatformColor
    let font: PlatformFont
    let cornerRadius: CGFloat
    let isCircle: Bool
    let onPressChanged: (Bool) -> Void
    
    init(
        title: String? = nil,
        systemImage: String? = nil,
        symbolColor: PlatformColor = .white,
        font: PlatformFont = .systemFont(ofSize: 16, weight: .bold),
        cornerRadius: CGFloat = 8.0,
        isCircle: Bool = false,
        onPressChanged: @escaping (Bool) -> Void
    ) {
        self.title = title
        self.systemImage = systemImage
        self.symbolColor = symbolColor
        self.font = font
        self.cornerRadius = cornerRadius
        self.isCircle = isCircle
        self.onPressChanged = onPressChanged
    }
    
    func makeUIView(context: Context) -> TouchButtonUIView {
        let view = TouchButtonUIView(
            title: title,
            systemImage: systemImage,
            symbolColor: symbolColor,
            font: font,
            cornerRadius: cornerRadius,
            isCircle: isCircle
        )
        view.onStateChanged = onPressChanged
        return view
    }
    
    func updateUIView(_ uiView: TouchButtonUIView, context: Context) {
        uiView.onStateChanged = onPressChanged
    }
}

// MARK: - Native Multi-Touch Thumbstick

final class ImmediatePanGestureRecognizer: UIPanGestureRecognizer {
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent) {
        super.touchesBegan(touches, with: event)
        state = .began
    }
}

final class ThumbstickUIView: UIView, UIGestureRecognizerDelegate {
    var onAxisChanged: ((Float, Float) -> Void)?
    var onStickButtonChanged: ((Bool) -> Void)?
    
    private var panGesture: ImmediatePanGestureRecognizer!
    private var centerPoint: CGPoint = .zero
    private let baseRadius: CGFloat = 42.0
    private let knobRadius: CGFloat = 20.0
    
    private let baseView = UIView()
    private let centerDotView = UIView()
    private let knobView = UIView()
    
    override init(frame: CGRect) {
        super.init(frame: frame)
        isMultipleTouchEnabled = true
        isExclusiveTouch = false
        backgroundColor = .clear
        
        baseView.backgroundColor = UIColor.black.withAlphaComponent(0.35)
        baseView.layer.masksToBounds = true
        baseView.layer.borderWidth = 0.0
        addSubview(baseView)
        
        // Center idle indicator dot
        centerDotView.backgroundColor = UIColor.white.withAlphaComponent(0.18)
        centerDotView.layer.masksToBounds = true
        addSubview(centerDotView)
        
        // Knob: Smooth light translucent circle
        knobView.backgroundColor = UIColor(white: 0.88, alpha: 0.65)
        knobView.layer.masksToBounds = false
        knobView.layer.shadowColor = UIColor.black.cgColor
        knobView.layer.shadowOpacity = 0.35
        knobView.layer.shadowRadius = 4.0
        knobView.layer.shadowOffset = CGSize(width: 0, height: 2)
        knobView.layer.borderWidth = 0.0
        addSubview(knobView)
        
        panGesture = ImmediatePanGestureRecognizer(target: self, action: #selector(handlePan(_:)))
        panGesture.maximumNumberOfTouches = 1
        panGesture.cancelsTouchesInView = false
        panGesture.delaysTouchesBegan = false
        panGesture.delaysTouchesEnded = false
        panGesture.delegate = self
        addGestureRecognizer(panGesture)
    }
    
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }
    
    override func didMoveToWindow() {
        super.didMoveToWindow()
        disableParentGestureCancellation()
    }
    
    private func disableParentGestureCancellation() {
        var v: UIView? = self
        while let cur = v {
            cur.gestureRecognizers?.forEach { recognizer in
                if recognizer !== self.panGesture {
                    recognizer.cancelsTouchesInView = false
                }
            }
            v = cur.superview
        }
        window?.gestureRecognizers?.forEach { recognizer in
            if recognizer !== self.panGesture {
                recognizer.cancelsTouchesInView = false
            }
        }
    }
    
    override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
        // While actively tracking a thumb, capture touches anywhere across the screen
        if panGesture.state == .began || panGesture.state == .changed {
            return true
        }
        return bounds.contains(point)
    }
    
    override func layoutSubviews() {
        super.layoutSubviews()
        centerPoint = CGPoint(x: bounds.midX, y: bounds.midY)
        
        baseView.frame = CGRect(
            x: centerPoint.x - baseRadius,
            y: centerPoint.y - baseRadius,
            width: baseRadius * 2,
            height: baseRadius * 2
        )
        baseView.layer.cornerRadius = baseRadius
        
        centerDotView.frame = CGRect(
            x: centerPoint.x - 7,
            y: centerPoint.y - 7,
            width: 14,
            height: 14
        )
        centerDotView.layer.cornerRadius = 7
        
        if panGesture.state != .began && panGesture.state != .changed {
            knobView.frame = CGRect(
                x: centerPoint.x - knobRadius,
                y: centerPoint.y - knobRadius,
                width: knobRadius * 2,
                height: knobRadius * 2
            )
            knobView.layer.cornerRadius = knobRadius
        }
    }
    
    @objc private func handlePan(_ pan: UIPanGestureRecognizer) {
        switch pan.state {
        case .began, .changed:
            let loc = pan.location(in: self)
            let dx = loc.x - centerPoint.x
            let dy = loc.y - centerPoint.y
            let distance = hypot(dx, dy)
            
            let clampedDist = min(distance, baseRadius)
            let angle = atan2(dy, dx)
            let clampedX = clampedDist * cos(angle)
            let clampedY = clampedDist * sin(angle)
            
            // Move knob directly to clamped position
            knobView.center = CGPoint(x: centerPoint.x + clampedX, y: centerPoint.y + clampedY)
            
            // Deadzone check
            if distance < 3.5 {
                onAxisChanged?(0, 0)
            } else {
                let normX = max(-1.0, min(1.0, Float(clampedX / baseRadius)))
                let normY = max(-1.0, min(1.0, Float(-clampedY / baseRadius))) // Controller Y-up convention
                onAxisChanged?(normX, normY)
            }
            
        case .ended, .cancelled, .failed:
            reset()
            
        default:
            break
        }
    }
    
    private func reset() {
        knobView.center = centerPoint
        onAxisChanged?(0, 0)
    }
    
    // MARK: - UIGestureRecognizerDelegate
    
    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer) -> Bool {
        return true
    }
    
    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer, shouldRequireFailureOf otherGestureRecognizer: UIGestureRecognizer) -> Bool {
        return false
    }
    
    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer, shouldBeRequiredToFailBy otherGestureRecognizer: UIGestureRecognizer) -> Bool {
        return false
    }
}

struct NativeThumbstick: UIViewRepresentable {
    let onChange: (Float, Float) -> Void
    
    func makeUIView(context: Context) -> ThumbstickUIView {
        let view = ThumbstickUIView()
        view.onAxisChanged = onChange
        return view
    }
    
    func updateUIView(_ uiView: ThumbstickUIView, context: Context) {
        uiView.onAxisChanged = onChange
    }
}
#elseif os(macOS)
import AppKit

typealias PlatformColor = NSColor
typealias PlatformFont = NSFont

// MARK: - Native macOS Press Button

final class TouchButtonNSView: NSView {
    var onStateChanged: ((Bool) -> Void)?
    var isPressed: Bool = false {
        didSet {
            if oldValue != isPressed {
                updateVisualState()
                onStateChanged?(isPressed)
            }
        }
    }
    
    private let titleLabel = NSTextField(labelWithString: "")
    private let iconImageView = NSImageView()
    var normalBgColor = NSColor.black.withAlphaComponent(0.45)
    var pressedBgColor = NSColor.white.withAlphaComponent(0.42)
    var isCircle: Bool = false
    var cornerRadius: CGFloat = 8.0
    
    init(
        title: String? = nil,
        systemImage: String? = nil,
        symbolColor: NSColor = .white,
        font: NSFont = .boldSystemFont(ofSize: 15),
        cornerRadius: CGFloat = 8.0,
        isCircle: Bool = false
    ) {
        self.isCircle = isCircle
        self.cornerRadius = cornerRadius
        super.init(frame: .zero)
        
        wantsLayer = true
        layer?.masksToBounds = true
        layer?.backgroundColor = normalBgColor.cgColor
        layer?.borderWidth = 1.0
        layer?.borderColor = NSColor.white.withAlphaComponent(0.25).cgColor
        
        if let title = title {
            titleLabel.stringValue = title
            titleLabel.textColor = symbolColor
            titleLabel.font = font
            titleLabel.alignment = .center
            titleLabel.isBezeled = false
            titleLabel.drawsBackground = false
            titleLabel.isEditable = false
            titleLabel.isSelectable = false
            titleLabel.translatesAutoresizingMaskIntoConstraints = false
            addSubview(titleLabel)
            NSLayoutConstraint.activate([
                titleLabel.centerXAnchor.constraint(equalTo: centerXAnchor),
                titleLabel.centerYAnchor.constraint(equalTo: centerYAnchor)
            ])
        }
        
        if let systemImage = systemImage {
            if let image = NSImage(systemSymbolName: systemImage, accessibilityDescription: nil) {
                let config = NSImage.SymbolConfiguration(pointSize: font.pointSize, weight: .bold)
                iconImageView.image = image.withSymbolConfiguration(config)
                iconImageView.contentTintColor = symbolColor
            }
            iconImageView.imageScaling = .scaleProportionallyDown
            iconImageView.translatesAutoresizingMaskIntoConstraints = false
            addSubview(iconImageView)
            NSLayoutConstraint.activate([
                iconImageView.centerXAnchor.constraint(equalTo: centerXAnchor),
                iconImageView.centerYAnchor.constraint(equalTo: centerYAnchor)
            ])
        }
    }
    
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }
    
    override func layout() {
        super.layout()
        layer?.cornerRadius = isCircle ? bounds.width / 2.0 : cornerRadius
    }
    
    private func updateVisualState() {
        CATransaction.begin()
        CATransaction.setAnimationDuration(0.05)
        layer?.backgroundColor = isPressed ? pressedBgColor.cgColor : normalBgColor.cgColor
        let scale: CGFloat = isPressed ? 0.92 : 1.0
        layer?.transform = CATransform3DMakeScale(scale, scale, 1.0)
        CATransaction.commit()
    }
    
    override func mouseDown(with event: NSEvent) {
        isPressed = true
        guard let window = self.window else { return }
        
        window.trackEvents(matching: [.leftMouseDragged, .leftMouseUp], timeout: NSEvent.foreverDuration, mode: .eventTrackingRunLoopMode) { [weak self] trackEvent, stop in
            guard let self = self, let trackEvent = trackEvent else { return }
            if trackEvent.type == .leftMouseUp {
                self.isPressed = false
                stop.pointee = true
            } else if trackEvent.type == .leftMouseDragged {
                let loc = self.convert(trackEvent.locationInWindow, from: nil)
                let inside = self.bounds.insetBy(dx: -15, dy: -15).contains(loc)
                if inside != self.isPressed {
                    self.isPressed = inside
                }
            }
        }
    }
    
    override func mouseUp(with event: NSEvent) {
        isPressed = false
    }
}

struct NativeTouchButton: NSViewRepresentable {
    let title: String?
    let systemImage: String?
    let symbolColor: PlatformColor
    let font: PlatformFont
    let cornerRadius: CGFloat
    let isCircle: Bool
    let onPressChanged: (Bool) -> Void
    
    init(
        title: String? = nil,
        systemImage: String? = nil,
        symbolColor: PlatformColor = .white,
        font: PlatformFont = .boldSystemFont(ofSize: 15),
        cornerRadius: CGFloat = 8.0,
        isCircle: Bool = false,
        onPressChanged: @escaping (Bool) -> Void
    ) {
        self.title = title
        self.systemImage = systemImage
        self.symbolColor = symbolColor
        self.font = font
        self.cornerRadius = cornerRadius
        self.isCircle = isCircle
        self.onPressChanged = onPressChanged
    }
    
    func makeNSView(context: Context) -> TouchButtonNSView {
        let view = TouchButtonNSView(
            title: title,
            systemImage: systemImage,
            symbolColor: symbolColor,
            font: font,
            cornerRadius: cornerRadius,
            isCircle: isCircle
        )
        view.onStateChanged = onPressChanged
        return view
    }
    
    func updateNSView(_ nsView: TouchButtonNSView, context: Context) {
        nsView.onStateChanged = onPressChanged
    }
}

// MARK: - Native macOS Thumbstick

final class ThumbstickNSView: NSView {
    var onChange: ((Float, Float) -> Void)?
    private let radius: CGFloat = 50.0
    private let knobRadius: CGFloat = 24.0
    private var knobLayer = CALayer()
    
    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        setupLayers()
    }
    
    required init?(coder: NSCoder) {
        super.init(coder: coder)
        wantsLayer = true
        setupLayers()
    }
    
    private func setupLayers() {
        guard let layer = self.layer else { return }
        layer.backgroundColor = NSColor.black.withAlphaComponent(0.40).cgColor
        layer.borderColor = NSColor.white.withAlphaComponent(0.20).cgColor
        layer.borderWidth = 1.5
        layer.cornerRadius = radius
        layer.masksToBounds = true
        
        knobLayer.backgroundColor = NSColor.white.withAlphaComponent(0.70).cgColor
        knobLayer.borderColor = NSColor.gray.withAlphaComponent(0.50).cgColor
        knobLayer.borderWidth = 1.0
        knobLayer.cornerRadius = knobRadius
        knobLayer.shadowColor = NSColor.black.cgColor
        knobLayer.shadowOpacity = 0.5
        knobLayer.shadowRadius = 3
        knobLayer.shadowOffset = CGSize(width: 0, height: -2)
        layer.addSublayer(knobLayer)
    }
    
    override func layout() {
        super.layout()
        layer?.cornerRadius = bounds.width / 2.0
        centerKnob(animated: false)
    }
    
    private func centerKnob(animated: Bool) {
        let center = CGPoint(x: bounds.midX - knobRadius, y: bounds.midY - knobRadius)
        if animated {
            CATransaction.begin()
            CATransaction.setAnimationDuration(0.15)
            knobLayer.frame = CGRect(origin: center, size: CGSize(width: knobRadius * 2, height: knobRadius * 2))
            CATransaction.commit()
        } else {
            CATransaction.begin()
            CATransaction.setDisableActions(true)
            knobLayer.frame = CGRect(origin: center, size: CGSize(width: knobRadius * 2, height: knobRadius * 2))
            CATransaction.commit()
        }
    }
    
    override func mouseDown(with event: NSEvent) {
        handleDrag(event: event)
        guard let window = self.window else { return }
        
        window.trackEvents(matching: [.leftMouseDragged, .leftMouseUp], timeout: NSEvent.foreverDuration, mode: .eventTrackingRunLoopMode) { [weak self] trackEvent, stop in
            guard let self = self, let trackEvent = trackEvent else { return }
            if trackEvent.type == .leftMouseUp {
                self.centerKnob(animated: true)
                self.onChange?(0.0, 0.0)
                stop.pointee = true
            } else if trackEvent.type == .leftMouseDragged {
                self.handleDrag(event: trackEvent)
            }
        }
    }
    
    private func handleDrag(event: NSEvent) {
        let loc = convert(event.locationInWindow, from: nil)
        let center = CGPoint(x: bounds.midX, y: bounds.midY)
        let dx = loc.x - center.x
        let dy = loc.y - center.y
        let distance = sqrt(dx * dx + dy * dy)
        let clamped = min(distance, radius)
        let angle = atan2(dy, dx)
        let targetX = clamped * cos(angle)
        let targetY = clamped * sin(angle)
        
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        knobLayer.frame = CGRect(
            x: center.x + targetX - knobRadius,
            y: center.y + targetY - knobRadius,
            width: knobRadius * 2,
            height: knobRadius * 2
        )
        CATransaction.commit()
        
        onChange?(Float(targetX / radius), Float(targetY / radius))
    }
    
    override func mouseUp(with event: NSEvent) {
        centerKnob(animated: true)
        onChange?(0.0, 0.0)
    }
}

struct NativeThumbstick: NSViewRepresentable {
    let onChange: (Float, Float) -> Void
    
    func makeNSView(context: Context) -> ThumbstickNSView {
        let view = ThumbstickNSView(frame: NSRect(x: 0, y: 0, width: 100, height: 100))
        view.onChange = onChange
        return view
    }
    
    func updateNSView(_ nsView: ThumbstickNSView, context: Context) {
        nsView.onChange = onChange
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
            NSLog("[PhysicalController] Connected: %@", controller.vendorName ?? "Gamepad")
        }
        
        NotificationCenter.default.addObserver(
            forName: .GCControllerDidDisconnect,
            object: nil,
            queue: .main
        ) { [weak self] notif in
            guard let self = self, let controller = notif.object as? GCController else { return }
            self.physicalControllers.remove(controller)
            self.hasPhysicalController = !self.physicalControllers.isEmpty
            NSLog("[PhysicalController] Disconnected: %@", controller.vendorName ?? "Gamepad")
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

// MARK: - Customizable On-Screen Touch Controls

struct TouchControlsView: View {
    @ObservedObject private var physicalManager = PhysicalControllerManager.shared
    
    var body: some View {
        GeometryReader { geo in
            let safeLeft = max(geo.safeAreaInsets.leading, 58)
            let safeRight = max(geo.safeAreaInsets.trailing, 58)
            
            ZStack {
                VStack {
                    // Top shoulder row, positioned below top HUD bar
                    HStack(alignment: .top) {
                        // Left bumpers (L1 & L2)
                        HStack(spacing: 8) {
                            NativeTouchButton(
                                title: "L2",
                                symbolColor: .white,
                                font: .systemFont(ofSize: 13, weight: .bold),
                                cornerRadius: 8
                            ) { pressed in
                                KytyBridge.setTriggerL2(pressed ? 1.0 : 0.0, r2: 0.0)
                                KytyBridge.setButton(.L2, pressed: pressed)
                            }
                            .frame(width: 50, height: 30)
                            
                            NativeTouchButton(
                                title: "L1",
                                symbolColor: .white,
                                font: .systemFont(ofSize: 13, weight: .bold),
                                cornerRadius: 8
                            ) { pressed in
                                KytyBridge.setButton(.L1, pressed: pressed)
                            }
                            .frame(width: 50, height: 30)
                        }
                        .padding(.leading, safeLeft)
                        
                        Spacer()
                        
                        // Center utility buttons (SHARE, L3, Touchpad, R3, OPTIONS)
                        HStack(spacing: 8) {
                            NativeTouchButton(
                                title: "SHARE",
                                symbolColor: PlatformColor.white.withAlphaComponent(0.85),
                                font: .systemFont(ofSize: 10, weight: .bold),
                                cornerRadius: 6
                            ) { pressed in
                                KytyBridge.setButton(.share, pressed: pressed)
                            }
                            .frame(width: 52, height: 26)
                            
                            NativeTouchButton(
                                title: "L3",
                                symbolColor: PlatformColor.white.withAlphaComponent(0.85),
                                font: .systemFont(ofSize: 10, weight: .bold),
                                cornerRadius: 6
                            ) { pressed in
                                KytyBridge.setButton(.L3, pressed: pressed)
                            }
                            .frame(width: 36, height: 26)
                            
                            NativeTouchButton(
                                systemImage: "rectangle.inset.filled",
                                symbolColor: PlatformColor.white.withAlphaComponent(0.6),
                                font: .systemFont(ofSize: 13),
                                cornerRadius: 6
                            ) { pressed in
                                KytyBridge.setButton(.touchpad, pressed: pressed)
                            }
                            .frame(width: 54, height: 26)
                            
                            NativeTouchButton(
                                title: "R3",
                                symbolColor: PlatformColor.white.withAlphaComponent(0.85),
                                font: .systemFont(ofSize: 10, weight: .bold),
                                cornerRadius: 6
                            ) { pressed in
                                KytyBridge.setButton(.R3, pressed: pressed)
                            }
                            .frame(width: 36, height: 26)
                            
                            NativeTouchButton(
                                title: "OPTIONS",
                                symbolColor: PlatformColor.white.withAlphaComponent(0.85),
                                font: .systemFont(ofSize: 10, weight: .bold),
                                cornerRadius: 6
                            ) { pressed in
                                KytyBridge.setButton(.options, pressed: pressed)
                            }
                            .frame(width: 58, height: 26)
                        }
                        
                        Spacer()
                        
                        // Right bumpers (R1 & R2)
                        HStack(spacing: 8) {
                            NativeTouchButton(
                                title: "R1",
                                symbolColor: .white,
                                font: .systemFont(ofSize: 13, weight: .bold),
                                cornerRadius: 8
                            ) { pressed in
                                KytyBridge.setButton(.R1, pressed: pressed)
                            }
                            .frame(width: 50, height: 30)
                            
                            NativeTouchButton(
                                title: "R2",
                                symbolColor: .white,
                                font: .systemFont(ofSize: 13, weight: .bold),
                                cornerRadius: 8
                            ) { pressed in
                                KytyBridge.setTriggerL2(0.0, r2: pressed ? 1.0 : 0.0)
                                KytyBridge.setButton(.R2, pressed: pressed)
                            }
                            .frame(width: 50, height: 30)
                        }
                        .padding(.trailing, safeRight)
                    }
                    .padding(.top, 50)
                    
                    Spacer()
                    
                    // Bottom controls
                    HStack(alignment: .bottom) {
                        // Left side: D-Pad & Left Stick
                        VStack(spacing: 16) {
                            DPadNativeView()
                            
                            NativeThumbstick { x, y in
                                KytyBridge.setLeftStickX(x, y: y)
                            }
                            .frame(width: 130, height: 130)
                        }
                        .padding(.leading, safeLeft)
                        .padding(.bottom, 16)
                        
                        Spacer()
                        
                        // PS Home Button in bottom center
                        NativeTouchButton(
                            systemImage: "circle.circle.fill",
                            symbolColor: PlatformColor.white.withAlphaComponent(0.6),
                            font: .systemFont(ofSize: 28),
                            cornerRadius: 20,
                            isCircle: true
                        ) { pressed in
                            KytyBridge.setButton(.PS, pressed: pressed)
                        }
                        .frame(width: 40, height: 40)
                        .padding(.bottom, 14)
                        
                        Spacer()
                        
                        // Right side: Action buttons & Right Stick
                        VStack(spacing: 16) {
                            ActionButtonsNativeView()
                            
                            NativeThumbstick { x, y in
                                KytyBridge.setRightStickX(x, y: y)
                            }
                            .frame(width: 130, height: 130)
                        }
                        .padding(.trailing, safeRight)
                        .padding(.bottom, 16)
                    }
                }
            }
        }
    }
}

// MARK: - D-Pad & Action Buttons

struct DPadNativeView: View {
    var body: some View {
        VStack(spacing: 4) {
            NativeTouchButton(
                systemImage: "chevron.up",
                symbolColor: .white,
                font: .systemFont(ofSize: 18, weight: .bold),
                isCircle: true
            ) { pressed in
                KytyBridge.setButton(.dpadUp, pressed: pressed)
            }
            .frame(width: 42, height: 42)
            
            HStack(spacing: 20) {
                NativeTouchButton(
                    systemImage: "chevron.left",
                    symbolColor: .white,
                    font: .systemFont(ofSize: 18, weight: .bold),
                    isCircle: true
                ) { pressed in
                    KytyBridge.setButton(.dpadLeft, pressed: pressed)
                }
                .frame(width: 42, height: 42)
                
                NativeTouchButton(
                    systemImage: "chevron.right",
                    symbolColor: .white,
                    font: .systemFont(ofSize: 18, weight: .bold),
                    isCircle: true
                ) { pressed in
                    KytyBridge.setButton(.dpadRight, pressed: pressed)
                }
                .frame(width: 42, height: 42)
            }
            
            NativeTouchButton(
                systemImage: "chevron.down",
                symbolColor: .white,
                font: .systemFont(ofSize: 18, weight: .bold),
                isCircle: true
            ) { pressed in
                KytyBridge.setButton(.dpadDown, pressed: pressed)
            }
            .frame(width: 42, height: 42)
        }
    }
}

struct ActionButtonsNativeView: View {
    var body: some View {
        VStack(spacing: 4) {
            NativeTouchButton(
                title: "△",
                symbolColor: PlatformColor.systemGreen,
                font: .systemFont(ofSize: 20, weight: .bold),
                isCircle: true
            ) { pressed in
                KytyBridge.setButton(.triangle, pressed: pressed)
            }
            .frame(width: 42, height: 42)
            
            HStack(spacing: 20) {
                NativeTouchButton(
                    title: "□",
                    symbolColor: PlatformColor(red: 1.0, green: 0.45, blue: 0.75, alpha: 1.0),
                    font: .systemFont(ofSize: 20, weight: .bold),
                    isCircle: true
                ) { pressed in
                    KytyBridge.setButton(.square, pressed: pressed)
                }
                .frame(width: 42, height: 42)
                
                NativeTouchButton(
                    title: "○",
                    symbolColor: PlatformColor.systemRed,
                    font: .systemFont(ofSize: 20, weight: .bold),
                    isCircle: true
                ) { pressed in
                    KytyBridge.setButton(.circle, pressed: pressed)
                }
                .frame(width: 42, height: 42)
            }
            
            NativeTouchButton(
                title: "✕",
                symbolColor: PlatformColor.systemBlue,
                font: .systemFont(ofSize: 20, weight: .bold),
                isCircle: true
            ) { pressed in
                KytyBridge.setButton(.cross, pressed: pressed)
            }
            .frame(width: 42, height: 42)
        }
    }
}
