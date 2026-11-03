// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI
#if os(iOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif
import UniformTypeIdentifiers

struct GameItem: Identifiable, Hashable {
    let id = UUID()
    let title: String
    let titleId: String
    let appVersion: String
    let rawVersion: String
    let firmwareVersion: String
    let path: String
    let sizeString: String
    let isDirectory: Bool
    let iconPath: String?
    let isValidGame: Bool
}

struct LibraryView: View {
    @Binding var selectedGame: GameItem?
    @Binding var isEmulating: Bool

    @Environment(\.scenePhase) var scenePhase
    
    @State private var games: [GameItem] = []
    @State private var showImporter = false
    @State private var showLogSheet = false
    @State private var showSettingsSheet = false
    @State private var showTrophyOverview = false
    @State private var selectedGameForDetails: GameItem? = nil
    @State private var selectedGameForTrophies: GameItem? = nil
    @State private var gameToRemove: GameItem? = nil
    @State private var showRemoveConfirmation = false
    @State private var showJitWarning = false
    @State private var showSlowClosingAlert = false
    @State private var pendingLaunchGame: GameItem? = nil
    @State private var isRefreshing = false
    
    @AppStorage("libraryViewMode") private var libraryViewMode = "grid"
    @State private var searchText = ""
    
#if os(iOS)
    let columns = [
        GridItem(.adaptive(minimum: 155, maximum: 195), spacing: 18)
    ]
#else
    let columns = [
        GridItem(.adaptive(minimum: 170, maximum: 220), spacing: 20)
    ]
#endif
    
    var filteredGames: [GameItem] {
        let query = searchText.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        guard !query.isEmpty else { return games }
        return games.filter {
            $0.title.localizedCaseInsensitiveContains(query) ||
            $0.titleId.localizedCaseInsensitiveContains(query) ||
            $0.rawVersion.localizedCaseInsensitiveContains(query)
        }
    }
    
    var body: some View {
#if os(macOS)
        if isEmulating, let game = selectedGame {
            NavigationStack {
                EmulationView(
                    game: game,
                    isEmulating: $isEmulating
                )
                .id(game.id)
                .navigationTitle(game.title)
                .toolbar {
                    ToolbarItem(placement: .automatic) {
                        Button(action: { showLogSheet = true }) {
                            Label("Logs", systemImage: "terminal.fill")
                        }
                        .help("View Logs & Diagnostics")
                    }
                    
                    ToolbarItem(placement: .automatic) {
                        Button(action: { showSettingsSheet = true }) {
                            Label("Settings", systemImage: "gearshape")
                        }
                        .help("Settings (⌘,)")
                        .keyboardShortcut(",", modifiers: .command)
                    }
                    
                    ToolbarItem(placement: .primaryAction) {
                        Button(action: {
                            KytyBridge.stopEmulation()
                        }) {
                            Label("Stop Game", systemImage: "xmark.circle.fill")
                        }
                        .tint(.red)
                        .disabled(KytyBridge.isStopping())
                        .help("Stop Emulation")
                    }
                }
            }
            .sheet(isPresented: $showLogSheet) {
                LogView()
            }
            .sheet(isPresented: $showSettingsSheet) {
                SettingsView()
            }
        } else {
            NavigationStack {
                libraryMainContent
                    .searchable(text: $searchText, prompt: "Search games, Title IDs...")
            }
        }
#else
        NavigationStack {
            libraryMainContent
                .searchable(text: $searchText, prompt: "Search games, Title IDs...")
        }
#endif
    }
    
