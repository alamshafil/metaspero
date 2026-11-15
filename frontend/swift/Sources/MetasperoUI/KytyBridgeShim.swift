// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
import SwiftUI

public let KytyLogUpdatedNotification = "KytyLogUpdatedNotification"
public let KytyEmulationCrashedNotification = "KytyEmulationCrashedNotification"
public let KytyCrashErrorKey = "error"
public let KytyCrashDetailKey = "detail"
public let KytyWindowDestroyedNotification = "KytyWindowDestroyedNotification"
public let KytyEmulationStoppingNotification = "KytyEmulationStoppingNotification"
public let KytyEmulationStoppedNotification = "KytyEmulationStoppedNotification"
public let KytyMemoryPressureNotification = "KytyMemoryPressureNotification"
public let KytyMemoryPressureLevelKey = "level"
public let KytyMemoryPressureReasonKey = "reason"
public let KytyMemoryUsedMBKey = "usedMB"
public let KytyMemoryAvailMBKey = "availMB"
public let KytyMemoryLimitMBKey = "limitMB"

public extension Notification.Name {
    static let KytyLogUpdated = Notification.Name(KytyLogUpdatedNotification)
    static let KytyEmulationCrashed = Notification.Name(KytyEmulationCrashedNotification)
    static let KytyWindowDestroyed = Notification.Name(KytyWindowDestroyedNotification)
    static let KytyEmulationStopping = Notification.Name(KytyEmulationStoppingNotification)
    static let KytyEmulationStopped = Notification.Name(KytyEmulationStoppedNotification)
    static let KytyMemoryPressure = Notification.Name(KytyMemoryPressureNotification)
}

#if SKIP
public typealias NSNotification = Notification

public struct NSColor {
    public static var clear: Color { Color.clear }
    public static var black: Color { Color.black }
    public static var white: Color { Color.white }
    public static var windowBackgroundColor: Color { Color(white: 0.12) }
    public static var controlBackgroundColor: Color { Color(white: 0.15) }
    public static var separatorColor: Color { Color.gray.opacity(0.3) }
}

public typealias NSImage = UIImage

extension String {
    public func localizedCaseInsensitiveContains(_ other: String) -> Bool {
        return self.lowercased().contains(other.lowercased())
    }
}

public struct DispatchQoS {
    public static let userInitiated = DispatchQoS()
    public static let background = DispatchQoS()
    public static let utility = DispatchQoS()
    public static let `default` = DispatchQoS()
}

extension DispatchQueue {
    public static func global(qos: Any? = nil) -> DispatchQueue {
        return DispatchQueue.main
    }
}

extension View {
    public func help(_ text: String) -> some View {
        return self
    }
}
#endif

public class KytyBridge {
    public struct GpuDriverInfo: Identifiable, Hashable {
        public let id: String
        public let name: String
        public let isDefault: Bool
        public init(id: String, name: String, isDefault: Bool) {
            self.id = id
            self.name = name
            self.isDefault = isDefault
        }
    }

    #if !SKIP
    nonisolated(unsafe) public static var customGamePaths: [String] = []
    nonisolated(unsafe) public static var onLaunchGame: ((String) -> Void)? = nil
    nonisolated(unsafe) public static var onPickFolderRequested: (() -> Void)? = nil
    nonisolated(unsafe) public static var installedDrivers: [GpuDriverInfo] = [
        GpuDriverInfo(id: "system_default", name: "System Default Vulkan", isDefault: true)
    ]
    nonisolated(unsafe) public static var selectedDriverId: String = "system_default"
    nonisolated(unsafe) public static var onInstallDriverRequested: (() -> Void)? = nil
    nonisolated(unsafe) public static var onSelectDriverRequested: ((String) -> Void)? = nil
    nonisolated(unsafe) public static var onCustomGamePathsChanged: (([String]) -> Void)? = nil
    nonisolated(unsafe) public static var onCurrentMemoryStats: (() -> [String: Any])? = nil
    nonisolated(unsafe) public static var onHasTrophies: ((String) -> Bool)? = nil
    nonisolated(unsafe) public static var onGetTrophies: ((String, Bool) -> [[String: Any]])? = nil
    #else
    public static var customGamePaths: [String] = []
    public static var onCustomGamePathsChanged: (([String]) -> Void)? = nil
    public static var onLaunchGame: ((String) -> Void)? = nil
    public static var onPickFolderRequested: (() -> Void)? = nil
    public static var installedDrivers: [GpuDriverInfo] = [
        GpuDriverInfo(id: "system_default", name: "System Default Vulkan", isDefault: true)
    ]
    public static var selectedDriverId: String = "system_default"
    public static var onInstallDriverRequested: (() -> Void)? = nil
    public static var onSelectDriverRequested: ((String) -> Void)? = nil
    public static var onCurrentMemoryStats: (() -> [String: Any])? = nil
    public static var onHasTrophies: ((String) -> Bool)? = nil
    public static var onGetTrophies: ((String, Bool) -> [[String: Any]])? = nil
    #endif

    public static func clearInstalledDrivers() {
        installedDrivers.removeAll()
    }

    public static func addInstalledDriver(id: String, name: String, isDefault: Bool) {
        installedDrivers.append(GpuDriverInfo(id: id, name: name, isDefault: isDefault))
    }

    public static func requestInstallDriver() {
        if let callback = onInstallDriverRequested {
            callback()
        }
    }

    public static func selectDriver(id: String) {
        selectedDriverId = id
        if let callback = onSelectDriverRequested {
            callback(id)
        }
    }

    public static func requestPickFolder() {
        if let callback = onPickFolderRequested {
            callback()
        }
    }

