// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct SettingsView: View {
    @Environment(\.dismiss) private var dismiss
    
    @AppStorage("resolutionScale") private var resolutionScale = "720p"
    @AppStorage("presentMode") private var presentMode = "Mailbox"
    @AppStorage("enableHaptics") private var enableHaptics = true
    @AppStorage("enableTouchOverlay") private var enableTouchOverlay = touchOverlayDefault
    @AppStorage("enableMetalHud") private var enableMetalHud = false
    @AppStorage("enableLogFileWriting") private var enableLogFileWriting = false
    @AppStorage("enableMemoryWarningToast") private var enableMemoryWarningToast = true
    @AppStorage("memoryBackingMode") private var memoryBackingMode = "ram"
    @AppStorage("skipMeshShaders") private var skipMeshShaders = false
    
    @State private var showLogSheet = false
    @State private var showAboutSheet = false
    @State private var controllers: [ControllerInfo] = []
    @State private var isSdlActive: Bool = false
    @State private var selectedMacTab: MacSettingsTab = .graphics
    
    enum MacSettingsTab: String, CaseIterable, Identifiable {
        case graphics = "Graphics"
        case controllers = "Controllers"
        case general = "JIT"
        case diagnostics = "Diagnostics"
        case about = "About"
        
        var id: String { rawValue }
        
        var icon: String {
            switch self {
            case .general: return "cpu"
            case .graphics: return "display"
            case .controllers: return "gamecontroller.fill"
            case .diagnostics: return "waveform.path.ecg"
            case .about: return "info.circle.fill"
            }
        }
    }
    
    struct ControllerInfo: Identifiable {
        let id: Int
        let name: String
        let type: String
        let source: String
    }
    
    var appVersionString: String {
        let version = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "0.0.1"
        let build = Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "0.0.1"
        return "v\(version) (\(build))"
    }
    
    var body: some View {
#if os(macOS)
        macOSView
#else
        iOSView
#endif
    }
    
    // MARK: - macOS View
    
