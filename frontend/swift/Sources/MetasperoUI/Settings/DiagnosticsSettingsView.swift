// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

#if os(iOS) && !SKIP
/// Dedicated top-level JIT & Entitlements screen on iOS for quick StikDebug access.
struct JitSettingsView: View {
    @State private var isJitReady: Bool = KytyBridge.isJitReady()
    @State private var isProcessDebugged: Bool = KytyBridge.isProcessDebugged()

    var body: some View {
        Form {
            Section(header: Text("JIT Runtime")) {
                HStack {
                    Text("JIT Status")
                    Spacer()
                    HStack(spacing: 6) {
                        Circle()
                            .fill(isJitReady ? Color.green : (isProcessDebugged ? Color.orange : Color.secondary))
                            .frame(width: 8, height: 8)
                        Text(isJitReady ? "Ready" : (isProcessDebugged ? "Debugger Attached" : "Inactive"))
                            .foregroundColor(.secondary)
                    }
                }

                HStack {
                    Text("Process ID (PID)")
                    Spacer()
                    Text("\(KytyBridge.processId())")
                        .font(.system(.body, design: .monospaced))
                        .foregroundColor(.secondary)
                }

                HStack {
                    Text("CPU Core")
                    Spacer()
                    Text("FEXCore (x86_64 -> ARM64)")
                        .foregroundColor(.secondary)
                }

                Button(action: { KytyBridge.openStikDebug() }) {
                    PlatformLabel("Attach StikDebug", systemImage: "ladybug.fill")
                }

                Button(action: refreshJitState) {
                    PlatformLabel("Refresh Status", systemImage: "arrow.clockwise")
                }
            }

            Section(header: Text("Entitlements")) {
                HStack {
                    Text("Extended Memory Limit")
                    Spacer()
                    Text(ExtendedMemoryEntitlement.hasIncreasedMemoryLimit ? "Granted" : "Not Granted")
                        .foregroundColor(ExtendedMemoryEntitlement.hasIncreasedMemoryLimit ? .green : .secondary)
                }
                HStack {
                    Text("Extended Virtual Addressing")
                    Spacer()
                    Text(ExtendedMemoryEntitlement.hasExtendedVirtualAddressing ? "Granted" : "Not Granted")
                        .foregroundColor(ExtendedMemoryEntitlement.hasExtendedVirtualAddressing ? .green : .secondary)
                }
            }
        }
        .settingsFormStyle()
        .navigationTitle("JIT")
        .navigationBarTitleDisplayMode(.inline)
        .onAppear {
            syncJitState()
        }
    }

    private func syncJitState() {
        isJitReady = KytyBridge.isJitReady()
        isProcessDebugged = KytyBridge.isProcessDebugged()
    }

    private func refreshJitState() {
        Task {
            await Task.detached(priority: .userInitiated) {
                _ = KytyBridge.refreshJitStatus()
            }.value
            syncJitState()
        }
    }
}
#endif

struct DiagnosticsSettingsView: View {
    @AppStorage(SettingsKey.enableLogFileWriting) private var enableLogFileWriting = false
    @AppStorage(SettingsKey.enableMemoryWarningToast) private var enableMemoryWarningToast = true

    @State private var showLogSheet = false
    @State private var usedMemoryMB: Int = 0
    @State private var limitMemoryMB: Int = 0
    #if os(macOS)
    @State private var isJitReady: Bool = KytyBridge.isJitReady()
    @State private var isProcessDebugged: Bool = KytyBridge.isProcessDebugged()
    #endif

    var body: some View {
        Form {
            #if os(macOS) || SKIP
            Section(header: Text("Runtime & CPU")) {
                HStack {
                    Text("JIT Status")
                    Spacer()
                    #if os(macOS)
                    HStack(spacing: 6) {
                        Circle()
                            .fill(isJitReady ? Color.green : (isProcessDebugged ? Color.orange : Color.secondary))
                            .frame(width: 8, height: 8)
                        Text(isJitReady ? "Ready" : (isProcessDebugged ? "Debugger Attached" : "Inactive"))
                            .foregroundColor(.secondary)
                    }
                    #else
                    HStack(spacing: 6) {
                        Circle()
                            .fill(Color.green)
                            .frame(width: 8, height: 8)
                        Text("Ready")
                            .foregroundColor(.secondary)
                    }
                    #endif
                }

                #if os(macOS)
                HStack {
                    Text("Process ID (PID)")
                    Spacer()
                    Text("\(KytyBridge.processId())")
                        .font(.system(.body, design: .monospaced))
                        .foregroundColor(.secondary)
                }
                #endif

                HStack {
                    Text("CPU Core")
                    Spacer()
                    Text("FEXCore (x86_64 -> ARM64)")
                        .foregroundColor(.secondary)
                }
            }
            #endif

            Section(header: Text("Memory")) {
                HStack {
                    Text("Memory Footprint")
                    Spacer()
                    Text("\(usedMemoryMB) MB / \(limitMemoryMB) MB")
                        .font(.system(.body, design: .monospaced))
                        .foregroundColor(.secondary)
                }

                #if !SKIP
                Toggle("Memory Warning Toast", isOn: $enableMemoryWarningToast)
                #endif
            }

            Section(header: Text("Logs")) {
                Button(action: { showLogSheet = true }) {
                    PlatformLabel("View Recent Logs", systemImage: "terminal.fill")
                }

                #if SKIP
                Text("Logs are streamed in memory for the current session and are not written to disk.")
                    .font(.caption)
                    .foregroundColor(.secondary)
                #else
                Toggle("Write Logs to Disk", isOn: $enableLogFileWriting)
                    .onChange(of: enableLogFileWriting) { enabled in
                        KytyBridge.setLogFileWritingEnabled(enabled)
                    }
                #endif
            }
        }
        .settingsFormStyle()
        .navigationTitle("Diagnostics")
        #if os(iOS) || SKIP
        .navigationBarTitleDisplayMode(.inline)
        #endif
        .sheet(isPresented: $showLogSheet) {
            LogView()
        }
        .onAppear {
            refreshStats()
        }
    }

    private func refreshStats() {
        enableLogFileWriting = KytyBridge.isLogFileWritingEnabled()
        let stats = KytyBridge.currentMemoryStats()
        #if SKIP
        usedMemoryMB = (stats[KytyMemoryUsedMBKey] as? Int) ?? 0
        limitMemoryMB = (stats[KytyMemoryLimitMBKey] as? Int) ?? 0
        #else
        usedMemoryMB = (stats[KytyMemoryUsedMBKey] as? NSNumber)?.intValue ?? 0
        limitMemoryMB = (stats[KytyMemoryLimitMBKey] as? NSNumber)?.intValue ?? 0
        #endif
        #if os(macOS)
        isJitReady = KytyBridge.isJitReady()
        isProcessDebugged = KytyBridge.isProcessDebugged()
        #endif
    }
}
