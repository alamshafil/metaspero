// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI
import Foundation

@MainActor
final class CompatibilityService: ObservableObject {
    static let shared = CompatibilityService()
    
    enum CompatibilityStatus: String, CaseIterable {
        case playable = "Playable"
        case inGame = "InGame"
        case mainMenu = "MainMenu"
        case logo = "Logo"
        case doesntBoot = "DoesntBoot"
        case unknown = "Unknown"
        
        var displayName: String {
            switch self {
            case .playable: return "Playable"
            case .inGame: return "In-Game"
            case .mainMenu: return "Main Menu"
            case .logo: return "Intro / Logo"
            case .doesntBoot: return "Doesn't Boot"
            case .unknown: return "Untested"
            }
        }
        
        var color: Color {
            switch self {
            case .playable: return .green
            case .inGame: return .blue
            case .mainMenu: return .orange
            case .logo: return .purple
            case .doesntBoot: return .red
            case .unknown: return .secondary
            }
        }
        
        var iconName: String {
            switch self {
            case .playable: return "checkmark.seal.fill"
            case .inGame: return "play.circle.fill"
            case .mainMenu: return "list.bullet.rectangle"
            case .logo: return "photo.fill"
            case .doesntBoot: return "xmark.octagon.fill"
            case .unknown: return "questionmark.circle"
            }
        }
        
        static func from(text: String) -> CompatibilityStatus {
            let clean = text.trimmingCharacters(in: CharacterSet.whitespacesAndNewlines).lowercased()
            switch clean {
            case "playable": return .playable
            case "ingame", "in game": return .inGame
            case "mainmenu", "main menu": return .mainMenu
            case "logo", "intro": return .logo
            case "doesntboot", "doesn't boot", "unbootable": return .doesntBoot
            default: return .unknown
            }
        }
    }
    
    struct CompatibilityInfo {
        let status: CompatibilityStatus
        let comment: String
    }
    
    @Published private(set) var entries: [String: CompatibilityInfo] = [:]
    @Published private(set) var isFetching: Bool = false
    @Published private(set) var lastFetchDate: Date? = nil
    
    private let databaseURL = URL(string: "https://kytyps5.github.io/data/compatibility.json")!
    
    private var cacheFileURL: URL {
        let paths = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)
        let dir = paths.first ?? URL(fileURLWithPath: NSTemporaryDirectory())
        return dir.appendingPathComponent("compatibility_db.json")
    }
    
    private init() {
        loadCachedDatabase()
    }
    
    func info(for titleId: String) -> CompatibilityInfo? {
        let key = titleId.trimmingCharacters(in: CharacterSet.whitespacesAndNewlines).uppercased()
        guard !key.isEmpty else { return nil }
        return entries[key]
    }
    
    func status(for titleId: String) -> CompatibilityStatus {
        return info(for: titleId)?.status ?? .unknown
    }
    
    func loadCachedDatabase() {
        guard FileManager.default.fileExists(atPath: cacheFileURL.path),
              let data = try? Data(contentsOf: cacheFileURL) else {
            return
        }
        parse(jsonData: data)
    }
    
    func refreshDatabase() async {
        guard !isFetching else { return }
        isFetching = true
        defer { isFetching = false }
        
        do {
            var request = URLRequest(url: databaseURL)
            request.timeoutInterval = 10.0
            request.setValue("Kyty-SwiftUI", forHTTPHeaderField: "User-Agent")
            
            let (data, response) = try await URLSession.shared.data(for: request)
            if let http = response as? HTTPURLResponse, http.statusCode == 200 {
                parse(jsonData: data)
                try? data.write(to: cacheFileURL, options: .atomic)
                lastFetchDate = Date()
            }
        } catch {
            // Offline fallback: keep existing cached database
        }
    }
    
    private func parse(jsonData: Data) {
        guard let raw = (try? JSONSerialization.jsonObject(with: jsonData)) as? [String: Any] else {
            return
        }
        
        var newEntries: [String: CompatibilityInfo] = [:]
        for (rawKey, val) in raw {
            let key = rawKey.trimmingCharacters(in: CharacterSet.whitespacesAndNewlines).uppercased()
            guard !key.isEmpty, let dict = val as? [String: Any] else { continue }
            let statusStr = dict["status"] as? String ?? ""
            let comment = dict["comment"] as? String ?? ""
            let status = CompatibilityStatus.from(text: statusStr)
            newEntries[key] = CompatibilityInfo(status: status, comment: comment)
        }
        self.entries = newEntries
    }
}