#if os(macOS)
    var macOSView: some View {
        VStack(spacing: 0) {
            // macOS Toolbar / Tab Selector
            HStack {
                Picker("", selection: $selectedMacTab) {
                    ForEach(MacSettingsTab.allCases) { tab in
                        Label(tab.rawValue, systemImage: tab.icon).tag(tab)
                    }
                }
                .pickerStyle(.segmented)
                .frame(maxWidth: 480)
                
                Spacer()
                
                Button("Done") {
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
            }
            .padding(.horizontal, 20)
            .padding(.top, 16)
            .padding(.bottom, 12)
            
            Divider()
            
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    switch selectedMacTab {
                    case .general:
                        macGeneralSection
                    case .graphics:
                        macGraphicsSection
                    case .controllers:
                        macControllersSection
                    case .diagnostics:
                        macDiagnosticsSection
                    case .about:
                        macAboutSection
                    }
                }
                .padding(24)
            }
        }
        .frame(minWidth: 560, idealWidth: 620, minHeight: 480, idealHeight: 520)
        .onAppear {
            refreshControllers()
            enableLogFileWriting = KytyBridge.isLogFileWritingEnabled()
        }
        .sheet(isPresented: $showLogSheet) {
            LogView()
        }
    }
    
    private var macGeneralSection: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("JIT Compilation & CPU Engine")
                .font(.headline)
            
            GroupBox {
                VStack(spacing: 12) {
                    HStack {
                        Text("JIT Status")
                        Spacer()
                        HStack(spacing: 6) {
                            Circle()
                                .fill(KytyBridge.isJitReady() ? Color.green : (KytyBridge.isProcessDebugged() ? Color.orange : Color.secondary))
                                .frame(width: 8, height: 8)
                            Text(KytyBridge.isJitReady() ? "Ready" : (KytyBridge.isProcessDebugged() ? "Debugger Attached" : "Inactive"))
                                .foregroundColor(.secondary)
                        }
                    }
                    
                    Divider()
                    
                    HStack {
                        Text("Process ID (PID)")
                        Spacer()
                        Text("\(KytyBridge.processId())")
                            .font(.system(.body, design: .monospaced))
                            .foregroundColor(.secondary)
                    }
                    
                    Divider()
                    
                    HStack {
                        Text("CPU Core")
                        Spacer()
                        Text("FEXCore (x86_64 -> ARM64)")
                            .foregroundColor(.secondary)
                    }
                }
                .padding(8)
            }
            
            HStack {
                Spacer()
                Button(action: {
                    DispatchQueue.global(qos: .userInitiated).async {
                        _ = KytyBridge.refreshJitStatus()
                    }
                }) {
                    Label("Refresh JIT Status", systemImage: "arrow.clockwise")
                }
            }
        }
    }
    
    private var macGraphicsSection: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Graphics & Display")
                .font(.headline)
            
            GroupBox {
                VStack(alignment: .leading, spacing: 14) {
                    HStack {
                        Text("Resolution")
                        Spacer()
                        Picker("Resolution", selection: $resolutionScale) {
                            Text("480p (Performance)").tag("480p")
                            Text("720p (Default)").tag("720p")
                            Text("1080p (Full HD)").tag("1080p")
                        }
                        .labelsHidden()
                        .fixedSize()
                    }
                    
                    Divider()
                    
                    HStack {
                        Text("V-Sync / Present Mode")
                        Spacer()
                        Picker("V-Sync / Present Mode", selection: $presentMode) {
                            Text("Mailbox (Fast V-Sync, Lowest Latency)").tag("Mailbox")
                            Text("Fifo (Strict 60 FPS V-Sync)").tag("Fifo")
                            Text("Immediate (Uncapped Frame Rate)").tag("Immediate")
                        }
                        .labelsHidden()
                        .fixedSize()
                    }
                    
                    Divider()
                    
                    Toggle("Apple Metal Performance HUD", isOn: $enableMetalHud)
                        .onChange(of: enableMetalHud) { newValue in
                            KytyBridge.setMetalHudEnabled(newValue)
                        }
                    
                    Divider()
                    
                    HStack {
                        Text("Vulkan Translation")
                        Spacer()
                        Text(KytyBridge.moltenVkVersion())
                            .foregroundColor(.secondary)
                    }

                    Divider()

                    Toggle("Skip Mesh Shaders (Debug)", isOn: $skipMeshShaders)
                        .onChange(of: skipMeshShaders) { _ in
                            KytyBridge.applyGraphicsDebugSettings()
                        }
                }
                .padding(8)
            }
        }
    }
    
    private var macControllersSection: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Text("Controllers & Input")
                    .font(.headline)
                Spacer()
                Button(action: refreshControllers) {
                    Label("Rescan", systemImage: "arrow.clockwise")
                }
            }
            
            GroupBox {
                VStack(alignment: .leading, spacing: 12) {
                    HStack {
                        Text("SDL3 Gamepad Subsystem")
                        Spacer()
                        HStack(spacing: 6) {
                            Circle()
                                .fill(isSdlActive ? Color.green : Color.orange)
                                .frame(width: 8, height: 8)
                            Text(isSdlActive ? "Active" : "Standby / Initialized")
                                .foregroundColor(.secondary)
                        }
                    }
                    
                    Divider()
                    
                    HStack {
                        Text("Connected Gamepads")
                        Spacer()
                        Text("\(controllers.count) detected")
                            .foregroundColor(.secondary)
                    }
                    
                    Divider()
                    
                    Toggle("On-Screen Controls Overlay", isOn: $enableTouchOverlay)
                }
                .padding(8)
            }
            
            if controllers.isEmpty {
                VStack(spacing: 8) {
                    Image(systemName: "gamecontroller")
                        .font(.system(size: 32))
                        .foregroundColor(.secondary)
                    Text("No Physical Gamepads Connected")
                        .font(.subheadline)
                        .foregroundColor(.secondary)
                    Text("Connect a DualSense, DualShock 4, Xbox, or MFi controller via Bluetooth or USB to play.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .multilineTextAlignment(.center)
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 24)
            } else {
                VStack(alignment: .leading, spacing: 8) {
                    Text("Detected Controllers:")
                        .font(.subheadline)
                        .foregroundColor(.secondary)
                    
                    ForEach(controllers) { ctrl in
                        GroupBox {
                            HStack {
                                Image(systemName: "gamecontroller.fill")
                                    .font(.system(size: 20))
                                    .foregroundColor(.blue)
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(ctrl.name)
                                        .font(.system(size: 13, weight: .semibold))
                                    Text("\(ctrl.type) - via \(ctrl.source)")
                                        .font(.caption)
                                        .foregroundColor(.secondary)
                                }
                                Spacer()
                                Text("Connected")
                                    .font(.caption)
                                    .foregroundColor(.green)
                            }
                            .padding(4)
                        }
                    }
                }
            }
        }
    }
    
    private var macDiagnosticsSection: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Diagnostics & Log Storage")
                .font(.headline)
            
            GroupBox {
                VStack(alignment: .leading, spacing: 14) {
                    Button(action: { showLogSheet = true }) {
                        HStack {
                            Image(systemName: "terminal.fill")
                                .foregroundColor(.blue)
                            Text("View Logs")
                            Spacer()
                            Image(systemName: "arrow.up.right")
                        }
                    }
                    
                    Divider()
                    
                    Toggle("Write Logs to Disk", isOn: $enableLogFileWriting)
                        .onChange(of: enableLogFileWriting) { enabled in
                            KytyBridge.setLogFileWritingEnabled(enabled)
                        }
                    
                    Toggle("Memory Warning Toast", isOn: $enableMemoryWarningToast)
                    
                    HStack {
                        Text("Memory Footprint")
                        Spacer()
                        let stats = KytyBridge.currentMemoryStats()
                        let used = (stats[KytyMemoryUsedMBKey] as? NSNumber)?.intValue ?? 0
                        let limit = (stats[KytyMemoryLimitMBKey] as? NSNumber)?.intValue ?? 0
                        Text("\(used) MB / \(limit) MB")
                            .font(.system(.body, design: .monospaced))
                            .foregroundColor(.secondary)
                    }
                }
                .padding(8)
            }
        }
    }
    
    private var macAboutSection: some View {
        AboutKytyContentView(appVersion: appVersionString)
    }
