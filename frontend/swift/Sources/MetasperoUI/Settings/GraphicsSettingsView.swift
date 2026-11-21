// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct GraphicsSettingsView: View {
    // Display
    @AppStorage(SettingsKey.resolutionScale) private var resolutionScale = SettingsDefault.resolutionScale
    @AppStorage(SettingsKey.presentMode) private var presentMode = SettingsDefault.presentMode
    @AppStorage(SettingsKey.vblankFrequency) private var vblankFrequency = SettingsDefault.vblankFrequency

    // Overlays
    @AppStorage(SettingsKey.perfOverlayEnabled) private var perfOverlayEnabled = false
    @AppStorage(SettingsKey.enableMetalHud) private var enableMetalHud = false
    @AppStorage(SettingsKey.hudDetailLevel) private var hudDetailLevel = SettingsDefault.hudDetailLevel

    // Experimental
    @AppStorage(SettingsKey.renderScalePercent) private var renderScalePercent = SettingsDefault.renderScalePercent
    @AppStorage(SettingsKey.relaxedShaderFloat) private var relaxedShaderFloat = false
    @AppStorage(SettingsKey.skipMeshShaders) private var skipMeshShaders = false
    @AppStorage(SettingsKey.enableSrtArm64Jit) private var enableSrtArm64Jit = false

    var body: some View {
        Form {
            Section {
                Picker("Resolution", selection: $resolutionScale) {
                    Text("480p").tag("480p")
                    Text("720p (Default)").tag("720p")
                    Text("1080p").tag("1080p")
                }

                Picker("V-Sync / Present Mode", selection: $presentMode) {
                    Text("Mailbox (Fast V-Sync)").tag("Mailbox")
                    Text("Fifo (60 FPS V-Sync)").tag("Fifo")
                    Text("Immediate (Uncapped)").tag("Immediate")
                }

                Picker("Vblank Frequency", selection: $vblankFrequency) {
                    Text("30 Hz").tag(30)
                    Text("60 Hz (Default)").tag(60)
                    Text("120 Hz").tag(120)
                }
            } header: {
                Text("Display")
            }

            Section {
                #if SKIP
                Picker("Active Driver", selection: Binding(
                    get: { KytyBridge.selectedDriverId },
                    set: { KytyBridge.selectDriver(id: $0) }
                )) {
                    ForEach(KytyBridge.installedDrivers) { driver in
                        Text(driver.name).tag(driver.id)
                    }
                }

                Button(action: {
                    KytyBridge.requestInstallDriver()
                }) {
                    PlatformLabel("Install Custom Driver (.zip)", systemImage: "arrow.down.doc.fill")
                }
                #else
                HStack {
                    Text("Active Driver")
                    Spacer()
                    Text(KytyBridge.moltenVkVersion())
                        .foregroundColor(.secondary)
                }
                #endif
            } header: {
                Text("GPU Driver")
            }

            Section {
                Toggle("Kyty Performance Overlay", isOn: $perfOverlayEnabled)

                #if SKIP
                Toggle("Performance & FPS HUD", isOn: $enableMetalHud)
                if enableMetalHud {
                    Picker("HUD Detail Level", selection: $hudDetailLevel) {
                        Text("Full Stats").tag("Full")
                        Text("Minimal").tag("Minimal")
                    }
                }
                #else
                Toggle("Apple Metal Performance HUD", isOn: $enableMetalHud)
                    .onChange(of: enableMetalHud) { newValue in
                        KytyBridge.setMetalHudEnabled(newValue)
                    }
                #endif
            } header: {
                Text("Overlays")
            }

            Section {
                Picker("4K Render Scale (Highly Experimental)", selection: $renderScalePercent) {
                    Text("100% (Native)").tag(100)
                    Text("75%").tag(75)
                    Text("50% (1080p)").tag(50)
                    Text("33% (720p)").tag(33)
                    Text("25% (540p)").tag(25)
                    Text("22% (480p)").tag(22)
                }

                Toggle("Skip Mesh Shaders", isOn: $skipMeshShaders)
                Toggle("ARM64 SRT Walker JIT", isOn: $enableSrtArm64Jit)
                Toggle("Relaxed Shader Float", isOn: $relaxedShaderFloat)
            } header: {
                Text("Experimental")
            } footer: {
                Text("Experimental options may cause rendering glitches, missing geometry, or crashes. 4K Render Scale is highly experimental.")
            }
        }
        .settingsFormStyle()
        .navigationTitle("Graphics")
        #if os(iOS) || SKIP
        .navigationBarTitleDisplayMode(.inline)
        #endif
    }
}
