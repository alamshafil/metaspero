// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct ControllersSettingsView: View {
    @AppStorage(SettingsKey.enableTouchOverlay) private var enableTouchOverlay = SettingsDefault.enableTouchOverlay
    @AppStorage(SettingsKey.enableHaptics) private var enableHaptics = true

    @State private var controllers: [ControllerInfo] = []
    @State private var showKeymapSheet = false

    struct ControllerInfo: Identifiable {
        let id: Int
        let name: String
        let type: String
        let source: String
    }

    var body: some View {
        Form {
            Section {
                Toggle("On-Screen Controls Overlay", isOn: $enableTouchOverlay)
                Toggle("Controller Vibration / Haptics", isOn: $enableHaptics)
            } header: {
                Text("Controls & Feedback")
            } footer: {
                Text("Enable or disable the on-screen virtual gamepad overlay during gameplay.")
            }

            #if os(macOS)
            Section {
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Keyboard Input Mapping")
                        Text("Customize keyboard bindings corresponding to PS5 controls")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    Spacer()
                    Button("Configure Keymap...") {
                        showKeymapSheet = true
                    }
                }
            } header: {
                Text("Keyboard")
            }
            #endif

            #if !SKIP
            Section {
                HStack {
                    Text("Connected Gamepads")
                    Spacer()
                    Text("\(controllers.count) detected")
                        .foregroundColor(.secondary)
                }

                if controllers.isEmpty {
                    VStack(spacing: 6) {
                        Text("No Physical Gamepads Connected")
                            .font(.subheadline)
                            .foregroundColor(.secondary)
                        Text("Connect a DualSense, DualShock 4, Xbox, or MFi controller via Bluetooth or USB to play.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                            .multilineTextAlignment(.center)
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 8)
                } else {
                    ForEach(controllers) { ctrl in
                        HStack {
                            PlatformIcon("gamecontroller.fill")
                                .foregroundColor(.blue)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(ctrl.name)
                                    .font(.subheadline)
                                Text("\(ctrl.type) - via \(ctrl.source)")
                                    .font(.caption2)
                                    .foregroundColor(.secondary)
                            }
                            Spacer()
                            Text("Connected")
                                .font(.caption)
                                .foregroundColor(.green)
                        }
                    }
                }

                Button(action: refreshControllers) {
                    PlatformLabel("Rescan Controllers", systemImage: "arrow.clockwise")
                }
            } header: {
                Text("Gamepads")
            }
            #endif
        }
        .settingsFormStyle()
        .navigationTitle("Controllers")
        #if os(iOS) || SKIP
        .navigationBarTitleDisplayMode(.inline)
        #endif
        #if os(macOS)
        .sheet(isPresented: $showKeymapSheet) {
            InputMappingView()
        }
        #endif
        .onAppear {
            refreshControllers()
        }
    }

    private func refreshControllers() {
        #if !SKIP
        let raw = KytyBridge.getConnectedControllers()
        var list: [ControllerInfo] = []
        for (i, d) in raw.enumerated() {
            let name = d["name"] as? String ?? "Gamepad #\(i)"
            let type = d["type"] as? String ?? "Generic Gamepad"
            let source = d["source"] as? String ?? "SDL3"
            list.append(ControllerInfo(id: i, name: name, type: type, source: source))
        }
        self.controllers = list
        #endif
    }
}
