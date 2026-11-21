// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI
#if !SKIP
import UniformTypeIdentifiers
#endif

struct LibrarySettingsView: View {
    @AppStorage(SettingsKey.shaderCacheEnabled) private var shaderCacheEnabled = true

    @State private var scanPaths: [String] = []
    @State private var showFolderImporter = false
    @State private var shaderCacheSizeText: String = "0 KB (0 files)"

    var body: some View {
        Form {
            #if os(iOS) && !SKIP
            Section {
                Button(action: SettingsStorageHelpers.openDocumentsInFilesApp) {
                    PlatformLabel("Open in Files App", systemImage: "folder.fill")
                }
            } header: {
                Text("App Library Folder")
            } footer: {
                Text("Place games in On My iPhone/iPad -> Metaspero in Files to avoid copying.")
            }
            #endif

            Section {
                if scanPaths.isEmpty {
                    Text("No Game Folders")
                        .font(.subheadline)
                        .foregroundColor(.secondary)
                } else {
                    ForEach(Array(scanPaths.enumerated()), id: \.offset) { index, path in
                        HStack(spacing: 10) {
                            PlatformIcon("folder.fill")
                                .foregroundColor(.blue)
                            Text(path)
                                .font(.system(.subheadline, design: .monospaced))
                                .lineLimit(1)
                                .truncationMode(.middle)
                            Spacer()
                            Button(role: .destructive) {
                                scanPaths = SettingsStorageHelpers.removeScanPath(at: index, from: scanPaths)
                            } label: {
                                PlatformIcon("trash")
                                    .foregroundColor(.red)
                            }
                            .buttonStyle(.borderless)
                            #if !SKIP
                            .help("Remove Folder")
                            #endif
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

            Section {
                Toggle("Persistent Shader Cache", isOn: $shaderCacheEnabled)

                #if !SKIP
                HStack {
                    Text("Cache Size")
                    Spacer()
                    Text(shaderCacheSizeText)
                        .foregroundColor(.secondary)
                }

                #if os(macOS)
                HStack {
                    Button("Show in Finder") {
                        SettingsStorageHelpers.openShaderCacheFolder()
                    }
                    Spacer()
                    Button("Clear Cache", role: .destructive) {
                        clearShaderCache()
                    }
                }
                #else
                Button("Clear Cache", role: .destructive) {
                    clearShaderCache()
                }
                #endif
                #endif
            } header: {
                Text("Shader Cache")
            } footer: {
                Text("Saves compiled shaders and pipelines to speed up future launches.")
            }
        }
        .settingsFormStyle()
        .navigationTitle("Library")
        #if os(iOS) || SKIP
        .navigationBarTitleDisplayMode(.inline)
        #endif
        #if !SKIP
        .fileImporter(
            isPresented: $showFolderImporter,
            allowedContentTypes: [.folder],
            allowsMultipleSelection: false
        ) { result in
            if let updated = SettingsStorageHelpers.handleFolderImport(result: result) {
                scanPaths = updated
            }
        }
        #endif
        .onAppear {
            scanPaths = SettingsStorageHelpers.loadScanPaths()
            refreshShaderCacheInfo()
        }
        .onReceive(NotificationCenter.default.publisher(for: SettingsStorageHelpers.gamePathsChangedNotification)) { _ in
            scanPaths = SettingsStorageHelpers.loadScanPaths()
        }
    }

    private func refreshShaderCacheInfo() {
        #if !SKIP
        shaderCacheSizeText = SettingsStorageHelpers.shaderCacheStats().sizeText
        #endif
    }

    private func clearShaderCache() {
        #if !SKIP
        SettingsStorageHelpers.clearShaderCache()
        refreshShaderCacheInfo()
        #endif
    }
}
