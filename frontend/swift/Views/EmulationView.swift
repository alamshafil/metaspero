// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI
import QuartzCore
import GameController
import MetalKit

#if os(iOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif

/// On-screen controls default: on for iOS, off for macOS (opt-in via Settings).
#if os(macOS)
let touchOverlayDefault = false
#else
let touchOverlayDefault = true
#endif

struct EmulationView: View {
    let game: GameItem
    @Binding var isEmulating: Bool
    @State private var isClosing: Bool = false
    @State private var closingTimeoutTask: Task<Void, Never>? = nil
    @AppStorage("enableTouchOverlay") private var enableTouchOverlay = touchOverlayDefault
    @AppStorage("resolutionScale") private var resolutionScale = "720p"
    @ObservedObject private var physicalManager = PhysicalControllerManager.shared
    
    var body: some View {
        ZStack {
            Color.black.edgesIgnoringSafeArea(.all)
            
            // Core Metal rendering surface (16:9 aspect ratio on macOS, full-screen on iOS)
#if os(macOS)
            EmulatorMetalSurface(game: game)
                .aspectRatio(16.0 / 9.0, contentMode: .fit)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
#else
            EmulatorMetalSurface(game: game)
                .edgesIgnoringSafeArea(.all)
#endif
            
#if os(macOS)
            if enableTouchOverlay && !physicalManager.hasPhysicalController {
                TouchControlsView()
            }
#else
            InGameOverlayView(game: game) {
                requestCloseGame()
            }
#endif

            // Graceful "Closing Game..." toast overlay while teardown completes
            if isClosing {
                closingToastOverlay
            }
        }
        .onAppear {
            #if os(iOS)
            OrientationManager.lockLandscape()
            #endif
        }
        .onDisappear {
            closingTimeoutTask?.cancel()
            closingTimeoutTask = nil
            #if os(iOS)
            OrientationManager.unlockOrientation()
            #endif
            if !isEmulating && KytyBridge.isEmulationRunning() && !KytyBridge.isStopping() {
                KytyBridge.stopEmulation()
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: NSNotification.Name.KytyEmulationStopping)) { _ in
            withAnimation(.easeInOut(duration: 0.2)) {
                isClosing = true
            }
            closingTimeoutTask?.cancel()
            closingTimeoutTask = Task { @MainActor in
                try? await Task.sleep(nanoseconds: 8_000_000_000)
                guard !Task.isCancelled else { return }
                if isClosing && KytyBridge.isStopping() {
                    KytyBridge.addLogMessage("[EmulationView] Shutdown safety timeout reached (8.0s), forcing cleanup.")
                    KytyBridge.forceResetEmulationSession()
                    withAnimation(.easeInOut(duration: 0.25)) {
                        isClosing = false
                        isEmulating = false
                    }
                }
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: NSNotification.Name.KytyEmulationStopped)) { _ in
            closingTimeoutTask?.cancel()
            closingTimeoutTask = nil
            withAnimation(.easeInOut(duration: 0.25)) {
                isClosing = false
                isEmulating = false
            }
        }
        #if os(iOS)
        .navigationBarHidden(true)
        .statusBar(hidden: true)
        #endif
    }
    
    private func requestCloseGame() {
        guard !isClosing else { return }
        withAnimation(.easeInOut(duration: 0.2)) {
            isClosing = true
        }
        KytyBridge.stopEmulation()
    }

    private var closingToastOverlay: some View {
        ZStack {
            Color.black.opacity(0.6)
                .edgesIgnoringSafeArea(.all)
            
            VStack(spacing: 14) {
                ProgressView()
                    .progressViewStyle(CircularProgressViewStyle(tint: .white))
                    .scaleEffect(1.25)
                
                Text("Closing Game...")
                    .font(.system(size: 17, weight: .semibold))
                    .foregroundColor(.white)
                
                Text("Releasing GPU & system memory")
                    .font(.system(size: 13))
                    .foregroundColor(.white.opacity(0.7))
            }
            .padding(.horizontal, 28)
            .padding(.vertical, 22)
            .background(
                RoundedRectangle(cornerRadius: 16)
                    .fill(.ultraThinMaterial)
                    .shadow(color: .black.opacity(0.35), radius: 20, x: 0, y: 10)
            )
        }
        .transition(.opacity)
        .zIndex(99)
    }
}