    private var libraryMainContent: some View {
        ZStack {
            if games.isEmpty {
                VStack(spacing: 16) {
                    Image(systemName: "gamecontroller")
                        .font(.system(size: 64))
                        .foregroundColor(.secondary)
                    
                    Text("No Games Found")
                        .font(.title2)
                        .fontWeight(.bold)
                    
                    Text("Copy PS5 game folders or ELFs into the Metaspero folder in Files app, or tap '+' to import.")
                        .font(.subheadline)
                        .foregroundColor(.secondary)
                        .multilineTextAlignment(.center)
                        .padding(.horizontal, 32)
                    
                    Button(action: { showImporter = true }) {
                        Label("Import Game / Folder", systemImage: "plus.circle.fill")
                            .font(.headline)
                            .foregroundColor(.white)
                            .padding(.horizontal, 24)
                            .padding(.vertical, 12)
                            .background(Color.blue)
                            .clipShape(Capsule())
                    }
                    .buttonStyle(.plain)
                    .padding(.top, 8)
                }
            } else if filteredGames.isEmpty {
                VStack(spacing: 12) {
                    Image(systemName: "magnifyingglass")
                        .font(.system(size: 48))
                        .foregroundColor(.secondary)
                    Text("No Matching Games")
                        .font(.headline)
                    Text("No titles match '\(searchText)'.")
                        .font(.subheadline)
                        .foregroundColor(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if libraryViewMode == "list" {
                List {
                    ForEach(filteredGames) { game in
                        GameListRowView(
                            game: game,
                            onLaunch: { handleGameLaunchRequest(game) },
                            onShowDetails: { selectedGameForDetails = game },
                            onShowTrophies: { selectedGameForTrophies = game },
                            onRemove: {
                                gameToRemove = game
                                showRemoveConfirmation = true
                            }
                        )
                    }
                }
#if os(iOS)
                .listStyle(.insetGrouped)
#else
                .listStyle(.inset(alternatesRowBackgrounds: true))
#endif
            } else {
                ScrollView {
                    LazyVGrid(columns: columns, spacing: 18) {
                        ForEach(filteredGames) { game in
                            GameCardView(
                                game: game,
                                onLaunch: { handleGameLaunchRequest(game) },
                                onShowDetails: {
                                    selectedGameForDetails = game
                                },
                                onShowTrophies: {
                                    selectedGameForTrophies = game
                                },
                                onRemove: {
                                    gameToRemove = game
                                    showRemoveConfirmation = true
                                }
                            )
                        }
                    }
                    .padding(18)
                }
            }
        }
#if os(iOS)
        .background(Color(UIColor.systemGroupedBackground).ignoresSafeArea())
#else
        .background(Color(NSColor.windowBackgroundColor))
#endif
        .navigationTitle("Library")
        .toolbar {
#if os(iOS)
            ToolbarItemGroup(placement: .navigationBarLeading) {
                Button(action: loadGames) {
                    Image(systemName: "arrow.clockwise")
                }
                
                Button(action: {
                    libraryViewMode = (libraryViewMode == "grid" ? "list" : "grid")
                }) {
                    Image(systemName: libraryViewMode == "grid" ? "list.bullet" : "square.grid.2x2")
                }
                
                Button(action: { showTrophyOverview = true }) {
                    Image(systemName: "trophy")
                }
            }
            
            ToolbarItemGroup(placement: .navigationBarTrailing) {
                Button(action: { showLogSheet = true }) {
                    Image(systemName: "terminal.fill")
                        .font(.system(size: 15))
                }
                
                Button(action: { showImporter = true }) {
                    Image(systemName: "plus")
                        .font(.system(size: 17, weight: .semibold))
                }
            }
#else
            ToolbarItem(placement: .automatic) {
                Button(action: loadGames) {
                    Label("Refresh", systemImage: "arrow.clockwise")
                }
                .help("Refresh Library")
            }
            
            ToolbarItem(placement: .automatic) {
                Picker("View Mode", selection: $libraryViewMode) {
                    Image(systemName: "square.grid.2x2").tag("grid")
                    Image(systemName: "list.bullet").tag("list")
                }
                .pickerStyle(.segmented)
                .help("Toggle Grid / List View")
            }
            
            ToolbarItem(placement: .automatic) {
                Button(action: { showTrophyOverview = true }) {
                    Label("Trophies", systemImage: "trophy")
                }
                .help("Library Trophy Overview")
            }
            
            ToolbarItem(placement: .automatic) {
                Button(action: { showLogSheet = true }) {
                    Label("Logs", systemImage: "terminal.fill")
                }
                .help("View Logs & Diagnostics")
            }
            
            ToolbarItem(placement: .automatic) {
                Button(action: { showSettingsSheet = true }) {
                    Label("Settings", systemImage: "gearshape")
                }
                .help("Settings (⌘,)")
                .keyboardShortcut(",", modifiers: .command)
            }
            
            ToolbarItem(placement: .primaryAction) {
                Button(action: { showImporter = true }) {
                    Label("Import Game", systemImage: "plus")
                }
                .help("Import Game or Directory")
            }
#endif
        }
        .sheet(isPresented: $showTrophyOverview) {
            TrophyOverviewView(games: games)
        }
        .sheet(isPresented: $showLogSheet) {
            LogView()
        }
        .sheet(isPresented: $showSettingsSheet) {
            SettingsView()
        }
        .sheet(item: $selectedGameForDetails) { game in
            GameDetailsView(
                game: game,
                onLaunch: { handleGameLaunchRequest(game) },
                onViewTrophies: {
                    selectedGameForTrophies = game
                }
            )
        }
        .sheet(item: $selectedGameForTrophies) { game in
            TrophiesView(game: game)
        }
        .fileImporter(
            isPresented: $showImporter,
            allowedContentTypes: [.item, .folder, .data],
            allowsMultipleSelection: false
        ) { result in
            handleImport(result: result)
        }
        .alert(isPresented: $showRemoveConfirmation) {
            Alert(
                title: Text("Remove from Library"),
                message: Text("Remove '\(gameToRemove?.title ?? "this game")' from your library? Game files will NOT be deleted from your device."),
                primaryButton: .destructive(Text("Remove")) {
                    if let target = gameToRemove {
                        removeFromLibrary(target)
                    }
                },
                secondaryButton: .cancel()
            )
        }
        .alert("JIT Not Enabled", isPresented: $showJitWarning, presenting: pendingLaunchGame) { game in
            Button("Open StikDebug") {
                KytyBridge.openStikDebug()
            }
            Button("Try Anyway") {
                handleConfirmedLaunch(game)
            }
            Button("Cancel", role: .cancel) {
                pendingLaunchGame = nil
            }
        } message: { _ in
            Text(jitWarningMessage)
        }
        .alert("Game Still Closing", isPresented: $showSlowClosingAlert, presenting: pendingLaunchGame) { game in
            Button("Force Close & Launch", role: .destructive) {
                forceCloseAndLaunch(game)
            }
            Button("Wait and Launch") {
                queueGameLaunch(game)
            }
            Button("Cancel", role: .cancel) {
                pendingLaunchGame = nil
            }
        } message: { _ in
            Text("The previous game is still closing. You can wait a moment or force close it to launch immediately.")
        }
        .onReceive(NotificationCenter.default.publisher(for: NSNotification.Name.KytyEmulationStopped)) { _ in
            withAnimation(.easeInOut(duration: 0.2)) {
                isEmulating = false
            }
            if let queued = pendingLaunchGame {
                pendingLaunchGame = nil
                showSlowClosingAlert = false
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
                    handleConfirmedLaunch(queued)
                }
            }
        }
        .onAppear {
            loadGames()
            if KytyBridge.isProcessDebugged() {
                DispatchQueue.global(qos: .userInitiated).async {
                    _ = KytyBridge.refreshJitStatus()
                }
            }
        }
        .onChange(of: scenePhase) { newPhase in
            if newPhase == .active {
                handleAppActivation()
            }
        }
    }
    
    private func handleAppActivation() {
        DispatchQueue.global(qos: .userInitiated).async {
            if KytyBridge.isProcessDebugged() {
                let ok = KytyBridge.refreshJitStatus()
                if ok {
                    DispatchQueue.main.async {
                        if let game = pendingLaunchGame {
                            pendingLaunchGame = nil
                            handleConfirmedLaunch(game)
                        }
                    }
                }
            }
        }
    }
    
    private func handleGameLaunchRequest(_ game: GameItem) {
#if os(macOS)
        handleConfirmedLaunch(game)
#else
        if KytyBridge.isJitReady() || KytyBridge.isProcessDebugged() {
            handleConfirmedLaunch(game)
        } else {
            pendingLaunchGame = game
            showJitWarning = true
        }
#endif
    }
    
    private var jitWarningMessage: String {
        let pid = KytyBridge.processId()
        return "Metaspero requires JIT compilation to emulate games smoothly. JIT is currently not active.\n\nOpen StikDebug to enable JIT for this process (PID: \(pid)), or try launching anyway?"
    }

    private func forceCloseAndLaunch(_ game: GameItem) {
        KytyBridge.addLogMessage("[LibraryView] User selected force close for previous session to launch '\(game.title)'")
        pendingLaunchGame = game
        showSlowClosingAlert = false
        KytyBridge.forceResetEmulationSession()
    }
    
    private func queueGameLaunch(_ game: GameItem) {
        KytyBridge.addLogMessage("[LibraryView] Queued '\(game.title)' to launch after shutdown.")
    }

    private func handleConfirmedLaunch(_ game: GameItem) {
        if KytyBridge.isEmulationRunning() || KytyBridge.isStopping() {
            pendingLaunchGame = game
            showSlowClosingAlert = true
            KytyBridge.addLogMessage("[LibraryView] Previous session still closing. Offered launch queue for '\(game.title)'")
            return
        }
        _ = KytyBridge.acquireJitPool()
        selectedGame = game
        isEmulating = true
    }
    
    private func removeFromLibrary(_ game: GameItem) {
        games.removeAll { $0.id == game.id }
#if os(macOS)
        var savedPaths = UserDefaults.standard.stringArray(forKey: "KytyImportedGamePaths") ?? []
        savedPaths.removeAll { $0 == game.path }
        UserDefaults.standard.set(savedPaths, forKey: "KytyImportedGamePaths")
#endif
        KytyBridge.addLogMessage("[LibraryView] Removed '\(game.title)' from library")
    }
    
    private static let ignoredFolderNames: Set<String> = [
        "inbox", "frameworks", "library", "system", "caches", "tmp", "temp", "logs",
        "savedata", "shared", "preferences", "application support"
    ]
    
    private static func parseParamSfo(data: Data) -> (title: String?, titleId: String?, version: String?, firmware: String?) {
        guard data.count >= 20 else { return (nil, nil, nil, nil) }
        let magic = data.subdata(in: 0..<4)
        guard magic == Data([0x00, 0x50, 0x53, 0x46]) else { return (nil, nil, nil, nil) }
        
        let keyTableStart = Int(data.subdata(in: 8..<12).withUnsafeBytes { $0.load(as: UInt32.self) })
        let dataTableStart = Int(data.subdata(in: 12..<16).withUnsafeBytes { $0.load(as: UInt32.self) })
        let entriesCount = Int(data.subdata(in: 16..<20).withUnsafeBytes { $0.load(as: UInt32.self) })
        
        var title: String? = nil
        var titleId: String? = nil
        var version: String? = nil
        var firmware: String? = nil
        
        for i in 0..<entriesCount {
            let entryOffset = 20 + i * 16
            guard entryOffset + 16 <= keyTableStart, entryOffset + 16 <= data.count else { break }
            let keyOffset = Int(data.subdata(in: entryOffset..<entryOffset+2).withUnsafeBytes { $0.load(as: UInt16.self) })
            let dataLen = Int(data.subdata(in: entryOffset+4..<entryOffset+8).withUnsafeBytes { $0.load(as: UInt32.self) })
            let dataOffset = Int(data.subdata(in: entryOffset+12..<entryOffset+16).withUnsafeBytes { $0.load(as: UInt32.self) })
            
            let fullKeyOffset = keyTableStart + keyOffset
            guard fullKeyOffset < data.count else { continue }
            var keyEnd = fullKeyOffset
            while keyEnd < data.count && data[keyEnd] != 0 {
                keyEnd += 1
            }
            guard let key = String(data: data.subdata(in: fullKeyOffset..<keyEnd), encoding: .utf8) else { continue }
            
            let fullDataOffset = dataTableStart + dataOffset
            guard fullDataOffset + dataLen <= data.count else { continue }
            var valData = data.subdata(in: fullDataOffset..<fullDataOffset+dataLen)
            while let last = valData.last, last == 0 {
                valData.removeLast()
            }
            guard let val = String(data: valData, encoding: .utf8) else { continue }
            
            if key == "TITLE" && title == nil {
                title = val
            } else if key == "TITLE_ID" && titleId == nil {
                titleId = val
            } else if key == "APP_VER" && version == nil {
                version = val
            } else if key == "SYSTEM_VER" && firmware == nil {
                firmware = val
            }
        }
        return (title, titleId, version, firmware)
    }
    
    private func loadGames() {
        var items: [GameItem] = []
        let fileManager = FileManager.default
        
#if os(macOS)
        // On macOS: load in-place imported games without copying files
        var validSavedPaths: [String] = []
        let savedPaths = UserDefaults.standard.stringArray(forKey: "KytyImportedGamePaths") ?? []
        for path in savedPaths {
            let file = URL(fileURLWithPath: path)
            var isDir: ObjCBool = false
            guard fileManager.fileExists(atPath: file.path, isDirectory: &isDir) else {
                continue
            }
            validSavedPaths.append(path)
            if isDir.boolValue {
                if let item = parseGameDirectory(url: file) {
                    items.append(item)
                }
            } else {
                let name = file.lastPathComponent
                let ext = file.pathExtension.lowercased()
                if ext == "elf" || ext == "self" || ext == "zar" || (ext == "bin" && name.lowercased() == "eboot.bin") {
                    let sizeStr: String
                    if let resourceValues = try? file.resourceValues(forKeys: [.fileSizeKey]),
                       let size = resourceValues.fileSize {
                        sizeStr = ByteCountFormatter.string(fromByteCount: Int64(size), countStyle: .file)
                    } else {
                        sizeStr = "ELF File"
                    }
                    
                    items.append(GameItem(
                        title: file.deletingPathExtension().lastPathComponent,
                        titleId: ext.uppercased(),
                        appVersion: "Standalone",
                        rawVersion: "Standalone",
                        firmwareVersion: "N/A",
                        path: file.path,
                        sizeString: sizeStr,
                        isDirectory: false,
                        iconPath: nil,
                        isValidGame: true
                    ))
                }
            }
        }
        if validSavedPaths.count != savedPaths.count {
            UserDefaults.standard.set(validSavedPaths, forKey: "KytyImportedGamePaths")
        }

        if let appSupportURL = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask).first?.appendingPathComponent("Metaspero") {
            try? fileManager.createDirectory(at: appSupportURL, withIntermediateDirectories: true)
            do {
                let files = try fileManager.contentsOfDirectory(at: appSupportURL, includingPropertiesForKeys: [.fileSizeKey, .isDirectoryKey])
                for file in files {
                    if items.contains(where: { $0.path == file.path }) { continue }
                    let name = file.lastPathComponent
                    if name.hasPrefix(".") || name.hasPrefix("_") { continue }
                    if Self.ignoredFolderNames.contains(name.lowercased()) { continue }
                    
                    var isDir: ObjCBool = false
                    fileManager.fileExists(atPath: file.path, isDirectory: &isDir)
                    
                    if isDir.boolValue {
                        if let item = parseGameDirectory(url: file) {
                            items.append(item)
                        }
                    } else {
                        let ext = file.pathExtension.lowercased()
                        if ext == "elf" || ext == "self" || ext == "zar" || (ext == "bin" && name.lowercased() == "eboot.bin") {
                            let sizeStr: String
                            if let resourceValues = try? file.resourceValues(forKeys: [.fileSizeKey]),
                               let size = resourceValues.fileSize {
                                sizeStr = ByteCountFormatter.string(fromByteCount: Int64(size), countStyle: .file)
                            } else {
                                sizeStr = "ELF File"
                            }
                            
                            items.append(GameItem(
                                title: file.deletingPathExtension().lastPathComponent,
                                titleId: ext.uppercased(),
                                appVersion: "Standalone",
                                rawVersion: "Standalone",
                                firmwareVersion: "N/A",
                                path: file.path,
                                sizeString: sizeStr,
                                isDirectory: false,
                                iconPath: nil,
                                isValidGame: true
                            ))
                        }
                    }
                }
            } catch {
                NSLog("[LibraryView] Error enumerating Application Support: %@", error.localizedDescription)
            }
        }
#else
        if let documentsURL = fileManager.urls(for: .documentDirectory, in: .userDomainMask).first {
            do {
                let files = try fileManager.contentsOfDirectory(at: documentsURL, includingPropertiesForKeys: [.fileSizeKey, .isDirectoryKey])
                for file in files {
                    if items.contains(where: { $0.path == file.path }) { continue }
                    let name = file.lastPathComponent
                    if name.hasPrefix(".") || name.hasPrefix("_") { continue }
                    if Self.ignoredFolderNames.contains(name.lowercased()) { continue }
                    
                    var isDir: ObjCBool = false
                    fileManager.fileExists(atPath: file.path, isDirectory: &isDir)
                    
                    if isDir.boolValue {
                        // Check for PS5 / PS4 game directory structure
                        if let item = parseGameDirectory(url: file) {
                            items.append(item)
                        }
                    } else {
                        let ext = file.pathExtension.lowercased()
                        if ext == "elf" || ext == "self" || ext == "zar" || (ext == "bin" && name.lowercased() == "eboot.bin") {
                            let sizeStr: String
                            if let resourceValues = try? file.resourceValues(forKeys: [.fileSizeKey]),
                               let size = resourceValues.fileSize {
                                sizeStr = ByteCountFormatter.string(fromByteCount: Int64(size), countStyle: .file)
                            } else {
                                sizeStr = "ELF File"
                            }
                            
                            items.append(GameItem(
                                title: file.deletingPathExtension().lastPathComponent,
                                titleId: ext.uppercased(),
                                appVersion: "Standalone",
                                rawVersion: "Standalone",
                                firmwareVersion: "N/A",
                                path: file.path,
                                sizeString: sizeStr,
                                isDirectory: false,
                                iconPath: nil,
                                isValidGame: true
                            ))
                        }
                    }
                }
            } catch {
                NSLog("[LibraryView] Error enumerating documents: %@", error.localizedDescription)
            }
        }
#endif
        
        // Sort alphabetically by title
        items.sort { $0.title.localizedCaseInsensitiveCompare($1.title) == .orderedAscending }
        
        self.games = items
        KytyBridge.addLogMessage("[LibraryView] Discovered \(items.count) playable games")
    }
    
    private func parseGameDirectory(url: URL) -> GameItem? {
        let fileManager = FileManager.default
        let folderPath = url.path
        
        // 1. Check for standard PS5/PS4 executable candidates
        let candidateExecutables = [
            (folderPath as NSString).appendingPathComponent("eboot.bin"),
            (folderPath as NSString).appendingPathComponent("app0/eboot.bin"),
            (folderPath as NSString).appendingPathComponent("app0/eboot.elf"),
            (folderPath as NSString).appendingPathComponent("app0/eboot.self")
        ]
        
        var hasEboot = false
        for candidate in candidateExecutables {
            if fileManager.fileExists(atPath: candidate) {
                hasEboot = true
                break
            }
        }
        
        // Check for any root-level .elf, .self, or .zar
        var hasElf = false
        if let subfiles = try? fileManager.contentsOfDirectory(atPath: folderPath) {
            for sub in subfiles {
                let ext = (sub as NSString).pathExtension.lowercased()
                if ext == "elf" || ext == "self" || ext == "zar" {
                    hasElf = true
                    break
                }
            }
        }
        
        // 2. Parse title name, title ID, and versions from param.json or param.sfo
        var titleName = url.lastPathComponent
        var titleId = ""
        var appVersion = ""
        var firmwareVersion = "N/A"
        var hasParam = false
        
        // PS5 param.json
        var paramJsonPath = (folderPath as NSString).appendingPathComponent("sce_sys/param.json")
        if !fileManager.fileExists(atPath: paramJsonPath) {
            paramJsonPath = (folderPath as NSString).appendingPathComponent("app0/sce_sys/param.json")
        }
        
        if fileManager.fileExists(atPath: paramJsonPath),
           let data = try? Data(contentsOf: URL(fileURLWithPath: paramJsonPath)),
           let rawJson = try? JSONSerialization.jsonObject(with: data, options: []),
           let json = rawJson as? [String: Any] {
            hasParam = true
            if let tid = json["titleId"] as? String {
                titleId = tid.trimmingCharacters(in: .whitespaces)
            }
            if let ver = json["appVersion"] as? String {
                appVersion = ver.trimmingCharacters(in: .whitespaces)
            } else if let ver = json["contentVersion"] as? String {
                appVersion = ver.trimmingCharacters(in: .whitespaces)
            }
            
            // Required system software version (firmware version)
            if let fwHex = json["requiredSystemSoftwareVersion"] as? String {
                let cleanHex = fwHex.trimmingCharacters(in: .whitespacesAndNewlines)
                let pattern = "^0[xX]([0-9]{6})[0-9A-Fa-f]{10}$"
                if let regex = try? NSRegularExpression(pattern: pattern),
                   let match = regex.firstMatch(in: cleanHex, range: NSRange(cleanHex.startIndex..., in: cleanHex)),
                   let r = Range(match.range(at: 1), in: cleanHex) {
                    let digits = String(cleanHex[r])
                    if digits.count == 6 {
                        let majorStr = String(digits.prefix(2))
                        let minorStr = String(digits.dropFirst(2).prefix(2))
                        let patchStr = String(digits.suffix(2))
                        if let major = Int(majorStr) {
                            var v = "\(major).\(minorStr)"
                            if patchStr != "00" {
                                v += ".\(patchStr)"
                            }
                            firmwareVersion = v
                        }
                    }
                } else if !cleanHex.isEmpty {
                    firmwareVersion = cleanHex
                }
            }
            
            if let localized = json["localizedParameters"] as? [String: Any] {
                if let defaultLang = localized["defaultLanguage"] as? String,
                   let langObj = localized[defaultLang] as? [String: Any],
                   let name = langObj["titleName"] as? String, !name.isEmpty {
                    titleName = name
                } else if let usObj = localized["en-US"] as? [String: Any],
                          let name = usObj["titleName"] as? String, !name.isEmpty {
                    titleName = name
                } else {
                    for (_, val) in localized {
                        if let langObj = val as? [String: Any],
                           let name = langObj["titleName"] as? String, !name.isEmpty {
                            titleName = name
                            break
                        }
                    }
                }
            }
        }
        
        // PS4 param.sfo
        if !hasParam {
            var paramSfoPath = (folderPath as NSString).appendingPathComponent("sce_sys/param.sfo")
            if !fileManager.fileExists(atPath: paramSfoPath) {
                paramSfoPath = (folderPath as NSString).appendingPathComponent("app0/sce_sys/param.sfo")
            }
            if fileManager.fileExists(atPath: paramSfoPath),
               let data = try? Data(contentsOf: URL(fileURLWithPath: paramSfoPath)) {
                hasParam = true
                let sfoInfo = Self.parseParamSfo(data: data)
                if let t = sfoInfo.title, !t.isEmpty {
                    titleName = t
                }
                if let tid = sfoInfo.titleId, !tid.isEmpty {
                    titleId = tid
                }
                if let v = sfoInfo.version, !v.isEmpty {
                    appVersion = v
                }
                if let fw = sfoInfo.firmware, !fw.isEmpty {
                    firmwareVersion = fw
                }
            }
        }
        
        // Must contain an executable or valid param metadata
        guard hasEboot || hasElf || hasParam || !titleId.isEmpty else {
            return nil
        }
        
        // 3. Look for cover artwork (sce_sys/icon0.png or root icon0.png)
        var iconPath: String? = nil
        let candidateIcons = [
            (folderPath as NSString).appendingPathComponent("sce_sys/icon0.png"),
            (folderPath as NSString).appendingPathComponent("app0/sce_sys/icon0.png"),
            (folderPath as NSString).appendingPathComponent("icon0.png"),
            (folderPath as NSString).appendingPathComponent("app0/icon0.png")
        ]
        for candidate in candidateIcons {
            if fileManager.fileExists(atPath: candidate) {
                iconPath = candidate
                break
            }
        }
        
        // Compute folder size
        let sizeStr = computeDirectorySize(url: url)
        
        var cleanTitleId = titleId.trimmingCharacters(in: .whitespacesAndNewlines)
        if cleanTitleId.hasSuffix("-app0") {
            cleanTitleId = String(cleanTitleId.dropLast(5))
        }
        if cleanTitleId.isEmpty {
            cleanTitleId = hasEboot ? "PS5 GAME" : "APP"
        }
        
        var cleanVersion = appVersion.trimmingCharacters(in: .whitespacesAndNewlines)
        if cleanVersion.isEmpty {
            cleanVersion = "1.00"
        }
        
        return GameItem(
            title: titleName,
            titleId: cleanTitleId,
            appVersion: cleanVersion,
            rawVersion: appVersion.isEmpty ? cleanVersion : appVersion,
            firmwareVersion: firmwareVersion,
            path: folderPath,
            sizeString: sizeStr,
            isDirectory: true,
            iconPath: iconPath,
            isValidGame: true
        )
    }
    
    private func computeDirectorySize(url: URL) -> String {
        let fileManager = FileManager.default
        guard let enumerator = fileManager.enumerator(at: url, includingPropertiesForKeys: [.fileSizeKey], options: [.skipsHiddenFiles]) else {
            return "Folder"
        }
        var totalSize: Int64 = 0
        for case let fileURL as URL in enumerator {
            if let size = (try? fileURL.resourceValues(forKeys: [.fileSizeKey]))?.fileSize {
                totalSize += Int64(size)
            }
        }
        return ByteCountFormatter.string(fromByteCount: totalSize, countStyle: .file)
    }
    
    private func handleImport(result: Result<[URL], Error>) {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return }
            guard url.startAccessingSecurityScopedResource() else { return }
            defer { url.stopAccessingSecurityScopedResource() }
            
#if os(macOS)
            // On macOS: Store the path to read in-place without copying files or duplicating storage
            var savedPaths = UserDefaults.standard.stringArray(forKey: "KytyImportedGamePaths") ?? []
            if !savedPaths.contains(url.path) {
                savedPaths.append(url.path)
                UserDefaults.standard.set(savedPaths, forKey: "KytyImportedGamePaths")
            }
            loadGames()
#else
            // On iOS: Copy to app Documents container due to app sandbox constraints
            let fileManager = FileManager.default
            guard let dest = fileManager.urls(for: .documentDirectory, in: .userDomainMask).first?.appendingPathComponent(url.lastPathComponent) else { return }
            
            try? fileManager.copyItem(at: url, to: dest)
            loadGames()
#endif
        case .failure(let error):
            NSLog("[LibraryView] File import failed: %@", error.localizedDescription)
        }
    }
}

