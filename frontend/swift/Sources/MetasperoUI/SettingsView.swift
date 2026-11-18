// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI
#if os(iOS) && !SKIP
import UIKit
#endif
#if !SKIP
import UniformTypeIdentifiers
#endif

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
    var isSheet: Bool = false
    @Environment(\.dismiss) private var dismiss
    
    // Graphics
    @AppStorage("resolutionScale") private var resolutionScale = "720p"
    @AppStorage("presentMode") private var presentMode = "Mailbox"
    @AppStorage("vblankFrequency") private var vblankFrequency = 60
    @AppStorage("enableMetalHud") private var enableMetalHud = false
    @AppStorage("hudDetailLevel") private var hudDetailLevel = "Full"
    @AppStorage("perfOverlayEnabled") private var perfOverlayEnabled = false
    
    // Controllers & Input
    @AppStorage("enableHaptics") private var enableHaptics = true
    @AppStorage("enableTouchOverlay") private var enableTouchOverlay: Bool = true
    
    // JIT & Memory
    @AppStorage("skipMeshShaders") private var skipMeshShaders = false
    @AppStorage("enableSrtArm64Jit") private var enableSrtArm64Jit = false
    
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
    @State private var showFolderImporter = false
    @State private var scanPaths: [String] = []
    @State private var controllers: [ControllerInfo] = []
    @State private var selectedTab: SettingsTab = .library
    
    enum SettingsTab: String, CaseIterable, Identifiable {
        case library = "Library"
        case graphics = "Graphics"
        case controllers = "Controllers"
        case jit = "JIT"
        case advanced = "Advanced"
        case diagnostics = "Diagnostics"
        case about = "About"
        
        var id: String { rawValue }
        
        var icon: String {
            switch self {
            case .library: return "folder.fill"
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
#if os(iOS) || SKIP
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
                    case .library:
                        macLibrarySection
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
        #if !SKIP
        .fileImporter(
            isPresented: $showFolderImporter,
            allowedContentTypes: [.folder],
            allowsMultipleSelection: false
        ) { result in
            handleFolderImport(result: result)
        }
        #endif
        .onAppear {
            loadScanPaths()
            refreshControllers()
            enableLogFileWriting = KytyBridge.isLogFileWritingEnabled()
        }
    }

    private var macLibrarySection: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Text("Game Folders")
                    .font(.headline)
                Spacer()
                Button(action: { showFolderImporter = true }) {
                    Label("Add Game Folder", systemImage: "folder.badge.plus")
                }
            }
            
            GroupBox {
                VStack(alignment: .leading, spacing: 10) {
                    if scanPaths.isEmpty {
                        VStack(spacing: 8) {
                            Image(systemName: "folder")
                                .font(.system(size: 32))
                                .foregroundColor(.secondary)
                            Text("No Game Folders")
                                .font(.subheadline)
                                .foregroundColor(.secondary)
                            Text("Add a folder with PS5 games, ELFs, or ZAR files to scan automatically.")
                                .font(.caption)
                                .foregroundColor(.secondary)
                                .multilineTextAlignment(.center)
                        }
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 20)
                    } else {
                        ForEach(Array(scanPaths.enumerated()), id: \.offset) { index, path in
                            if index > 0 {
                                Divider()
                            }
                            HStack(spacing: 10) {
                                Image(systemName: "folder.fill")
                                    .foregroundColor(.blue)
                                Text(path)
                                    .font(.system(.body, design: .monospaced))
                                    .lineLimit(1)
                                    .truncationMode(.middle)
                                Spacer()
                                Button(role: .destructive) {
                                    removeScanPath(at: index)
                                } label: {
                                    Image(systemName: "trash")
                                        .foregroundColor(.red)
                                }
                                .buttonStyle(.borderless)
                                .help("Remove Folder")
                            }
                        }
                    }
                }
                .padding(8)
            }
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
                    
                    VStack(alignment: .leading, spacing: 4) {
                        Toggle("Kyty Performance Overlay", isOn: $perfOverlayEnabled)
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
            
            GroupBox("Metaspero-Specific Advanced Options") {
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
                    
                    Divider()
                    
                    Toggle("Experimental ARM64 SRT Walker (JIT)", isOn: $enableSrtArm64Jit)
                    
                    HStack(alignment: .top, spacing: 8) {
                        Image(systemName: "exclamationmark.triangle.fill")
                            .foregroundColor(.orange)
                        Text("The native SRT walker interpreter is used by default. Enabling the ARM64 JIT is experimental and may improve performance, but can cause shader resource glitches or crashes.")
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

#if os(iOS) || SKIP
    var iOSView: some View {
        NavigationStack {
            Form {
                Section {
                    NavigationLink { iosLibraryPage } label: {
                        PlatformLabel("Library", systemImage: "folder.fill")
                    }
                    NavigationLink { iosGraphicsPage } label: {
                        PlatformLabel("Graphics", systemImage: "display")
                    }
                    NavigationLink { iosControllersPage } label: {
                        PlatformLabel("Controllers", systemImage: "gamecontroller.fill")
                    }
                    NavigationLink { iosJitPage } label: {
                        PlatformLabel("JIT", systemImage: "cpu")
                    }
                }
                
                Section {
                    NavigationLink { iosAdvancedPage } label: {
                        PlatformLabel("Advanced", systemImage: "slider.horizontal.3")
                    }
                    NavigationLink { iosDiagnosticsPage } label: {
                        PlatformLabel("Diagnostics", systemImage: "waveform.path.ecg")
                    }
                    NavigationLink { iosAboutPage } label: {
                        PlatformLabel("About", systemImage: "info.circle.fill")
                    }
                }
                #if SKIP
                Section(header: Text("Android System")) {
                    NavigationLink { androidDriversPage } label: {
                        PlatformLabel("GPU Driver Manager", systemImage: "cpu")
                    }
                }
                #endif
            }
            #if SKIP
            .scrollContentBackground(.hidden)
            #endif
            .navigationTitle("Settings")
            #if !SKIP
            .navigationBarTitleDisplayMode(isSheet ? .inline : .large)
            #endif
            .toolbar {
                if isSheet {
                    ToolbarItem(placement: .confirmationAction) {
                        Button("Done") { dismiss() }
                    }
                }
            }
            .sheet(isPresented: $showLogSheet) {
                LogView()
            }
            #if !SKIP
            .fileImporter(
                isPresented: $showFolderImporter,
                allowedContentTypes: [.folder],
                allowsMultipleSelection: false
            ) { result in
                handleFolderImport(result: result)
            }
            #endif
            .onAppear {
                loadScanPaths()
                refreshControllers()
                enableLogFileWriting = KytyBridge.isLogFileWritingEnabled()
            }
        }
    }

    private var iosLibraryPage: some View {
        Form {
            #if os(iOS) && !SKIP
            Section {
                Button(action: openDocumentsInFilesApp) {
                    PlatformLabel("Open in Files App", systemImage: "folder.fill")
                }
            } header: {
                Text("App Library Folder")
            } footer: {
                Text("Put games in On My iPhone/iPad -> Metaspero in Files to avoid copying.")
            }
            #endif

            Section {
                if scanPaths.isEmpty {
                    Text("No Game Folders")
                        .font(.subheadline)
                        .foregroundColor(.secondary)
                } else {
                    ForEach(Array(scanPaths.enumerated()), id: \.offset) { index, path in
                        HStack {
                            PlatformIcon("folder.fill")
                                .foregroundColor(.blue)
                            Text(path)
                                .font(.system(.subheadline, design: .monospaced))
                                .lineLimit(1)
                                .truncationMode(.middle)
                            Spacer()
                            Button(role: .destructive) {
                                removeScanPath(at: index)
                            } label: {
                                PlatformIcon("trash")
                                    .foregroundColor(.red)
                            }
                            .buttonStyle(.borderless)
                        }
                    }
                }
                
                Button(action: {
                    #if SKIP
                    KytyBridge.requestPickFolder()
                    #else
                    showFolderImporter = true
                    #endif
                }) {
                    PlatformLabel("Add Game Folder", systemImage: "folder.badge.plus")
                }
            } header: {
                Text("Game Folders")
            } footer: {
                Text("Games, ELFs, and ZAR files in these folders are scanned automatically.")
            }
        }
        #if SKIP
        .scrollContentBackground(.hidden)
        #endif
        .navigationTitle("Library")
        .navigationBarTitleDisplayMode(.inline)
    }

    #if SKIP
    private var androidDriversPage: some View {
        Form {
            Section(header: Text("Active Driver")) {
                Picker("Active Driver", selection: Binding(
                    get: { KytyBridge.selectedDriverId },
                    set: { KytyBridge.selectDriver(id: $0) }
                )) {
                    ForEach(KytyBridge.installedDrivers) { driver in
                        Text(driver.name).tag(driver.id)
                    }
                }
            }
            
            Section(header: Text("Custom Drivers")) {
                Button(action: {
                    KytyBridge.requestInstallDriver()
                }) {
                    PlatformLabel("Install Custom Driver (.zip)", systemImage: "arrow.down.doc.fill")
                }
            }
        }
        .scrollContentBackground(.hidden)
        .navigationTitle("GPU Drivers")
        .navigationBarTitleDisplayMode(.inline)
    }
    #endif

    private var iosGraphicsPage: some View {
        Form {
            Section {
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
                
                Toggle("Kyty Performance Overlay", isOn: $perfOverlayEnabled)
                
                #if SKIP
                Toggle("Performance & FPS HUD", isOn: $enableMetalHud)
                if enableMetalHud {
                    Picker("HUD Detail Level", selection: $hudDetailLevel) {
                        Text("Full Stats (FPS, RAM, GPU)").tag("Full")
                        Text("Minimal (FPS & Frame Time)").tag("Minimal")
                    }
                }
                
                HStack {
                    Text("Vulkan Graphics Engine")
                    Spacer()
                    Text("Kyty Vulkan")
                        .foregroundColor(.secondary)
                }
                #else
                Toggle("Metal Performance HUD", isOn: $enableMetalHud)
                
                HStack {
                    Text("Vulkan Translation")
                    Spacer()
                    Text(KytyBridge.moltenVkVersion())
                        .foregroundColor(.secondary)
                }
                #endif
            } header: {
                Text("Graphics")
            } footer: {
                #if SKIP
                Text("Mailbox: fast V-Sync, lowest latency. Fifo: strict 60 FPS V-Sync. Immediate: uncapped frame rate. Vblank Frequency controls target display refresh rate (30, 60, or 120 Hz).")
                #else
                Text("Mailbox: fast V-Sync, lowest latency. Fifo: strict 60 FPS V-Sync. Immediate: uncapped frame rate.")
                #endif
            }
        }
        #if SKIP
        .scrollContentBackground(.hidden)
        #endif
        .onChange(of: enableMetalHud) { newValue in
            KytyBridge.setMetalHudEnabled(newValue)
        }
        .navigationTitle("Graphics")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var iosControllersPage: some View {
        Form {
            Section {
                Toggle("On-Screen Controls Overlay", isOn: $enableTouchOverlay)
                Toggle("Controller Haptic Feedback", isOn: $enableHaptics)
            } header: {
                Text("Touch & Haptics")
            } footer: {
                Text("Enable or disable the on-screen virtual gamepad overlay during gameplay.")
            }
            
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
            } header: {
                Text("Controllers")
            }
        }
        #if SKIP
        .scrollContentBackground(.hidden)
        #endif
        .toolbar {
            ToolbarItem(placement: .navigationBarTrailing) {
                Button(action: refreshControllers) {
                    PlatformIcon("arrow.clockwise")
                }
            }
        }
        .navigationTitle("Controllers")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var iosJitPage: some View {
        Form {
            Section(header: Text("JIT")) {
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
                
                HStack {
                    Text("CPU Core")
                    Spacer()
                    Text("FEXCore (x86_64 -> ARM64)")
                        .foregroundColor(.secondary)
                }
                
#if !SKIP
                Button(action: { KytyBridge.openStikDebug() }) {
                    PlatformLabel("Attach StikDebug", systemImage: "ladybug.fill")
                }
#endif
                
                Button(action: {
                    DispatchQueue.global().async {
                        _ = KytyBridge.refreshJitStatus()
                    }
                }) {
                    PlatformLabel("Refresh Status", systemImage: "arrow.clockwise")
                }
            }
            
#if os(iOS) && !SKIP
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
#endif
        }
        #if SKIP
        .scrollContentBackground(.hidden)
        #endif
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
#if !SKIP
                    TextField("1000", value: $userId, formatter: NumberFormatter())
                        .keyboardType(.numberPad)
                        .multilineTextAlignment(.trailing)
#else
                    TextField("1000", text: Binding(get: { "\(userId)" }, set: { userId = Int($0) ?? userId }))
                        .multilineTextAlignment(.trailing)
#endif
                }
                
                Picker("Console Language", selection: $consoleLanguage) {
                    ForEach(kConsoleLanguages) { lang in
                        Text(lang.name).tag(lang.id)
                    }
                }
                
                Toggle("Simulate Connected Network", isOn: $networkConnected)
                Toggle("Skip Startup Logo & Notice Screens", isOn: $skipNoticeScreen)
            }
            
            Section(header: Text("Advanced Graphics")) {
                Toggle("Enable Tessellation", isOn: $tessellationEnabled)
                Toggle("Synchronize Raw GPU Image Buffers", isOn: $syncRawImageBuffers)
                Toggle("Readback Linear Images", isOn: $readbackLinearImages)
                Toggle("High Dynamic Range (HDR)", isOn: $hdrEnabled)
            }
            
            Section {
                Toggle("Skip Mesh Shaders", isOn: $skipMeshShaders)
                Toggle("Experimental ARM64 SRT Walker (JIT)", isOn: $enableSrtArm64Jit)
            } header: {
                Text("Metaspero-Specific Advanced Options")
            } footer: {
                Text("Skipping mesh shaders bypasses geometry processing and can cause missing 3D geometry, severe rendering glitches, or game crashes. The native SRT walker interpreter is the default, enabling the experimental ARM64 JIT may improve performance but can cause shader resource glitches or crashes.")
            }
        }
        #if SKIP
        .scrollContentBackground(.hidden)
        #endif
        .navigationTitle("Advanced")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var iosDiagnosticsPage: some View {
        Form {
            Section(header: Text("Diagnostics")) {
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
                
                Toggle("Memory Warning Toast", isOn: $enableMemoryWarningToast)
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
        #if SKIP
        .scrollContentBackground(.hidden)
        #endif
        .navigationTitle("Diagnostics")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var iosAboutPage: some View {
        AboutKytyContentView(appVersion: appVersionString)
            .navigationTitle("About")
            .navigationBarTitleDisplayMode(.inline)
    }
#endif

    private func loadScanPaths() {
        #if !SKIP
        scanPaths = UserDefaults.standard.stringArray(forKey: "KytyGameScanPaths") ?? []
        #else
        scanPaths = KytyBridge.customGamePaths
        #endif
    }

    private func removeScanPath(at index: Int) {
        guard index >= 0 && index < scanPaths.count else { return }
        scanPaths.remove(at: index)
        #if !SKIP
        UserDefaults.standard.set(scanPaths, forKey: "KytyGameScanPaths")
        #else
        KytyBridge.customGamePaths = scanPaths
        KytyBridge.onCustomGamePathsChanged?(scanPaths)
        #endif
        NotificationCenter.default.post(name: Notification.Name("KytyGamePathsChanged"), object: nil)
    }

    #if os(iOS) && !SKIP
    private func openDocumentsInFilesApp() {
        guard let documentsURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first else { return }
        if let filesURL = URL(string: "shareddocuments://\(documentsURL.path)") {
            UIApplication.shared.open(filesURL)
        }
    }
    #endif

    #if !SKIP
    private func handleFolderImport(result: Result<[URL], Error>) {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return }
            let accessed = url.startAccessingSecurityScopedResource()
            #if os(macOS)
            defer { if accessed { url.stopAccessingSecurityScopedResource() } }
            #else
            if accessed {
                LibraryView.saveSecurityScopedBookmark(for: url)
            }
            #endif
            if !scanPaths.contains(url.path) {
                scanPaths.append(url.path)
                UserDefaults.standard.set(scanPaths, forKey: "KytyGameScanPaths")
                NotificationCenter.default.post(name: Notification.Name("KytyGamePathsChanged"), object: nil)
            }
        case .failure(let error):
            NSLog("[SettingsView] Folder import failed: %@", error.localizedDescription)
        }
    }
    #endif

    private func refreshControllers() {
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

#if os(iOS) && !SKIP
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
