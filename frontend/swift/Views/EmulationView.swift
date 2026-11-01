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
                KytyBridge.stopEmulation()
                isEmulating = false
            }
#endif
        }
        .onAppear {
            #if os(iOS)
            OrientationManager.lockLandscape()
            #endif
        }
        .onDisappear {
            #if os(iOS)
            OrientationManager.unlockOrientation()
            #endif
            KytyBridge.stopEmulation()
        }
        #if os(iOS)
        .navigationBarHidden(true)
        .statusBar(hidden: true)
        #endif
    }
}

// MARK: - In-Game Overlay (Controls & Top HUD)

struct InGameOverlayView: View {
    let game: GameItem
    let onExit: () -> Void
    
    @AppStorage("enableTouchOverlay") private var enableTouchOverlay = touchOverlayDefault
    @ObservedObject private var physicalManager = PhysicalControllerManager.shared
    @State private var showTouchOverlay = true
    @State private var showLogSheet = false
    @State private var showHud = true
    
    var body: some View {
        GeometryReader { geo in
            let safeTop = max(geo.safeAreaInsets.top, 8)
            let safeRight = max(geo.safeAreaInsets.trailing, 20)
            
            ZStack {
                // Customizable On-Screen Touch Controls
                if enableTouchOverlay && showTouchOverlay && !physicalManager.hasPhysicalController {
                    TouchControlsView()
                        .edgesIgnoringSafeArea(.all)
                }
                
                // Top HUD Bar
                if showHud {
                    VStack {
                        HStack {
                            Spacer()
                            
                            // Right-aligned Actions: Logs, Toggle Controls, Quit, Collapse
                            HStack(spacing: 12) {
                                // Diagnostics / Logs
                                Button(action: {
                                    NSLog("[InGameOverlayView] Terminal/Logs button tapped")
                                    showLogSheet = true
                                }) {
                                    Image(systemName: "terminal.fill")
                                        .font(.system(size: 15))
                                        .foregroundColor(.white)
                                        .frame(width: 38, height: 38)
                                        .background(Color.black.opacity(0.75))
                                        .clipShape(Circle())
                                }
                                .contentShape(Rectangle())
                                
                                // Touch Overlay Toggle
                                if enableTouchOverlay && !physicalManager.hasPhysicalController {
                                Button(action: {
                                    withAnimation {
                                        showTouchOverlay.toggle()
                                    }
                                }) {
                                    Image(systemName: showTouchOverlay ? "gamecontroller.fill" : "gamecontroller")
                                        .font(.system(size: 15))
                                        .foregroundColor(.white)
                                        .frame(width: 38, height: 38)
                                        .background(Color.black.opacity(0.75))
                                        .clipShape(Circle())
                                }
                                .contentShape(Rectangle())
                                }
                                
                                // Quit Button
                                Button(action: {
                                    NSLog("[InGameOverlayView] Quit button tapped")
                                    onExit()
                                }) {
                                    HStack(spacing: 6) {
                                        Image(systemName: "xmark.circle.fill")
                                            .font(.system(size: 15, weight: .bold))
                                        Text("Quit")
                                            .font(.system(size: 14, weight: .bold))
                                    }
                                    .foregroundColor(.white)
                                    .padding(.horizontal, 16)
                                    .padding(.vertical, 8)
                                    .background(Color.red.opacity(0.85))
                                    .clipShape(Capsule())
                                }
                                .contentShape(Rectangle())
                                
                                // Collapse HUD
                                Button(action: {
                                    withAnimation {
                                        showHud = false
                                    }
                                }) {
                                    Image(systemName: "chevron.up")
                                        .font(.system(size: 15))
                                        .foregroundColor(.white)
                                        .frame(width: 38, height: 38)
                                        .background(Color.black.opacity(0.75))
                                        .clipShape(Circle())
                                }
                                .contentShape(Rectangle())
                            }
                        }
                        .padding(.trailing, safeRight)
                        .padding(.top, safeTop)
                        
                        Spacer()
                    }
                    .transition(.move(edge: .top).combined(with: .opacity))
                } else {
                    // Discreet top handle to restore HUD
                    VStack {
                        HStack {
                            Spacer()
                            Button(action: {
                                withAnimation {
                                    showHud = true
                                }
                            }) {
                                Image(systemName: "chevron.down")
                                    .font(.system(size: 13, weight: .bold))
                                    .foregroundColor(.white.opacity(0.9))
                                    .frame(width: 54, height: 28)
                                    .background(Color.black.opacity(0.7))
                                    .clipShape(Capsule())
                            }
                            .contentShape(Rectangle())
                            .padding(.top, safeTop)
                            Spacer()
                        }
                        Spacer()
                    }
                }
            }
        }
        .onReceive(physicalManager.$hasPhysicalController) { connected in
            // Collapse the HUD when a controller connects; restore it when disconnected
            withAnimation { showHud = !connected }
        }
        .sheet(isPresented: $showLogSheet) {
            LogView()
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
}
#endif
