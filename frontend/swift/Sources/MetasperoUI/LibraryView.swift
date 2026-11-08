// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI
#if os(iOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif
#if !SKIP
import UniformTypeIdentifiers
#endif

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

    @Environment(\.scenePhase) var scenePhase: ScenePhase
    
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
                        #if !SKIP
                        .help("View Logs & Diagnostics")
                        #endif
                    }
                    
                    ToolbarItem(placement: .automatic) {
                        Button(action: { showSettingsSheet = true }) {
                            Label("Settings", systemImage: "gearshape")
                        }
                        #if !SKIP
                        .help("Settings (⌘,)")
                        #endif
                        #if !SKIP
                .keyboardShortcut(",", modifiers: .command)
                #endif
                    }
                    
                    ToolbarItem(placement: .primaryAction) {
                        Button(action: {
                            KytyBridge.stopEmulation()
                        }) {
                            Label("Stop Game", systemImage: "xmark.circle.fill")
                        }
                        .tint(.red)
                        .disabled(KytyBridge.isStopping())
                        #if !SKIP
                        .help("Stop Emulation")
                        #endif
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
                    PlatformIcon("gamecontroller", size: 64)
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
                        PlatformLabel("Import Game / Folder", systemImage: "plus.circle.fill")
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
                    PlatformIcon("magnifyingglass", size: 48)
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
#if os(iOS) || SKIP
                .listStyle(.plain)
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
#if os(iOS) && !SKIP
        .background(Color(UIColor.systemGroupedBackground).ignoresSafeArea())
#elseif os(iOS)
        .background(Color.black.ignoresSafeArea())
#elseif os(macOS)
        .background(Color(NSColor.windowBackgroundColor))
#endif
        .navigationTitle("Library")
        .toolbar {
#if os(iOS) || SKIP
            ToolbarItemGroup(placement: .navigationBarLeading) {
                Button(action: loadGames) {
                    PlatformIcon("arrow.clockwise")
                }
                
                Button(action: {
                    libraryViewMode = (libraryViewMode == "grid" ? "list" : "grid")
                }) {
                    PlatformIcon(libraryViewMode == "grid" ? "list.bullet" : "square.grid.2x2")
                }
                
                Button(action: { showTrophyOverview = true }) {
                    PlatformIcon("trophy")
                }
            }
            
            ToolbarItemGroup(placement: .navigationBarTrailing) {
                Button(action: { showLogSheet = true }) {
                    PlatformIcon("terminal.fill")
                }
                
                Button(action: {
                    #if SKIP
                    KytyBridge.requestPickFolder()
                    #else
                    showImporter = true
                    #endif
                }) {
                    PlatformIcon("plus", size: 20)
                }
            }
#else
            ToolbarItem(placement: .automatic) {
                Button(action: loadGames) {
                    Label("Refresh", systemImage: "arrow.clockwise")
                }
                #if !SKIP
                .help("Refresh Library")
                #endif
            }
            
            ToolbarItem(placement: .automatic) {
                Picker("View Mode", selection: $libraryViewMode) {
                    Image(systemName: "square.grid.2x2").tag("grid")
                    Image(systemName: "list.bullet").tag("list")
                }
                .pickerStyle(.segmented)
                #if !SKIP
                .help("Toggle Grid / List View")
                #endif
            }
            
            ToolbarItem(placement: .automatic) {
                Button(action: { showTrophyOverview = true }) {
                    Label("Trophies", systemImage: "trophy")
                }
                #if !SKIP
                .help("Library Trophy Overview")
                #endif
            }
            
            ToolbarItem(placement: .automatic) {
                Button(action: { showLogSheet = true }) {
                    Label("Logs", systemImage: "terminal.fill")
                }
                #if !SKIP
                .help("View Logs & Diagnostics")
                #endif
            }
            
            ToolbarItem(placement: .automatic) {
                Button(action: { showSettingsSheet = true }) {
                    Label("Settings", systemImage: "gearshape")
                }
                #if !SKIP
                .help("Settings (⌘,)")
                #endif
                #if !SKIP
                .keyboardShortcut(",", modifiers: .command)
                #endif
            }
            
            ToolbarItem(placement: .primaryAction) {
                Button(action: { showImporter = true }) {
                    Label("Import Game", systemImage: "plus")
                }
                #if !SKIP
                .help("Import Game or Directory")
                #endif
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
        #if !SKIP
        .fileImporter(
            isPresented: $showImporter,
            allowedContentTypes: [.item, .folder, .data],
            allowsMultipleSelection: false
        ) { result in
            handleImport(result: result)
        }
#endif
        .alert("Remove from Library", isPresented: $showRemoveConfirmation) {
            Button("Remove", role: .destructive) {
                if let target = gameToRemove {
                    removeFromLibrary(target)
                }
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Remove '\(gameToRemove?.title ?? "this game")' from your library? Game files will NOT be deleted from your device.")
        }
        .alert("JIT Not Enabled", isPresented: $showJitWarning) {
            Button("Open StikDebug") {
                KytyBridge.openStikDebug()
            }
            if let game = pendingLaunchGame {
                Button("Try Anyway") {
                    handleConfirmedLaunch(game)
                }
            }
            Button("Cancel", role: .cancel) {
                pendingLaunchGame = nil
            }
        } message: {
            Text(jitWarningMessage)
        }
        .alert("Game Still Closing", isPresented: $showSlowClosingAlert) {
            if let game = pendingLaunchGame {
                Button("Force Close & Launch", role: .destructive) {
                    forceCloseAndLaunch(game)
                }
                Button("Wait and Launch") {
                    queueGameLaunch(game)
                }
            }
            Button("Cancel", role: .cancel) {
                pendingLaunchGame = nil
            }
        } message: {
            Text("The previous game is still closing. You can wait a moment or force close it to launch immediately.")
        }
        .onReceive(NotificationCenter.default.publisher(for: .KytyEmulationStopped)) { _ in
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
        .onReceive(NotificationCenter.default.publisher(for: Notification.Name("KytyGamePathsChanged"))) { _ in
            loadGames()
        }
        .onAppear {
            loadGames()
            if KytyBridge.isProcessDebugged() {
                DispatchQueue.global().async {
                    _ = KytyBridge.refreshJitStatus()
                }
            }
        }
        .onChange(of: scenePhase) { newPhase in
            if newPhase == ScenePhase.active {
                handleAppActivation()
            }
        }
    }
    
    private func handleAppActivation() {
        DispatchQueue.global().async {
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
        #if SKIP
        if let onLaunch = KytyBridge.onLaunchGame {
            onLaunch(game.path)
            return
        }
        #endif
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
        #if !SKIP
        var savedPaths = UserDefaults.standard.stringArray(forKey: "KytyImportedGamePaths") ?? []
        #else
        var savedPaths: [String] = []
        #endif
        savedPaths.removeAll { $0 == game.path }
        UserDefaults.standard.set(savedPaths, forKey: "KytyImportedGamePaths")
#endif
        KytyBridge.addLogMessage("[LibraryView] Removed '\(game.title)' from library")
    }
    
    private static let ignoredFolderNames: Set<String> = [
        "inbox", "frameworks", "library", "system", "caches", "tmp", "temp", "logs",
        "savedata", "shared", "preferences", "application support"
    ]
    
    private static func readU16(_ data: Data, _ offset: Int) -> Int {
        guard offset + 2 <= data.count else { return 0 }
        return Int(data[offset]) | (Int(data[offset + 1]) << 8)
    }
    
    private static func readU32(_ data: Data, _ offset: Int) -> Int {
        guard offset + 4 <= data.count else { return 0 }
        return Int(data[offset]) | (Int(data[offset + 1]) << 8) | (Int(data[offset + 2]) << 16) | (Int(data[offset + 3]) << 24)
    }
    
    private static func parseParamSfo(data: Data) -> (title: String?, titleId: String?, version: String?, firmware: String?) {
        guard data.count >= 20 else { return (nil, nil, nil, nil) }
        guard Int(data[0]) == 0x00 && Int(data[1]) == 0x50 && Int(data[2]) == 0x53 && Int(data[3]) == 0x46 else { return (nil, nil, nil, nil) }
        
        let keyTableStart = readU32(data, 8)
        let dataTableStart = readU32(data, 12)
        let entriesCount = readU32(data, 16)
        
        var title: String? = nil
        var titleId: String? = nil
        var version: String? = nil
        var firmware: String? = nil
        
        for i in 0..<entriesCount {
            let entryOffset = 20 + i * 16
            guard entryOffset + 16 <= keyTableStart, entryOffset + 16 <= data.count else { break }
            let keyOffset = readU16(data, entryOffset)
            let dataLen = readU32(data, entryOffset + 4)
            let dataOffset = readU32(data, entryOffset + 12)
            
            let fullKeyOffset = keyTableStart + keyOffset
            guard fullKeyOffset < data.count else { continue }
            var keyEnd = fullKeyOffset
            while keyEnd < data.count && Int(data[keyEnd]) != 0 {
                keyEnd += 1
            }
            guard let key = String(data: data.subdata(in: fullKeyOffset..<keyEnd), encoding: .utf8) else { continue }
            
            let fullDataOffset = dataTableStart + dataOffset
            guard fullDataOffset + dataLen <= data.count else { continue }
            var len = dataLen
            while len > 0 && Int(data[fullDataOffset + len - 1]) == 0 {
                len -= 1
            }
            guard let valStr = String(data: data.subdata(in: fullDataOffset..<fullDataOffset+len), encoding: .utf8) else { continue }
            
            if key == "TITLE" && title == nil {
                title = valStr
            } else if key == "TITLE_ID" && titleId == nil {
                titleId = valStr
            } else if key == "APP_VER" && version == nil {
                version = valStr
            } else if key == "SYSTEM_VER" && firmware == nil {
                firmware = valStr
            }
        }
        return (title, titleId, version, firmware)
    }
    
    private static func joinPath(_ base: String, _ component: String) -> String {
        if base.hasSuffix("/") {
            return base + component
        } else {
            return base + "/" + component
        }
    }

    private static func pathLastComponent(_ path: String) -> String {
        let parts = path.split(separator: "/")
        if let last = parts.last {
            return String(last)
        }
        return path
    }

    private static func pathExtension(_ path: String) -> String {
        let name = pathLastComponent(path)
        guard let dotIndex = name.lastIndex(of: ".") else { return "" }
        return String(name[name.index(after: dotIndex)...])
    }

    private static func pathDeletingExtension(_ path: String) -> String {
        let name = pathLastComponent(path)
        guard let dotIndex = name.lastIndex(of: ".") else { return name }
        return String(name[..<dotIndex])
    }

    private static func readFileData(path: String) -> Data? {
        #if !SKIP
        return try? Data(contentsOf: URL(fileURLWithPath: path))
        #else
        return try? Data(contentsOfFile: path)
        #endif
    }

    private func loadGames() {
        var items: [GameItem] = []
        let fileManager = FileManager.default
        
#if os(macOS)
        // On macOS: load in-place imported games without copying files
        var validSavedPaths: [String] = []
        #if !SKIP
        let savedPaths = UserDefaults.standard.stringArray(forKey: "KytyImportedGamePaths") ?? []
        #else
        let savedPaths: [String] = []
        #endif
        for path in savedPaths {
            let file = URL(fileURLWithPath: path)
            var isDir: ObjCBool = false
            guard fileManager.fileExists(atPath: file.path, isDirectory: &isDir) else {
                continue
            }
            validSavedPaths.append(path)
            if isDir.boolValue {
                if let item = parseGameDirectory(folderPath: file.path) {
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
                        if let item = parseGameDirectory(folderPath: file.path) {
                            items.append(item)
                        }
                    } else {
                        let ext = file.pathExtension.lowercased()
                        if ext == "elf" || ext == "self" || ext == "zar" || (ext == "bin" && name.lowercased() == "eboot.bin") {
                            let sizeStr: String
                            #if !SKIP
                            if let resourceValues = try? file.resourceValues(forKeys: [.fileSizeKey]),
                               let size = resourceValues.fileSize {
                                sizeStr = ByteCountFormatter.string(fromByteCount: Int64(size), countStyle: .file)
                            } else {
                                sizeStr = "ELF File"
                            }
                            #else
                            sizeStr = "ELF File"
                            #endif
                            
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
        var directoriesToScan: [URL] = []
        #if !SKIP
        if let documentsURL = fileManager.urls(for: .documentDirectory, in: .userDomainMask).first {
            directoriesToScan.append(documentsURL)
        }
        #else
        var seenDirPaths = Set<String>()
        for customPath in KytyBridge.customGamePaths {
            if !seenDirPaths.contains(customPath) {
                seenDirPaths.insert(customPath)
                directoriesToScan.append(URL(fileURLWithPath: customPath))
            }
        }
        #endif

        for dir in directoriesToScan {
            guard fileManager.fileExists(atPath: dir.path) else { continue }
            guard let subpaths = try? fileManager.contentsOfDirectory(atPath: dir.path) else { continue }
            for sub in subpaths {
                let filePath = Self.joinPath(dir.path, sub)
                if items.contains(where: { $0.path == filePath }) { continue }
                let name = Self.pathLastComponent(filePath)
                if name.hasPrefix(".") || name.hasPrefix("_") { continue }
                if Self.ignoredFolderNames.contains(name.lowercased()) { continue }
                
                if let item = parseGameDirectory(folderPath: filePath) {
                    if !items.contains(where: { $0.path == item.path || (!item.titleId.isEmpty && $0.titleId == item.titleId) }) {
                        items.append(item)
                    }
                } else {
                    let ext = Self.pathExtension(filePath).lowercased()
                    if ext == "elf" || ext == "self" || ext == "zar" || (ext == "bin" && name.lowercased() == "eboot.bin") {
                        items.append(GameItem(
                            title: Self.pathDeletingExtension(name),
                            titleId: ext.uppercased(),
                            appVersion: "Standalone",
                            rawVersion: "Standalone",
                            firmwareVersion: "N/A",
                            path: filePath,
                            sizeString: "ELF File",
                            isDirectory: false,
                            iconPath: nil,
                            isValidGame: true
                        ))
                    }
                }
            }
        }
#endif
        
        // Sort alphabetically by title
        items.sort { $0.title.localizedCaseInsensitiveCompare($1.title) == .orderedAscending }
        
        self.games = items
        KytyBridge.addLogMessage("[LibraryView] Discovered \(items.count) playable games")
    }
    
    private func parseGameDirectory(folderPath: String) -> GameItem? {
        let fileManager = FileManager.default
        
        // 1. Check for standard PS5/PS4 executable candidates
        let candidateExecutables: [String] = [
            Self.joinPath(folderPath, "eboot.bin"),
            Self.joinPath(folderPath, "app0/eboot.bin"),
            Self.joinPath(folderPath, "app0/eboot.elf"),
            Self.joinPath(folderPath, "app0/eboot.self")
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
                let ext = Self.pathExtension(sub).lowercased()
                if ext == "elf" || ext == "self" || ext == "zar" {
                    hasElf = true
                    break
                }
            }
        }
        
        // 2. Parse title name, title ID, and versions from param.json or param.sfo
        var titleName = Self.pathLastComponent(folderPath)
        var titleId = ""
        var appVersion = ""
        var firmwareVersion = "N/A"
        var hasParam = false
        
        // PS5 param.json
        var paramJsonPath = Self.joinPath(folderPath, "sce_sys/param.json")
        if !fileManager.fileExists(atPath: paramJsonPath) {
            paramJsonPath = Self.joinPath(folderPath, "app0/sce_sys/param.json")
        }
        
        if fileManager.fileExists(atPath: paramJsonPath),
           let data = Self.readFileData(path: paramJsonPath),
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
                var cleanHex = fwHex.trimmingCharacters(in: .whitespacesAndNewlines)
                if cleanHex.hasPrefix("0x") || cleanHex.hasPrefix("0X") {
                    cleanHex = String(cleanHex.dropFirst(2))
                }
                if cleanHex.count >= 6 {
                    let digits = String(cleanHex.prefix(6))
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
            var paramSfoPath = Self.joinPath(folderPath, "sce_sys/param.sfo")
            if !fileManager.fileExists(atPath: paramSfoPath) {
                paramSfoPath = Self.joinPath(folderPath, "app0/sce_sys/param.sfo")
            }
            if fileManager.fileExists(atPath: paramSfoPath),
               let data = Self.readFileData(path: paramSfoPath) {
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
        let candidateIcons: [String] = [
            Self.joinPath(folderPath, "sce_sys/icon0.png"),
            Self.joinPath(folderPath, "app0/sce_sys/icon0.png"),
            Self.joinPath(folderPath, "icon0.png"),
            Self.joinPath(folderPath, "app0/icon0.png")
        ]
        for candidate in candidateIcons {
            if fileManager.fileExists(atPath: candidate) {
                iconPath = candidate
                break
            }
        }
        
        // Compute folder size
        let sizeStr = computeDirectorySize(path: folderPath)
        
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
    
    private func computeDirectorySize(path: String) -> String {
        #if !SKIP
        let fileManager = FileManager.default
        let url = URL(fileURLWithPath: path)
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
        #else
        return "Folder"
        #endif
    }
    
    #if !SKIP
    private func handleImport(result: Result<[URL], Error>) {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return }
            guard url.startAccessingSecurityScopedResource() else { return }
            defer { url.stopAccessingSecurityScopedResource() }
            
#if os(macOS)
            // On macOS: Store the path to read in-place without copying files or duplicating storage
            #if !SKIP
        var savedPaths = UserDefaults.standard.stringArray(forKey: "KytyImportedGamePaths") ?? []
        #else
        var savedPaths: [String] = []
        #endif
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
    #endif
}

// MARK: - Game Card View

struct GameCardView: View {
    let game: GameItem
    let onLaunch: () -> Void
    let onShowDetails: () -> Void
    let onShowTrophies: () -> Void
    let onRemove: () -> Void
    
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
                // Game Cover / Artwork
                ZStack {
#if os(iOS) || SKIP
                    if let iconPath = game.iconPath,
                       let data = readFileData(path: iconPath),
                       let uiImage = UIImage(data: data) {
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
                            PlatformIcon(game.isDirectory ? "folder.fill" : "cpu.fill", size: 38)
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
                            PlatformIcon(game.isDirectory ? "folder.fill" : "cpu.fill", size: 38)
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
                        #if !SKIP
    .layoutPriority(1)
    #endif
                }
            }
            .padding(.horizontal, 4)
        }
#if !SKIP
        .contentShape(Rectangle())
#endif
        .onTapGesture {
            onLaunch()
        }
        .contextMenu {
            Button(action: onLaunch) {
                PlatformLabel("Launch Game", systemImage: "play.fill")
            }
            
            Button(action: onShowDetails) {
                PlatformLabel("Game Details", systemImage: "info.circle")
            }
            
            Button(action: onShowTrophies) {
                PlatformLabel("View Trophies", systemImage: "trophy.fill")
            }
            
            Button(action: {
#if os(iOS)
                UIPasteboard.general.string = game.path
#elseif os(macOS)
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString(game.path, forType: .string)
#endif
            }) {
                PlatformLabel("Copy Path", systemImage: "doc.on.doc")
            }
            
            Divider()
            
            Button(role: .destructive, action: onRemove) {
                PlatformLabel("Remove from Library", systemImage: "minus.circle")
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
#if os(iOS) || SKIP
                    if let iconPath = game.iconPath,
                       let data = readFileData(path: iconPath),
                       let uiImage = UIImage(data: data) {
                        Image(uiImage: uiImage)
                            .resizable()
                            .aspectRatio(contentMode: .fill)
                            .frame(width: 44, height: 44)
                            .cornerRadius(8)
                    } else {
                        PlatformIcon("gamecontroller.fill", size: 20)
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
                        PlatformIcon("gamecontroller.fill", size: 20)
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
                    PlatformIcon("ellipsis.circle", size: 18)
                        .foregroundColor(.secondary)
                }
                .buttonStyle(.plain)
            }
            #if !SKIP
    #if !SKIP
        .contentShape(Rectangle())
#endif
            #endif
        }
        .buttonStyle(.plain)
        .contextMenu {
            Button(action: onLaunch) {
                PlatformLabel("Launch Game", systemImage: "play.fill")
            }
            Button(action: onShowDetails) {
                PlatformLabel("Game Details", systemImage: "info.circle")
            }
            Button(action: onShowTrophies) {
                PlatformLabel("View Trophies", systemImage: "trophy.fill")
            }
            Divider()
            Button(role: .destructive, action: onRemove) {
                PlatformLabel("Remove from Library", systemImage: "minus.circle")
            }
        }
    }
}
