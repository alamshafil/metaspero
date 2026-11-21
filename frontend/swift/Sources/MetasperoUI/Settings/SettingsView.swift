// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct SettingsView: View {
    var isSheet: Bool = false
    @Environment(\.dismiss) private var dismiss

    #if os(macOS)
    @State private var selectedCategory: SettingsCategory = .library
    #endif

    enum SettingsCategory: String, CaseIterable, Identifiable {
        case library = "Library"
        case graphics = "Graphics"
        case controllers = "Controllers"
        case advanced = "Advanced"
        case diagnostics = "Diagnostics"
        case about = "About"

        var id: String { rawValue }

        var icon: String {
            switch self {
            case .library: return "folder.fill"
            case .graphics: return "display"
            case .controllers: return "gamecontroller.fill"
            case .advanced: return "slider.horizontal.3"
            case .diagnostics: return "waveform.path.ecg"
            case .about: return "info.circle.fill"
            }
        }
    }

    private var appVersionString: String {
        Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "0.1.0"
    }

    var body: some View {
        #if os(macOS)
        macOSView
        #else
        mobileView
        #endif
    }

    // MARK: - macOS Split Settings

    #if os(macOS)
    private var macOSView: some View {
        NavigationSplitView {
            List(SettingsCategory.allCases, selection: $selectedCategory) { category in
                NavigationLink(value: category) {
                    Label(category.rawValue, systemImage: category.icon)
                }
            }
            .listStyle(.sidebar)
            .navigationSplitViewColumnWidth(min: 170, ideal: 195, max: 230)
        } detail: {
            Group {
                switch selectedCategory {
                case .library:
                    LibrarySettingsView()
                case .graphics:
                    GraphicsSettingsView()
                case .controllers:
                    ControllersSettingsView()
                case .advanced:
                    AdvancedSettingsView()
                case .diagnostics:
                    DiagnosticsSettingsView()
                case .about:
                    AboutKytyContentView(appVersion: appVersionString)
                }
            }
            .toolbar {
                if isSheet {
                    ToolbarItem(placement: .confirmationAction) {
                        Button("Done") { dismiss() }
                            .keyboardShortcut(.defaultAction)
                    }
                }
            }
        }
        .frame(minWidth: 700, idealWidth: 760, minHeight: 500, idealHeight: 560)
    }
    #endif

    // MARK: - iOS & Android (Skip) Navigation Stack

    #if os(iOS) || SKIP
    private var mobileView: some View {
        NavigationStack {
            Form {
                Section {
                    NavigationLink {
                        LibrarySettingsView()
                    } label: {
                        PlatformLabel("Library", systemImage: "folder.fill")
                    }

                    NavigationLink {
                        GraphicsSettingsView()
                    } label: {
                        PlatformLabel("Graphics", systemImage: "display")
                    }

                    NavigationLink {
                        ControllersSettingsView()
                    } label: {
                        PlatformLabel("Controllers", systemImage: "gamecontroller.fill")
                    }

                    NavigationLink {
                        AdvancedSettingsView()
                    } label: {
                        PlatformLabel("Advanced", systemImage: "slider.horizontal.3")
                    }
                }

                Section {
                    #if os(iOS) && !SKIP
                    NavigationLink {
                        JitSettingsView()
                    } label: {
                        PlatformLabel("JIT", systemImage: "cpu")
                    }
                    #endif

                    NavigationLink {
                        DiagnosticsSettingsView()
                    } label: {
                        PlatformLabel("Diagnostics", systemImage: "waveform.path.ecg")
                    }

                    NavigationLink {
                        AboutKytyContentView(appVersion: appVersionString)
                            .navigationTitle("About")
                            .navigationBarTitleDisplayMode(.inline)
                    } label: {
                        PlatformLabel("About", systemImage: "info.circle.fill")
                    }
                }
            }
            #if SKIP
            .scrollContentBackground(.hidden)
            #endif
            .navigationTitle("Settings")
            #if !SKIP
            .navigationBarTitleDisplayMode(isSheet ? .inline : .large)
            #endif
            .toolbar {
                if isSheet {
                    ToolbarItem(placement: .confirmationAction) {
                        Button("Done") { dismiss() }
                    }
                }
            }
        }
    }
    #endif
}
