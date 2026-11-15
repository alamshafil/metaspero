// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

#if SKIP
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.__
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
#endif

public struct ContentView: View {
    @State private var selectedGame: GameItem? = nil
    @State private var isEmulating = false
    @State private var showCrashAlert = false
    @State private var crashAlertMessage = ""
    @State private var showLogSheetFromCrash = false
    
    public init() {}
    
    public var body: some View {
        ZStack {
#if os(iOS) || SKIP
            TabView {
                LibraryView(selectedGame: $selectedGame, isEmulating: $isEmulating)
                    .tabItem {
                        PlatformLabel("Library", systemImage: "square.grid.2x2.fill")
                    }
                
                SettingsView()
                    .tabItem {
                        PlatformLabel("Settings", systemImage: "gearshape.fill")
                    }
            }
            #if SKIP
            .material3NavigationBar { options in
                var updated = options
                updated.itemIcon = { tabIndex in
                    let vector = (tabIndex == 0 ? Icons.Filled.GridView : Icons.Filled.Settings)
                    Icon(
                        imageVector = vector,
                        contentDescription = (tabIndex == 0 ? "Library" : "Settings"),
                        modifier = Modifier.size(24.dp)
                    )
                }
                return updated
            }
            #endif
            #if !SKIP
            .accentColor(.blue)
            #endif
            
            #if !SKIP
            // Fullscreen active game emulation overlay on iOS
            if isEmulating, let game = selectedGame {
                EmulationView(game: game, isEmulating: $isEmulating)
                    .id(game.id)
                    .transition(.opacity)
                    .zIndex(1)
            }
            #endif
#else
            LibraryView(selectedGame: $selectedGame, isEmulating: $isEmulating)
                .frame(minWidth: 960, minHeight: 640)
#endif
        }
        .alert("Emulation Stopped", isPresented: $showCrashAlert) {
            Button("View Logs") {
                showLogSheetFromCrash = true
            }
            Button("Dismiss", role: .cancel) {}
        } message: {
            Text(crashAlertMessage)
        }
        .sheet(isPresented: $showLogSheetFromCrash) {
            LogView()
        }
        .withMemoryWarningToast()
        .onAppear {
            NotificationCenter.default.addObserver(
                forName: .KytyEmulationCrashed,
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
                forName: .KytyWindowDestroyed,
                object: nil,
                queue: .main
            ) { _ in
                if isEmulating && KytyBridge.isEmulationRunning() && !KytyBridge.isStopping() {
                    withAnimation {
                        isEmulating = false
                    }
#if os(iOS)
                    OrientationManager.unlockOrientation()
#endif
                    KytyBridge.stopEmulation()
                }
            }

            NotificationCenter.default.addObserver(
                forName: .KytyEmulationStopped,
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
        
        var lines = trimmed.components(separatedBy: "\n")
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