// MARK: - Game Card View

struct GameCardView: View {
    let game: GameItem
    let onLaunch: () -> Void
    let onShowDetails: () -> Void
    let onShowTrophies: () -> Void
    let onRemove: () -> Void
    
    var body: some View {
        Button(action: onLaunch) {
            VStack(alignment: .leading, spacing: 8) {
                // Game Cover / Artwork
                ZStack {
#if os(iOS)
                    if let iconPath = game.iconPath,
                       let uiImage = UIImage(contentsOfFile: iconPath) {
                        Image(uiImage: uiImage)
                            .resizable()
                            .aspectRatio(contentMode: .fill)
                            .frame(height: 140)
                            .clipped()
                            .cornerRadius(12)
                    } else {
                        LinearGradient(
                            colors: [Color.blue.opacity(0.8), Color.purple.opacity(0.9)],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        )
                        .cornerRadius(12)
                        
                        VStack(spacing: 8) {
                            Image(systemName: game.isDirectory ? "folder.fill" : "cpu.fill")
                                .font(.system(size: 38))
                                .foregroundColor(.white.opacity(0.9))
                            
                            Text(game.titleId)
                                .font(.system(size: 11, weight: .bold, design: .monospaced))
                                .foregroundColor(.white.opacity(0.85))
                        }
                    }
#else
                    if let iconPath = game.iconPath,
                       let nsImage = NSImage(contentsOfFile: iconPath) {
                        Image(nsImage: nsImage)
                            .resizable()
                            .aspectRatio(contentMode: .fill)
                            .frame(height: 140)
                            .clipped()
                            .cornerRadius(12)
                    } else {
                        LinearGradient(
                            colors: [Color.blue.opacity(0.8), Color.purple.opacity(0.9)],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        )
                        .cornerRadius(12)
                        
                        VStack(spacing: 8) {
                            Image(systemName: game.isDirectory ? "folder.fill" : "cpu.fill")
                                .font(.system(size: 38))
                                .foregroundColor(.white.opacity(0.9))
                            
                            Text(game.titleId)
                                .font(.system(size: 11, weight: .bold, design: .monospaced))
                                .foregroundColor(.white.opacity(0.85))
                        }
                    }
#endif
                }
                .frame(height: 140)
                .shadow(color: Color.black.opacity(0.18), radius: 6, x: 0, y: 3)
                
                // Game Info
                VStack(alignment: .leading, spacing: 4) {
                    Text(game.title)
                        .font(.system(size: 13, weight: .bold))
                        .foregroundColor(.primary)
                        .lineLimit(1)
                        .truncationMode(.tail)
                    
                    HStack(spacing: 4) {
                        Text(game.titleId)
                            .font(.system(size: 9, design: .monospaced))
                            .foregroundColor(.secondary)
                            .lineLimit(1)
                            .truncationMode(.tail)
                        
                        Spacer()
                        
                        Text(game.sizeString)
                            .font(.system(size: 9))
                            .foregroundColor(.secondary)
                            .lineLimit(1)
                            .layoutPriority(1)
                    }
                }
                .padding(.horizontal, 4)
            }
        }
        .buttonStyle(PlainButtonStyle())
        .contextMenu {
            Button(action: onLaunch) {
                Label("Launch Game", systemImage: "play.fill")
            }
            
            Button(action: onShowDetails) {
                Label("Game Details", systemImage: "info.circle")
            }
            
            Button(action: onShowTrophies) {
                Label("View Trophies", systemImage: "trophy.fill")
            }
            
            Button(action: {
#if os(iOS)
                UIPasteboard.general.string = game.path
#else
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString(game.path, forType: .string)
#endif
            }) {
                Label("Copy Path", systemImage: "doc.on.doc")
            }
            
            Divider()
            
            Button(role: .destructive, action: onRemove) {
                Label("Remove from Library", systemImage: "minus.circle")
            }
        }
    }
}