// MARK: - In-Game Overlay (Controls & Options Menu)

struct InGameOverlayView: View {
    let game: GameItem
    let onExit: () -> Void
    
    @AppStorage("enableTouchOverlay") private var enableTouchOverlay = touchOverlayDefault
    @ObservedObject private var physicalManager = PhysicalControllerManager.shared
    @State private var showTouchOverlay = true
    @State private var showLogSheet = false
    @State private var showSettingsSheet = false
    
    var body: some View {
        GeometryReader { geo in
            let safeTop = max(geo.safeAreaInsets.top, 10)
            let safeRight = max(geo.safeAreaInsets.trailing, 20)
            
            ZStack(alignment: .topTrailing) {
                Color.clear
                
                // Customizable On-Screen Touch Controls (independent from HUD button)
                if enableTouchOverlay && showTouchOverlay && !physicalManager.hasPhysicalController {
                    TouchControlsView()
                        .edgesIgnoringSafeArea(.all)
                }
                
                Menu {
                    // Controller overlay toggle (only if virtual controls are enabled and no gamepad attached)
                    if enableTouchOverlay && !physicalManager.hasPhysicalController {
                        Button {
                            withAnimation(.easeInOut(duration: 0.2)) {
                                showTouchOverlay.toggle()
                            }
                        } label: {
                            Label(
                                showTouchOverlay ? "Hide On-Screen Controls" : "Show On-Screen Controls",
                                systemImage: showTouchOverlay ? "gamecontroller.fill" : "gamecontroller"
                            )
                        }
                    }
                    
                    Button {
                        showLogSheet = true
                    } label: {
                        Label("Logs & Diagnostics", systemImage: "terminal.fill")
                    }
                    
                    Button {
                        showSettingsSheet = true
                    } label: {
                        Label("Settings", systemImage: "gearshape.fill")
                    }
                    
                    Divider()
                    
                    Button(role: .destructive) {
                        onExit()
                    } label: {
                        Label("Quit Game", systemImage: "xmark.circle.fill")
                    }
                } label: {
                    ZStack {
                        Circle()
                            .fill(Color.black.opacity(0.60))
                            .frame(width: 34, height: 34)
                            .overlay(
                                Circle()
                                    .stroke(Color.white.opacity(0.22), lineWidth: 1)
                            )
                            .shadow(color: Color.black.opacity(0.35), radius: 6, x: 0, y: 2)
                        
                        Image(systemName: "ellipsis")
                            .font(.system(size: 15, weight: .bold))
                            .foregroundColor(.white.opacity(0.95))
                    }
                    .frame(width: 44, height: 44)
                    .contentShape(Rectangle())
                }
                .padding(.top, safeTop)
                .padding(.trailing, safeRight)
            }
        }
        .sheet(isPresented: $showLogSheet) {
            LogView()
        }
        .sheet(isPresented: $showSettingsSheet) {
            SettingsView()
        }
    }
}

// MARK: - Metal Render Canvas

/// Returns the target drawable pixel dimensions for the configured resolution setting.
private func configuredDrawableSize() -> (width: Int, height: Int) {
    let res = UserDefaults.standard.string(forKey: "resolutionScale") ?? "720p"
    if res == "480p" {
        return (854, 480)
    }
    if res == "1080p" {
        return (1920, 1080)
    }
    return (1280, 720)
}