#endif
    
    // MARK: - iOS View
    
#if os(iOS)
    var iOSView: some View {
        NavigationStack {
            Form {
                Section {
                    NavigationLink { iosGraphicsPage } label: {
                        Label("Graphics & Display", systemImage: "display")
                    }
                    NavigationLink { iosControllersPage } label: {
                        Label("Controllers & Input", systemImage: "gamecontroller.fill")
                    }
                    NavigationLink { iosSystemMemoryPage } label: {
                        Label("System & Memory", systemImage: "cpu")
                    }
                    NavigationLink { iosDiagnosticsPage } label: {
                        Label("Diagnostics", systemImage: "waveform.path.ecg")
                    }
                    NavigationLink { iosAboutPage } label: {
                        Label("About KytyPS5", systemImage: "info.circle.fill")
                    }
                }
            }
            .navigationTitle("Settings")
            .sheet(isPresented: $showLogSheet) {
                LogView()
            }
            .onAppear {
                refreshControllers()
                enableLogFileWriting = KytyBridge.isLogFileWritingEnabled()
            }
        }
    }

    private var iosGraphicsPage: some View {
        Form {
                Section(
                    header: Text("Graphics & Display"),
                    footer: Text("Mailbox: fast V-Sync, lowest latency. Fifo: strict 60 FPS V-Sync. Immediate: uncapped frame rate.")
                ) {
                    Picker("Resolution", selection: $resolutionScale) {
                        Text("480p").tag("480p")
                        Text("720p").tag("720p")
                        Text("1080p").tag("1080p")
                    }
                    
                    Picker("Present Mode", selection: $presentMode) {
                        Text("Mailbox").tag("Mailbox")
                        Text("Fifo").tag("Fifo")
                        Text("Immediate").tag("Immediate")
                    }
                    
                    Toggle("Metal Performance HUD", isOn: $enableMetalHud)
                        .onChange(of: enableMetalHud) { newValue in
                            KytyBridge.setMetalHudEnabled(newValue)
                        }
                    
                    HStack {
                        Text("Vulkan Translation")
                        Spacer()
                        Text(KytyBridge.moltenVkVersion())
                            .foregroundColor(.secondary)
                    }

                    Toggle("Skip Mesh Shaders (Debug)", isOn: $skipMeshShaders)
                        .onChange(of: skipMeshShaders) { _ in
                            KytyBridge.applyGraphicsDebugSettings()
                        }
                }
        }
        .navigationTitle("Graphics & Display")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var iosControllersPage: some View {
        Form {
                Section(
                    header: Text("Controllers & Input"),
                    footer: Text(controllers.isEmpty ? "No physical gamepads connected. Connect a DualSense, DualShock 4, Xbox, or MFi controller via Bluetooth." : "\(controllers.count) controller(s) detected.")
                ) {
                    HStack {
                        Text("SDL3 Gamepad Subsystem")
                        Spacer()
                        Text(isSdlActive ? "Active" : "Standby")
                            .foregroundColor(isSdlActive ? .green : .secondary)
                    }
                    
                    if !controllers.isEmpty {
                        ForEach(controllers) { ctrl in
                            HStack {
                                Image(systemName: "gamecontroller.fill")
                                    .foregroundColor(.blue)
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(ctrl.name)
                                        .font(.subheadline)
                                    Text("\(ctrl.type) (\(ctrl.source))")
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
                        HStack {
                            Image(systemName: "arrow.clockwise")
                            Text("Refresh Connected Controllers")
                        }
                    }
                    
                    Toggle("On-Screen Touch Controls", isOn: $enableTouchOverlay)
                    Toggle("Haptic Touch Feedback", isOn: $enableHaptics)
                }
        }
        .navigationTitle("Controllers & Input")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var iosSystemMemoryPage: some View {
        Form {
            Section(header: Text("JIT Compilation")) {
                HStack {
                    Text("JIT Status")
                    Spacer()
                    Text(KytyBridge.isJitReady() ? "Ready" : (KytyBridge.isProcessDebugged() ? "Debugger Attached" : "Inactive"))
                        .foregroundColor(KytyBridge.isJitReady() ? .green : (KytyBridge.isProcessDebugged() ? .orange : .secondary))
                }
                
                HStack {
                    Text("Process ID (PID)")
                    Spacer()
                    Text("\(KytyBridge.processId())")
                        .foregroundColor(.secondary)
                        .font(.system(.body, design: .monospaced))
                }
                
                Button(action: {
                    KytyBridge.openStikDebug()
                }) {
                    HStack {
                        Image(systemName: "arrow.up.right.square")
                        Text("Open StikDebug")
                    }
                }
                
                Button(action: {
                    DispatchQueue.global(qos: .userInitiated).async {
                        _ = KytyBridge.refreshJitStatus()
                    }
                }) {
                    HStack {
                        Image(systemName: "arrow.clockwise")
                        Text("Refresh JIT Status")
                    }
                }
            }
            
            Section(
                header: Text("Direct Memory Backing"),
                footer: Text(memoryBackingMode == "file"
                    ? "Disk-Backed (APFS) creates sparse swap files on internal storage. Warning: increases NAND storage writes."
                    : "RAM (Mach VM) stores direct memory entirely in RAM for highest performance with zero storage writes. Recommended.")
            ) {
                Picker("Backing Mode", selection: $memoryBackingMode) {
                    Text("RAM (Mach VM)").tag("ram")
                    Text("Disk-Backed (APFS)").tag("file")
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                
                if memoryBackingMode == "file" {
                    HStack(alignment: .top, spacing: 8) {
                        Image(systemName: "exclamationmark.triangle.fill")
                            .foregroundColor(.orange)
                        Text("Warning: Disk-backed memory increases internal storage writes. Not recommended, only use for testing.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    .padding(.vertical, 2)
                }
            }
        }
        .navigationTitle("System & Memory")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var iosDiagnosticsPage: some View {
        Form {
                Section(header: Text("Diagnostics")) {
                    Button(action: { showLogSheet = true }) {
                        HStack {
                            Image(systemName: "terminal.fill")
                                .foregroundColor(.blue)
                            Text("View Logs")
                                .foregroundColor(.primary)
                            Spacer()
                            Image(systemName: "chevron.right")
                                .foregroundColor(.secondary)
                                .font(.system(size: 13))
                        }
                    }
                    
                    Toggle("Write Logs to Disk", isOn: $enableLogFileWriting)
                        .onChange(of: enableLogFileWriting) { enabled in
                            KytyBridge.setLogFileWritingEnabled(enabled)
                        }
                    
                    Toggle("Memory Warning Toast", isOn: $enableMemoryWarningToast)
                    
                    HStack {
                        Text("Extended Memory Entitlement")
                        Spacer()
                        Text(ExtendedMemoryEntitlement.statusText)
                            .foregroundColor(ExtendedMemoryEntitlement.isGranted ? .green : .secondary)
                    }
                    
                    HStack {
                        Text("Memory Footprint")
                        Spacer()
                        let stats = KytyBridge.currentMemoryStats()
                        let used = (stats[KytyMemoryUsedMBKey] as? NSNumber)?.intValue ?? 0
                        let limit = (stats[KytyMemoryLimitMBKey] as? NSNumber)?.intValue ?? 0
                        Text("\(used) MB / \(limit) MB")
                            .font(.system(.subheadline, design: .monospaced))
                            .foregroundColor(.secondary)
                    }
                }
        }
        .navigationTitle("Diagnostics")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var iosAboutPage: some View {
        AboutKytyContentView(appVersion: appVersionString)
            .navigationBarTitleDisplayMode(.inline)
    }

#endif
    
    private func refreshControllers() {
        isSdlActive = KytyBridge.isSDLGamepadSubsystemActive()
        let rawList = KytyBridge.getConnectedControllers()
        var items: [ControllerInfo] = []
        for (idx, dict) in rawList.enumerated() {
            let name = dict["name"] as? String ?? "Gamepad"
            let type = dict["type"] as? String ?? "Generic"
            let source = dict["source"] as? String ?? "SDL3"
            items.append(ControllerInfo(id: idx, name: name, type: type, source: source))
        }
        controllers = items
    }
}

#if os(iOS)
/// Reads the entitlements actually signed into the app's executable (code signature
/// entitlements blob).
enum ExtendedMemoryEntitlement {
    private static let increasedLimitKey = "com.apple.developer.kernel.increased-memory-limit"
    private static let extendedVAKey = "com.apple.developer.kernel.extended-virtual-addressing"

    private static let entitlements: [String: Any]? = {
        guard let url = Bundle.main.executableURL,
              let data = try? Data(contentsOf: url, options: .alwaysMapped) else { return nil }
        // Entitlements blob: magic 0xFADE7171 (big-endian), 4-byte big-endian length, then XML plist.
        let magic = Data([0xFA, 0xDE, 0x71, 0x71])
        var searchRange = data.startIndex..<data.endIndex
        while let hit = data.range(of: magic, options: [], in: searchRange) {
            let lenStart = hit.upperBound
            if lenStart + 4 <= data.endIndex {
                let length = data[lenStart..<lenStart + 4].reduce(0) { ($0 << 8) | Int($1) }
                let bodyStart = lenStart + 4
                let bodyEnd = hit.lowerBound + length
                if length > 8, bodyEnd <= data.endIndex, bodyStart < bodyEnd {
                    let body = data.subdata(in: bodyStart..<bodyEnd)
                    if let object = try? PropertyListSerialization.propertyList(from: body, options: [], format: nil),
                       let dict = object as? [String: Any] {
                        return dict
                    }
                }
            }
            searchRange = hit.upperBound..<data.endIndex
        }
        return nil
    }()

    static var hasIncreasedMemoryLimit: Bool { (entitlements?[increasedLimitKey] as? Bool) ?? false }
    static var hasExtendedVirtualAddressing: Bool { (entitlements?[extendedVAKey] as? Bool) ?? false }
    static var isGranted: Bool { hasIncreasedMemoryLimit }

    static var statusText: String {
        if entitlements == nil { return "Unknown" }
        if hasIncreasedMemoryLimit { return hasExtendedVirtualAddressing ? "Granted (+Ext. VA)" : "Granted" }
        return "Not Granted"
    }
}
#endif