// MARK: - Game List Row View

struct GameListRowView: View {
    let game: GameItem
    let onLaunch: () -> Void
    let onShowDetails: () -> Void
    let onShowTrophies: () -> Void
    let onRemove: () -> Void
    
    var body: some View {
        Button(action: onLaunch) {
            HStack(spacing: 12) {
                // Mini Artwork
                Group {
#if os(iOS)
                    if let iconPath = game.iconPath, let uiImage = UIImage(contentsOfFile: iconPath) {
                        Image(uiImage: uiImage)
                            .resizable()
                            .aspectRatio(contentMode: .fill)
                            .frame(width: 44, height: 44)
                            .cornerRadius(8)
                    } else {
                        Image(systemName: "gamecontroller.fill")
                            .font(.system(size: 20))
                            .foregroundColor(.blue)
                            .frame(width: 44, height: 44)
                    }
#else
                    if let iconPath = game.iconPath, let nsImage = NSImage(contentsOfFile: iconPath) {
                        Image(nsImage: nsImage)
                            .resizable()
                            .aspectRatio(contentMode: .fill)
                            .frame(width: 44, height: 44)
                            .cornerRadius(8)
                    } else {
                        Image(systemName: "gamecontroller.fill")
                            .font(.system(size: 20))
                            .foregroundColor(.blue)
                            .frame(width: 44, height: 44)
                    }
#endif
                }
                
                VStack(alignment: .leading, spacing: 3) {
                    Text(game.title)
                        .font(.system(size: 14, weight: .semibold))
                        .foregroundColor(.primary)
                        .lineLimit(1)
                    
                    HStack(spacing: 8) {
                        Text(game.titleId)
                            .font(.system(size: 11, design: .monospaced))
                            .foregroundColor(.blue)
                        
                        Text("v\(game.rawVersion)")
                            .font(.system(size: 11))
                            .foregroundColor(.secondary)
                        
                        Text(game.sizeString)
                            .font(.system(size: 11))
                            .foregroundColor(.secondary)
                    }
                }
                
                Spacer()
                
                Button(action: onShowDetails) {
                    Image(systemName: "ellipsis.circle")
                        .font(.system(size: 18))
                        .foregroundColor(.secondary)
                }
                .buttonStyle(.plain)
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .contextMenu {
            Button(action: onLaunch) {
                Label("Launch Game", systemImage: "play.fill")
            }
            Button(action: onShowDetails) {
                Label("Game Details", systemImage: "info.circle")
            }
            Button(action: onShowTrophies) {
                Label("View Trophies", systemImage: "trophy.fill")
            }
            Divider()
            Button(role: .destructive, action: onRemove) {
                Label("Remove from Library", systemImage: "minus.circle")
            }
        }
    }
}