private func configureMetalLayerScale(_ metalLayer: CAMetalLayer, hostBounds: CGRect) -> (width: Int, height: Int) {
    let (targetW, targetH) = configuredDrawableSize()
    metalLayer.frame = hostBounds
    guard hostBounds.width > 0, hostBounds.height > 0 else { return (targetW, targetH) }
    let scale = CGFloat(targetH) / hostBounds.height
    metalLayer.contentsScale = scale
    var w = Int((hostBounds.width * scale).rounded())
    let h = targetH
    if abs(w - targetW) <= 3 {
        w = targetW
    }
    metalLayer.drawableSize = CGSize(width: w, height: h)
    return (w, h)
}

/// The core renders straight into this view's CAMetalLayer. The view reports
/// its pixel size to the bridge so the renderer can resize its swapchain.
#if os(iOS)
final class HostMetalView: UIView {
    let metalLayer = CAMetalLayer()

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .black
        isMultipleTouchEnabled = true
        metalLayer.device = MTLCreateSystemDefaultDevice()
        metalLayer.pixelFormat = .bgra8Unorm
        metalLayer.framebufferOnly = true
        let (w, h) = configuredDrawableSize()
        metalLayer.drawableSize = CGSize(width: w, height: h)
        layer.addSublayer(metalLayer)
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    override func layoutSubviews() {
        super.layoutSubviews()
        let (w, h) = configureMetalLayerScale(metalLayer, hostBounds: bounds)
        KytyBridge.hostViewDidResize(toPixelWidth: Int32(w), height: Int32(h))
    }
}

struct EmulatorMetalSurface: UIViewRepresentable {
    let game: GameItem

    func makeUIView(context: Context) -> HostMetalView {
        let view = HostMetalView(frame: .zero)
        KytyBridge.startEmulation(withGamePath: game.path, in: view) { success, error in
            if !success, let error = error {
                NSLog("[EmulatorMetalSurface] Launch error: %@", error)
            }
        }
        return view
    }

    func updateUIView(_ uiView: HostMetalView, context: Context) {
        let (w, h) = configureMetalLayerScale(uiView.metalLayer, hostBounds: uiView.bounds)
        KytyBridge.hostViewDidResize(toPixelWidth: Int32(w), height: Int32(h))
    }

    static func dismantleUIView(_ uiView: HostMetalView, coordinator: ()) {
        uiView.metalLayer.removeFromSuperlayer()
    }
}
#elseif os(macOS)
final class HostMetalView: NSView {
    let metalLayer = CAMetalLayer()

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        layer?.backgroundColor = NSColor.black.cgColor
        metalLayer.device = MTLCreateSystemDefaultDevice()
        metalLayer.pixelFormat = .bgra8Unorm
        metalLayer.framebufferOnly = true
        let (w, h) = configuredDrawableSize()
        metalLayer.drawableSize = CGSize(width: w, height: h)
        layer?.addSublayer(metalLayer)
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    override func layout() {
        super.layout()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let (w, h) = configureMetalLayerScale(metalLayer, hostBounds: bounds)
        CATransaction.commit()
        KytyBridge.hostViewDidResize(toPixelWidth: Int32(w), height: Int32(h))
    }
}

struct EmulatorMetalSurface: NSViewRepresentable {
    let game: GameItem

    func makeNSView(context: Context) -> HostMetalView {
        let view = HostMetalView(frame: .zero)
        KytyBridge.startEmulation(withGamePath: game.path, in: view) { success, error in
            if !success, let error = error {
                NSLog("[EmulatorMetalSurface] Launch error: %@", error)
            }
        }
        return view
    }

    func updateNSView(_ nsView: HostMetalView, context: Context) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let (w, h) = configureMetalLayerScale(nsView.metalLayer, hostBounds: nsView.bounds)
        CATransaction.commit()
        KytyBridge.hostViewDidResize(toPixelWidth: Int32(w), height: Int32(h))
    }

    static func dismantleNSView(_ nsView: HostMetalView, coordinator: ()) {
        nsView.metalLayer.removeFromSuperlayer()
    }
}
#endif
