// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct AdvancedSettingsView: View {
    @AppStorage(SettingsKey.userName) private var userName = SettingsDefault.userName
    @AppStorage(SettingsKey.userId) private var userId = SettingsDefault.userId
    @AppStorage(SettingsKey.consoleLanguage) private var consoleLanguage = SettingsDefault.consoleLanguage
    @AppStorage(SettingsKey.networkConnected) private var networkConnected = false
    @AppStorage(SettingsKey.skipNoticeScreen) private var skipNoticeScreen = false
    @AppStorage(SettingsKey.showExperimentalWarning) private var showExperimentalWarning = SettingsDefault.showExperimentalWarning

    var body: some View {
        Form {
            Section {
                #if os(macOS)
                TextField("User Name", text: $userName, prompt: Text(SettingsDefault.userName))

                TextField("User ID", value: $userId, formatter: NumberFormatter())
                #else
                HStack {
                    Text("User Name")
                    Spacer()
                    TextField(SettingsDefault.userName, text: $userName)
                        .multilineTextAlignment(.trailing)
                }

                HStack {
                    Text("User ID")
                    Spacer()
                    #if !SKIP
                    TextField("\(SettingsDefault.userId)", value: $userId, formatter: NumberFormatter())
                        .keyboardType(.numberPad)
                        .multilineTextAlignment(.trailing)
                    #else
                    TextField("\(SettingsDefault.userId)", text: Binding(
                        get: { "\(userId)" },
                        set: { userId = Int($0) ?? userId }
                    ))
                    .multilineTextAlignment(.trailing)
                    #endif
                }
                #endif

                Picker("Console Language", selection: $consoleLanguage) {
                    ForEach(kConsoleLanguages) { lang in
                        Text(lang.name).tag(lang.id)
                    }
                }
            } header: {
                Text("User Profile & Console")
            }

            Section {
                Toggle("Simulate Connected Network", isOn: $networkConnected)
                Toggle("Skip Startup Logo & Notice Screens", isOn: $skipNoticeScreen)
                Toggle("Show Experimental Warning on Launch", isOn: $showExperimentalWarning)
            } header: {
                Text("System")
            }
        }
        .settingsFormStyle()
        .navigationTitle("Advanced")
        #if os(iOS) || SKIP
        .navigationBarTitleDisplayMode(.inline)
        #endif
    }
}
