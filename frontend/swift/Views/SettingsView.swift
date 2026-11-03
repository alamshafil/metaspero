// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct ConsoleLanguage: Identifiable, Hashable {
    let id: Int
    let name: String
}

let kConsoleLanguages: [ConsoleLanguage] = [
    ConsoleLanguage(id: 0, name: "Japanese"),
    ConsoleLanguage(id: 1, name: "English (United States)"),
    ConsoleLanguage(id: 2, name: "French"),
    ConsoleLanguage(id: 3, name: "Spanish (Spain)"),
    ConsoleLanguage(id: 4, name: "German"),
    ConsoleLanguage(id: 5, name: "Italian"),
    ConsoleLanguage(id: 6, name: "Dutch"),
    ConsoleLanguage(id: 7, name: "Portuguese (Portugal)"),
    ConsoleLanguage(id: 8, name: "Russian"),
    ConsoleLanguage(id: 9, name: "Korean"),
    ConsoleLanguage(id: 10, name: "Chinese (Traditional)"),
    ConsoleLanguage(id: 11, name: "Chinese (Simplified)"),
    ConsoleLanguage(id: 12, name: "Finnish"),
    ConsoleLanguage(id: 13, name: "Swedish"),
    ConsoleLanguage(id: 14, name: "Danish"),
    ConsoleLanguage(id: 15, name: "Norwegian"),
    ConsoleLanguage(id: 16, name: "Polish"),
    ConsoleLanguage(id: 17, name: "Portuguese (Brazil)"),
    ConsoleLanguage(id: 18, name: "English (United Kingdom)"),
    ConsoleLanguage(id: 19, name: "Turkish"),
    ConsoleLanguage(id: 20, name: "Spanish (Latin America)"),
    ConsoleLanguage(id: 21, name: "Arabic"),
    ConsoleLanguage(id: 22, name: "French (Canada)"),
    ConsoleLanguage(id: 23, name: "Czech"),
    ConsoleLanguage(id: 24, name: "Hungarian"),
    ConsoleLanguage(id: 25, name: "Greek"),
    ConsoleLanguage(id: 26, name: "Romanian"),
    ConsoleLanguage(id: 27, name: "Thai"),
    ConsoleLanguage(id: 28, name: "Vietnamese"),
    ConsoleLanguage(id: 29, name: "Indonesian")
]

struct SettingsView: View {
    @Environment(\.dismiss) private var dismiss
    
    // Graphics
    @AppStorage("resolutionScale") private var resolutionScale = "720p"
    @AppStorage("presentMode") private var presentMode = "Mailbox"
    @AppStorage("vblankFrequency") private var vblankFrequency = 60
    @AppStorage("enableMetalHud") private var enableMetalHud = false
    
    // Controllers & Input
    @AppStorage("enableHaptics") private var enableHaptics = true
    @AppStorage("enableTouchOverlay") private var enableTouchOverlay = touchOverlayDefault
    
    // JIT & Memory
    @AppStorage("skipMeshShaders") private var skipMeshShaders = false
    
    // Advanced Settings
    @AppStorage("userName") private var userName = "Metaspero"
    @AppStorage("userId") private var userId = 1000
    @AppStorage("consoleLanguage") private var consoleLanguage = 1
    @AppStorage("networkConnected") private var networkConnected = false
    @AppStorage("skipNoticeScreen") private var skipNoticeScreen = false
    @AppStorage("tessellationEnabled") private var tessellationEnabled = false
    @AppStorage("readbackLinearImages") private var readbackLinearImages = false
    @AppStorage("syncRawImageBuffers") private var syncRawImageBuffers = false
    @AppStorage("hdrEnabled") private var hdrEnabled = false
    @AppStorage("fullscreenEnabled") private var fullscreenEnabled = false
    
    // Diagnostics
    @AppStorage("enableLogFileWriting") private var enableLogFileWriting = false
    @AppStorage("enableMemoryWarningToast") private var enableMemoryWarningToast = true
    
