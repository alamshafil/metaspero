// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
#if os(iOS) && !SKIP
import UIKit
#endif
#if os(macOS) && !SKIP
import AppKit
#endif

/// Shared filesystem, scan-path, and shader-cache helpers for Settings & Library views.
enum SettingsStorageHelpers {
    static let gamePathsChangedNotification = Notification.Name("KytyGamePathsChanged")

    static func loadScanPaths() -> [String] {
        #if !SKIP
        return UserDefaults.standard.stringArray(forKey: SettingsKey.gameScanPaths) ?? []
        #else
        return KytyBridge.customGamePaths
        #endif
    }

    static func saveScanPaths(_ paths: [String]) {
        #if !SKIP
        UserDefaults.standard.set(paths, forKey: SettingsKey.gameScanPaths)
        #else
        KytyBridge.customGamePaths = paths
        KytyBridge.onCustomGamePathsChanged?(paths)
        #endif
        NotificationCenter.default.post(name: gamePathsChangedNotification, object: nil)
    }

    static func addScanPath(_ path: String) -> [String] {
        var paths = loadScanPaths()
        if !paths.contains(path) {
            paths.append(path)
            saveScanPaths(paths)
        }
        return paths
    }

    static func removeScanPath(at index: Int, from current: [String]) -> [String] {
        var paths = current
        guard index >= 0 && index < paths.count else { return paths }
        paths.remove(at: index)
        saveScanPaths(paths)
        return paths
    }

    #if os(iOS) && !SKIP
    static func openDocumentsInFilesApp() {
        guard let documentsURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first else { return }
        if let filesURL = URL(string: "shareddocuments://\(documentsURL.path)") {
            UIApplication.shared.open(filesURL)
        }
    }
    #endif

    #if !SKIP
    static func handleFolderImport(result: Result<[URL], Error>) -> [String]? {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return nil }
            let accessed = url.startAccessingSecurityScopedResource()
            #if os(macOS)
            defer { if accessed { url.stopAccessingSecurityScopedResource() } }
            #else
            if accessed {
                LibraryView.saveSecurityScopedBookmark(for: url)
            }
            #endif
            return addScanPath(url.path)
        case .failure(let error):
            NSLog("[SettingsStorageHelpers] Folder import failed: %@", error.localizedDescription)
            return nil
        }
    }

    static func shaderCacheDirectoryURL() -> URL? {
        #if os(macOS)
        guard let appSupport = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first else {
            return nil
        }
        return appSupport.appendingPathComponent("Metaspero/_PipelineCache", isDirectory: true)
        #else
        guard let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first else {
            return nil
        }
        return docs.appendingPathComponent("_PipelineCache", isDirectory: true)
        #endif
    }

    static func shaderCacheStats() -> (sizeText: String, pathText: String) {
        guard let cacheURL = shaderCacheDirectoryURL() else {
            return ("Unavailable", "")
        }
        let pathText = cacheURL.path
        let fm = FileManager.default
        guard let items = try? fm.contentsOfDirectory(
            at: cacheURL,
            includingPropertiesForKeys: [.fileSizeKey, .isRegularFileKey],
            options: [.skipsHiddenFiles]
        ) else {
            return ("0 KB (0 files)", pathText)
        }
        var totalBytes: Int64 = 0
        var fileCount = 0
        for item in items {
            if let values = try? item.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey]),
               values.isRegularFile == true {
                totalBytes += Int64(values.fileSize ?? 0)
                fileCount += 1
            }
        }
        let formatted = ByteCountFormatter.string(fromByteCount: totalBytes, countStyle: .file)
        return ("\(formatted) (\(fileCount) file\(fileCount == 1 ? "" : "s"))", pathText)
    }

    static func openShaderCacheFolder() {
        #if os(macOS)
        guard let cacheURL = shaderCacheDirectoryURL() else { return }
        try? FileManager.default.createDirectory(at: cacheURL, withIntermediateDirectories: true)
        NSWorkspace.shared.open(cacheURL)
        #endif
    }

    static func clearShaderCache() {
        guard let cacheURL = shaderCacheDirectoryURL() else { return }
        let fm = FileManager.default
        if let items = try? fm.contentsOfDirectory(at: cacheURL, includingPropertiesForKeys: nil) {
            for item in items {
                try? fm.removeItem(at: item)
            }
        }
    }
    #endif
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
