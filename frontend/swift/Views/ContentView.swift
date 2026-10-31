// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct ContentView: View {
    @State private var selectedGame: GameItem? = nil
    @State private var isEmulating = false
    @State private var showCrashAlert = false
    @State private var crashAlertMessage = ""
    @State private var showLogSheetFromCrash = false
    
    var body: some View {
        ZStack {
#if os(iOS)
            TabView {
                LibraryView(selectedGame: $selectedGame, isEmulating: $isEmulating)
                    .tabItem {
                        Label("Library", systemImage: "square.grid.2x2.fill")
                    }
                
                SettingsView()
                    .tabItem {
                        Label("Settings", systemImage: "gearshape.fill")
                    }
            }
            .accentColor(.blue)
            
            // Fullscreen active game emulation overlay on iOS
            if isEmulating, let game = selectedGame {
                EmulationView(game: game, isEmulating: $isEmulating)
                    .transition(.opacity)
                    .zIndex(1)
            }
#else
            LibraryView(selectedGame: $selectedGame, isEmulating: $isEmulating)
                .frame(minWidth: 960, minHeight: 640)
#endif
        }
        .alert(isPresented: $showCrashAlert) {
            Alert(
                title: Text("Emulation Stopped"),
                message: Text(crashAlertMessage),
                primaryButton: .default(Text("View Logs")) {
                    showLogSheetFromCrash = true
                },
                secondaryButton: .cancel(Text("Dismiss"))
            )
        }
        .sheet(isPresented: $showLogSheetFromCrash) {
            LogView()
        }
        .withMemoryWarningToast()
        .onAppear {
            NotificationCenter.default.addObserver(
                forName: NSNotification.Name.KytyEmulationCrashed,
                object: nil,
                queue: .main
            ) { notification in
                withAnimation {
                    isEmulating = false
                }
#if os(iOS)
                OrientationManager.unlockOrientation()
#endif
                KytyBridge.stopEmulation()
                
                if let userInfo = notification.userInfo,
                   let error = userInfo[KytyCrashErrorKey] as? String {
                    crashAlertMessage = Self.trimCrashMessage(error)
                } else if let error = notification.userInfo?["error"] as? String {
                    crashAlertMessage = Self.trimCrashMessage(error)
                } else {
                    crashAlertMessage = "Emulation stopped due to an unexpected core assertion or fatal fault."
                }
                
                // Allow in-flight orientation rotation & transition animations to finish
                // so UIAlertController is presented on a stable, interactive key window.
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) {
                    showCrashAlert = true
                }
            }
            
            NotificationCenter.default.addObserver(
                forName: NSNotification.Name.KytyWindowDestroyed,
                object: nil,
                queue: .main
            ) { _ in
                if isEmulating {
                    withAnimation {
                        isEmulating = false
                    }
#if os(iOS)
                    OrientationManager.unlockOrientation()
#endif
                    KytyBridge.stopEmulation()
                }
            }
        }
    }
    
    /// Trims excessive crash log messages (e.g. huge call stack dumps)
    private static func trimCrashMessage(_ rawMessage: String, maxLength: Int = 800, maxLines: Int = 16) -> String {
        let trimmed = rawMessage.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty {
            return "Emulation stopped due to an unexpected core assertion or fatal fault."
        }
        
        var lines = trimmed.components(separatedBy: .newlines)
        var wasTruncated = false
        
        if lines.count > maxLines {
            lines = Array(lines.prefix(maxLines))
            wasTruncated = true
        }
        
        var combined = lines.joined(separator: "\n")
        if combined.count > maxLength {
            combined = String(combined.prefix(maxLength))
            wasTruncated = true
        }
        
        if wasTruncated {
            let clean = combined.trimmingCharacters(in: .whitespacesAndNewlines)
            return "\(clean)\n\n... (log trimmed, tap 'View Logs' to view full report)"
        }
        
        return combined
    }
}
