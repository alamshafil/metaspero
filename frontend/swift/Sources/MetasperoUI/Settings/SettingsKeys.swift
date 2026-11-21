// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

/// Centralized `@AppStorage` / `UserDefaults` keys shared across iOS, macOS, and Android (Skip).
enum SettingsKey {
    // General & Console
    static let userName = "userName"
    static let userId = "userId"
    static let consoleLanguage = "consoleLanguage"
    static let networkConnected = "networkConnected"
    static let skipNoticeScreen = "skipNoticeScreen"

    // Library & Storage
    static let gameScanPaths = "KytyGameScanPaths"
    static let gameBookmarks = "KytyGameBookmarks"
    static let shaderCacheEnabled = "shaderCacheEnabled"

    // Graphics
    static let resolutionScale = "resolutionScale"
    static let renderScalePercent = "renderScalePercent"
    static let presentMode = "presentMode"
    static let vblankFrequency = "vblankFrequency"
    static let relaxedShaderFloat = "relaxedShaderFloat"
    static let perfOverlayEnabled = "perfOverlayEnabled"
    static let enableMetalHud = "enableMetalHud"
    static let hudDetailLevel = "hudDetailLevel"
    static let skipMeshShaders = "skipMeshShaders"
    static let enableSrtArm64Jit = "enableSrtArm64Jit"

    // Controllers & Input
    static let enableTouchOverlay = "enableTouchOverlay"
    static let enableHaptics = "enableHaptics"
    static let keymapBindings = "keymapBindings"

    // Diagnostics
    static let enableLogFileWriting = "enableLogFileWriting"
    static let enableMemoryWarningToast = "enableMemoryWarningToast"
}

/// Platform-aware default values for settings.
enum SettingsDefault {
    static let userName = "Metaspero"
    static let userId = 1000
    static let consoleLanguage = 1
    static let resolutionScale = "720p"
    static let renderScalePercent = 100
    static let presentMode = "Mailbox"
    static let vblankFrequency = 60
    static let hudDetailLevel = "Full"

    /// On-screen virtual gamepad overlay: enabled by default on iOS/Android, opt-in on macOS.
    #if os(macOS)
    static let enableTouchOverlay = false
    #else
    static let enableTouchOverlay = true
    #endif
}

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

/// Shared form styling across macOS (grouped System Settings), iOS, and Android (Skip).
public struct SettingsFormModifier: ViewModifier {
    public init() {}

    public func body(content: Content) -> some View {
        #if os(macOS)
        content
            .formStyle(.grouped)
        #elseif SKIP
        content
            .scrollContentBackground(.hidden)
        #else
        content
        #endif
    }
}

public extension View {
    func settingsFormStyle() -> some View {
        modifier(SettingsFormModifier())
    }
}