    public static func addCustomGamePath(_ path: String) {
        if !customGamePaths.contains(path) {
            customGamePaths.append(path)
            onCustomGamePathsChanged?(customGamePaths)
            NotificationCenter.default.post(name: Notification.Name("KytyGamePathsChanged"), object: nil)
        }
    }
    public static func getConnectedControllers() -> [[String: Any]] { return [] }
    public static func isSDLGamepadSubsystemActive() -> Bool { return false }
    public static func defaultKeymapBindings() -> [String] { return [] }
    public static func isEmulationRunning() -> Bool { return false }
    public static func isStopping() -> Bool { return false }
    public static func stopEmulation() {}
    public static func addLogMessage(_ message: String) {}
    public static func forceResetEmulationSession() {}
    public static func isLogFileWritingEnabled() -> Bool { return false }
    public static func setLogFileWritingEnabled(_ enabled: Bool) {}
    public static func setMetalHudEnabled(_ enabled: Bool) {}
    public static func moltenVkVersion() -> String { return "Vulkan 1.3" }
    public static func isJitReady() -> Bool { return true }
    public static func isProcessDebugged() -> Bool { return false }
    public static func refreshJitStatus() -> Bool { return true }
    public static func processId() -> Int32 { return 0 }
    public static func openStikDebug() {}
    public static func acquireJitPool() -> Bool { return true }
    public static func currentMemoryStats() -> [String: Any] {
        if let custom = onCurrentMemoryStats?() {
            return custom
        }
        #if SKIP
        var usedMB = Int(android.os.Debug.getPss() / 1024)
        if usedMB <= 0 {
            let rt = java.lang.Runtime.getRuntime()
            usedMB = Int((rt.totalMemory() - rt.freeMemory()) / (1024 * 1024))
        }
        var limitMB = 0
        if let data = try? Data(contentsOfFile: "/proc/meminfo"),
           let text = String(data: data, encoding: .utf8) {
            let lines = text.components(separatedBy: "\n")
            for line in lines {
                if line.hasPrefix("MemTotal:") {
                    let parts = line.components(separatedBy: " ").filter { !$0.isEmpty }
                    if parts.count >= 2, let kb = Int(parts[1]) {
                        limitMB = kb / 1024
                        break
                    }
                }
            }
        }
        if limitMB <= 0 {
            limitMB = Int(java.lang.Runtime.getRuntime().maxMemory() / (1024 * 1024))
        }
        return [
            KytyMemoryUsedMBKey: usedMB,
            KytyMemoryLimitMBKey: limitMB
        ]
        #else
        return [:]
        #endif
    }
    public static func getRecentLogs() -> [String] { return [] }
    public static func loadSavedLog() -> String { return "" }
    public static func hasTrophies(forGamePath path: String) -> Bool {
        if let callback = onHasTrophies {
            return callback(path)
        }
        #if SKIP
        guard !path.isEmpty else { return false }
        let candidates = [
            path + "/sce_sys/trophy2",
            path + "/app0/sce_sys/trophy2"
        ]
        for c in candidates {
            let dir = java.io.File(c)
            if dir.isDirectory() {
                if let files = dir.listFiles() {
                    for f in files {
                        let name = f.getName()
                        if name.hasPrefix("trophy") && name.hasSuffix(".ucp") {
                            return true
                        }
                    }
                }
            }
        }
        #endif
        return false
    }

    public static func hasSaveData(forTitleId titleId: String) -> Bool {
        let cleanId = titleId.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !cleanId.isEmpty else { return false }
        #if SKIP
        let localDir = java.io.File("_SaveData/" + cleanId)
        if localDir.exists() { return true }
        if let home = java.lang.System.getenv("HOME") {
            let homeSave = java.io.File(home + "/_SaveData/" + cleanId)
            if homeSave.exists() { return true }
        }
        #endif
        return false
    }

    public static func deleteSaveData(forTitleId titleId: String) -> Bool {
        let cleanId = titleId.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !cleanId.isEmpty else { return false }
        #if SKIP
        var deleted = false
        let localDir = java.io.File("_SaveData/" + cleanId)
        if localDir.exists() {
            deleted = localDir.deleteRecursively() || deleted
        }
        if let home = java.lang.System.getenv("HOME") {
            let homeSave = java.io.File(home + "/_SaveData/" + cleanId)
            if homeSave.exists() {
                deleted = homeSave.deleteRecursively() || deleted
            }
        }
        return deleted
        #else
        return true
        #endif
    }

    public static func getTrophiesForGamePath(_ path: String) -> [[String: Any]] {
        return getTrophiesForGamePath(path, includeIcons: true)
    }

    public static func getTrophiesForGamePath(_ path: String, includeIcons: Bool) -> [[String: Any]] {
        if let callback = onGetTrophies {
            return callback(path, includeIcons)
        }
        return []
    }
    public static func configureAudioSession() {}
    public static func hostViewDidResize(toPixelWidth width: Int32, height: Int32) {}
    public static func startEmulation(withGamePath path: String, in view: Any?, completion: ((Bool, Error?) -> Void)?) {}
    public static func sendKeyEvent(withMacKeyCode keyCode: UInt16, characters: String?, modifierFlags: UInt, down: Bool, isRepeat: Bool) {}
}

@MainActor
public final class PhysicalControllerManager: ObservableObject {
    public static let shared = PhysicalControllerManager()
    @Published public var hasPhysicalController: Bool = false
    public init() {}
}

public struct TouchControlsView: View {
    public init() {}
    public var body: some View {
        EmptyView()
    }
}