    @State private var showLogSheet = false
    @State private var showAboutSheet = false
    @State private var showKeymapSheet = false
    @State private var controllers: [ControllerInfo] = []
    @State private var isSdlActive: Bool = false
    @State private var selectedTab: SettingsTab = .graphics
    
    enum SettingsTab: String, CaseIterable, Identifiable {
        case graphics = "Graphics"
        case controllers = "Controllers"
        case jit = "JIT"
        case advanced = "Advanced"
        case diagnostics = "Diagnostics"
        case about = "About"
        
        var id: String { rawValue }
        
        var icon: String {
            switch self {
            case .graphics: return "display"
            case .controllers: return "gamecontroller.fill"
            case .jit: return "cpu"
            case .advanced: return "slider.horizontal.3"
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
        Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "0.1.0"
    }

    var body: some View {
#if os(iOS)
        iOSView
#else
        macOSView
#endif
    }

    // MARK: - macOS View
    
#if os(macOS)
    var macOSView: some View {
        NavigationSplitView {
            List(SettingsTab.allCases, selection: $selectedTab) { tab in
                NavigationLink(value: tab) {
                    Label(tab.rawValue, systemImage: tab.icon)
                }
            }
            .listStyle(.sidebar)
            .navigationSplitViewColumnWidth(min: 160, ideal: 190, max: 230)
        } detail: {
            ScrollView {
                Group {
                    switch selectedTab {
                    case .graphics:
                        macGraphicsSection
                    case .controllers:
                        macControllersSection
                    case .jit:
                        macJitSection
                    case .advanced:
                        macAdvancedSection
                    case .diagnostics:
                        macDiagnosticsSection
                    case .about:
                        macAboutSection
                    }
                }
                .padding(24)
                .frame(maxWidth: 680, alignment: .leading)
            }
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                        .keyboardShortcut(.defaultAction)
                }
            }
        }
        .frame(minWidth: 700, idealWidth: 760, minHeight: 520, idealHeight: 580)
        .sheet(isPresented: $showLogSheet) {
            LogView()
        }
        .sheet(isPresented: $showKeymapSheet) {
            InputMappingView()
        }
        .onAppear {
            refreshControllers()
            enableLogFileWriting = KytyBridge.isLogFileWritingEnabled()
        }
    }

    private var macGraphicsSection: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Graphics")
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
                    
