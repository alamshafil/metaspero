// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
import SwiftUI

public let KytyLogUpdatedNotification = "KytyLogUpdated"
public let KytyEmulationCrashedNotification = "KytyEmulationCrashed"
public let KytyCrashErrorKey = "error"
public let KytyCrashDetailKey = "detail"
public let KytyWindowDestroyedNotification = "KytyWindowDestroyed"
public let KytyEmulationStoppingNotification = "KytyEmulationStopping"
public let KytyEmulationStoppedNotification = "KytyEmulationStopped"
public let KytyMemoryPressureNotification = "KytyMemoryPressure"
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
    public static func currentMemoryStats() -> [String: Any] { return [:] }
    public static func getRecentLogs() -> [String] { return [] }
    public static func loadSavedLog() -> String { return "" }
    public static func hasTrophies(forGamePath path: String) -> Bool { return false }
    public static func hasSaveData(forTitleId titleId: String) -> Bool { return false }
    public static func deleteSaveData(forTitleId titleId: String) -> Bool { return true }
    public static func getTrophiesForGamePath(_ path: String) -> [[String: Any]] { return [] }
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

public func readFileData(path: String) -> Data? {
    #if SKIP
    return try? Data(contentsOfFile: path)
    #else
    return try? Data(contentsOf: URL(fileURLWithPath: path))
    #endif
}

public struct TouchControlsView: View {
    public init() {}
    public var body: some View {
        EmptyView()
    }
}