                    HStack {
                        Text("Vblank Frequency")
                        Spacer()
                        Picker("Vblank Frequency", selection: $vblankFrequency) {
                            Text("30 Hz").tag(30)
                            Text("60 Hz (Default)").tag(60)
                            Text("120 Hz").tag(120)
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
                VStack(alignment: .leading, spacing: 14) {
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
                    
                    Divider()
                    
                    HStack {
                        VStack(alignment: .leading, spacing: 2) {
                            Text("Keyboard Input Mapping")
                                .font(.system(size: 13, weight: .medium))
                            Text("Customize keyboard bindings corresponding to PS5 controls")
                                .font(.caption)
                                .foregroundColor(.secondary)
                        }
                        Spacer()
                        Button("Configure Keymap...") {
                            showKeymapSheet = true
                        }
                    }
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

    private var macJitSection: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("JIT")
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
        }
    }

    private var macAdvancedSection: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Advanced")
                .font(.headline)
            
            GroupBox {
                VStack(alignment: .leading, spacing: 14) {
                    Text("User Profile & System")
                        .font(.subheadline.weight(.semibold))
                    
                    HStack {
                        Text("User Name")
                        Spacer()
                        TextField("Metaspero", text: $userName)
                            .textFieldStyle(.roundedBorder)
                            .frame(width: 180)
                    }
                    
                    HStack {
                        Text("User ID")
                        Spacer()
                        TextField("1000", value: $userId, formatter: NumberFormatter())
                            .textFieldStyle(.roundedBorder)
                            .frame(width: 100)
                    }
                    
                    HStack {
                        Text("Console Language")
                        Spacer()
                        Picker("Console Language", selection: $consoleLanguage) {
                            ForEach(kConsoleLanguages) { lang in
                                Text(lang.name).tag(lang.id)
                            }
                        }
                        .labelsHidden()
                        .frame(width: 220)
                    }
                    
                    Toggle("Simulate Connected Network", isOn: $networkConnected)
                    Toggle("Skip Startup Logo & Notice Screens", isOn: $skipNoticeScreen)
                    
                    Divider()
                    
                    Text("Advanced Graphics")
                        .font(.subheadline.weight(.semibold))
                    
                    Toggle("Enable Tessellation", isOn: $tessellationEnabled)
                    Toggle("Synchronize Raw GPU Image Buffers", isOn: $syncRawImageBuffers)
                    Toggle("Readback Linear Images", isOn: $readbackLinearImages)
                    Toggle("High Dynamic Range (HDR)", isOn: $hdrEnabled)
                    Toggle("Start in Fullscreen", isOn: $fullscreenEnabled)
                }
                .padding(8)
            }
            
            GroupBox("Mesh Shaders") {
                VStack(alignment: .leading, spacing: 10) {
                    Toggle("Skip Mesh Shaders", isOn: $skipMeshShaders)
                    
                    HStack(alignment: .top, spacing: 8) {
                        Image(systemName: "exclamationmark.triangle.fill")
                            .foregroundColor(.orange)
                        Text("Warning: Skipping mesh shaders bypasses geometry processing. This can cause missing 3D geometry, severe rendering glitches, or game crashes. Only enable if a title fails to render or crashes on unsupported GPU hardware.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    .padding(8)
                    .background(Color.secondary.opacity(0.08))
                    .cornerRadius(6)
                }
                .padding(8)
            }
        }
    }

    private var macDiagnosticsSection: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Diagnostics")
                .font(.headline)
            
            GroupBox {
                VStack(alignment: .leading, spacing: 14) {
                    Button(action: { showLogSheet = true }) {
                        HStack {
                            Image(systemName: "terminal.fill")
                                .foregroundColor(.blue)
                            Text("View Recent Log Console")
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
                    
                    Divider()
                    
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
                        Label("Graphics", systemImage: "display")
                    }
                    NavigationLink { iosControllersPage } label: {
                        Label("Controllers", systemImage: "gamecontroller.fill")
                    }
                    NavigationLink { iosJitPage } label: {
                        Label("JIT", systemImage: "cpu")
                    }
                    NavigationLink { iosAdvancedPage } label: {
                        Label("Advanced", systemImage: "slider.horizontal.3")
                    }
                    NavigationLink { iosDiagnosticsPage } label: {
                        Label("Diagnostics", systemImage: "waveform.path.ecg")
                    }
                    NavigationLink { iosAboutPage } label: {
                        Label("About", systemImage: "info.circle.fill")
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
                
                Picker("Vblank Frequency", selection: $vblankFrequency) {
                    Text("30 Hz").tag(30)
                    Text("60 Hz (Default)").tag(60)
                    Text("120 Hz").tag(120)
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
            }
        }
        .navigationTitle("Graphics")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var iosControllersPage: some View {
        Form {
            Section(header: Text("Touch & Haptics")) {
                Toggle("On-Screen Touch Controls", isOn: $enableTouchOverlay)
                Toggle("Controller Haptic Feedback", isOn: $enableHaptics)
            }
            
            Section(header: Text("Connected Gamepads")) {
                HStack {
                    Text("SDL3 Subsystem")
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
                                Text("\(ctrl.type) via \(ctrl.source)")
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
            }
        }
        .navigationTitle("Controllers")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var iosJitPage: some View {
        Form {
            Section(header: Text("JIT Compilation")) {
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
                
                HStack {
                    Text("Process ID (PID)")
                    Spacer()
                    Text("\(KytyBridge.processId())")
                        .font(.system(.body, design: .monospaced))
                        .foregroundColor(.secondary)
                }
                
                Button(action: { KytyBridge.openStikDebug() }) {
                    Label("Attach StikDebug", systemImage: "ladybug.fill")
                }
                
                Button(action: {
                    DispatchQueue.global(qos: .userInitiated).async {
                        _ = KytyBridge.refreshJitStatus()
                    }
                }) {
                    Label("Refresh Status", systemImage: "arrow.clockwise")
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
        .navigationTitle("JIT")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var iosAdvancedPage: some View {
        Form {
            Section(header: Text("User Profile & System")) {
                HStack {
                    Text("User Name")
                    Spacer()
                    TextField("Metaspero", text: $userName)
                        .multilineTextAlignment(.trailing)
                }
                
                HStack {
                    Text("User ID")
                    Spacer()
                    TextField("1000", value: $userId, formatter: NumberFormatter())
                        .keyboardType(.numberPad)
                        .multilineTextAlignment(.trailing)
                }
                
                Picker("Console Language", selection: $consoleLanguage) {
                    ForEach(kConsoleLanguages) { lang in
                        Text(lang.name).tag(lang.id)
                    }
                }
                
                Toggle("Simulate Network Connected", isOn: $networkConnected)
                Toggle("Skip Startup Notices", isOn: $skipNoticeScreen)
            }
            
            Section(header: Text("Advanced Graphics")) {
                Toggle("Tessellation", isOn: $tessellationEnabled)
                Toggle("Sync Raw GPU Image Buffers", isOn: $syncRawImageBuffers)
                Toggle("Readback Linear Images", isOn: $readbackLinearImages)
                Toggle("High Dynamic Range (HDR)", isOn: $hdrEnabled)
            }
            
            Section(
                header: Text("Mesh Shaders"),
                footer: Text("Warning: Skipping mesh shaders bypasses geometry processing. This can cause missing 3D models, severe rendering glitches, or game crashes. Only enable if a title fails to render on unsupported GPU hardware.")
            ) {
                Toggle("Skip Mesh Shaders", isOn: $skipMeshShaders)
            }
        }
        .navigationTitle("Advanced")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var iosDiagnosticsPage: some View {
        Form {
            Section(header: Text("Diagnostics & Logs")) {
                Button(action: { showLogSheet = true }) {
                    Label("View Runtime Logs", systemImage: "terminal.fill")
                }
                
                Toggle("Write Logs to Disk", isOn: $enableLogFileWriting)
                    .onChange(of: enableLogFileWriting) { enabled in
                        KytyBridge.setLogFileWritingEnabled(enabled)
                    }
                
                Toggle("Memory Pressure Toast", isOn: $enableMemoryWarningToast)
            }
            
            Section(header: Text("Memory Footprint")) {
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
        }
        .navigationTitle("Diagnostics")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var iosAboutPage: some View {
        AboutKytyContentView(appVersion: appVersionString)
            .navigationTitle("About")
            .navigationBarTitleDisplayMode(.inline)
    }
#endif

    private func refreshControllers() {
        isSdlActive = KytyBridge.isSDLGamepadSubsystemActive()
        let raw = KytyBridge.getConnectedControllers()
        var list: [ControllerInfo] = []
        for (i, d) in raw.enumerated() {
            let name = d["name"] as? String ?? "Gamepad #\(i)"
            let type = d["type"] as? String ?? "Generic Gamepad"
            let source = d["source"] as? String ?? "SDL3"
            list.append(ControllerInfo(id: i, name: name, type: type, source: source))
        }
        self.controllers = list
    }
}

#if os(iOS)
enum ExtendedMemoryEntitlement {
    private static let increasedLimitKey = "com.apple.developer.kernel.increased-memory-limit"
    private static let extendedVAKey = "com.apple.developer.kernel.extended-virtual-addressing"

    private static let entitlements: [String: Any]? = {
        guard let url = Bundle.main.executableURL,
              let data = try? Data(contentsOf: url, options: .alwaysMapped) else { return nil }
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
}
#endif
