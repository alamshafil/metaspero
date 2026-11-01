// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct InputMappingView: View {
    @Environment(\.dismiss) private var dismiss
    
    struct KeyBindingItem: Identifiable {
        var id: String { control }
        let control: String
        let label: String
        let group: String
        var hostInput: String
    }
    
    @State private var bindings: [KeyBindingItem] = []
    @State private var selectedControl: String? = nil
    
    private let controlLabels: [(control: String, label: String, group: String)] = [
        ("Up", "D-Pad Up", "Directional Pad"),
        ("Down", "D-Pad Down", "Directional Pad"),
        ("Left", "D-Pad Left", "Directional Pad"),
        ("Right", "D-Pad Right", "Directional Pad"),
        
        ("LeftStickUp", "Left Stick Up", "Left Analog Stick"),
        ("LeftStickDown", "Left Stick Down", "Left Analog Stick"),
        ("LeftStickLeft", "Left Stick Left", "Left Analog Stick"),
        ("LeftStickRight", "Left Stick Right", "Left Analog Stick"),
        ("L3", "L3 (Stick Press)", "Left Analog Stick"),
        
        ("RightStickUp", "Right Stick Up", "Right Analog Stick"),
        ("RightStickDown", "Right Stick Down", "Right Analog Stick"),
        ("RightStickLeft", "Right Stick Left", "Right Analog Stick"),
        ("RightStickRight", "Right Stick Right", "Right Analog Stick"),
        ("R3", "R3 (Stick Press)", "Right Analog Stick"),
        
        ("Triangle", "Triangle (△)", "Action Buttons"),
        ("Circle", "Circle (○)", "Action Buttons"),
        ("Cross", "Cross (✕)", "Action Buttons"),
        ("Square", "Square (□)", "Action Buttons"),
        
        ("L1", "L1 Bumper", "Shoulders & Triggers"),
        ("R1", "R1 Bumper", "Shoulders & Triggers"),
        ("L2", "L2 Trigger", "Shoulders & Triggers"),
        ("R2", "R2 Trigger", "Shoulders & Triggers"),
        
        ("Options", "Options Button", "System Controls"),
        ("Touchpad", "Touchpad Click", "System Controls"),
        ("Share", "Share / Create", "System Controls")
    ]
    
    var groups: [String] {
        Array(NSOrderedSet(array: controlLabels.map { $0.group })).compactMap { $0 as? String }
    }
    
    var body: some View {
        NavigationStack {
            List {
                Section(footer: Text("Custom keyboard mappings are loaded by the emulator on macOS.")) {
                    HStack {
                        Text("Reset All to Default")
                        Spacer()
                        Button("Reset Defaults") {
                            resetDefaults()
                        }
                        .buttonStyle(.bordered)
                    }
                }
                
                ForEach(groups, id: \.self) { group in
                    Section(header: Text(group)) {
                        ForEach(bindings.filter { $0.group == group }) { item in
                            HStack {
                                Text(item.label)
                                    .font(.system(size: 14, weight: .medium))
                                Spacer()
                                TextField("Key", text: Binding(
                                    get: { item.hostInput },
                                    set: { newKey in
                                        if let idx = bindings.firstIndex(where: { $0.control == item.control }) {
                                            bindings[idx].hostInput = newKey
                                            saveBindings()
                                        }
                                    }
                                ))
                                .textFieldStyle(.roundedBorder)
                                .frame(width: 120)
                                .multilineTextAlignment(.center)
                            }
                        }
                    }
                }
            }
            .navigationTitle("Keyboard Input Mapping")
#if os(macOS)
            .frame(minWidth: 460, idealWidth: 520, minHeight: 480, idealHeight: 560)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Done") { dismiss() }
                }
            }
#else
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
#endif
            .onAppear {
                loadBindings()
            }
        }
    }
    
    private func loadBindings() {
        let defaults = KytyBridge.defaultKeymapBindings()
        var map: [String: String] = [:]
        for d in defaults {
            let parts = d.components(separatedBy: "=")
            if parts.count == 2 {
                map[parts[0]] = parts[1]
            }
        }
        
        let saved = UserDefaults.standard.stringArray(forKey: "keymapBindings") ?? []
        for s in saved {
            let parts = s.components(separatedBy: "=")
            if parts.count == 2 {
                map[parts[0]] = parts[1]
            }
        }
        
        var list: [KeyBindingItem] = []
        for def in controlLabels {
            let key = map[def.control] ?? "Unassigned"
            list.append(KeyBindingItem(control: def.control, label: def.label, group: def.group, hostInput: key))
        }
        self.bindings = list
    }
    
    private func saveBindings() {
        var strList: [String] = []
        for b in bindings {
            let input = b.hostInput.trimmingCharacters(in: .whitespacesAndNewlines)
            if !input.isEmpty {
                strList.append("\(b.control)=\(input)")
            }
        }
        UserDefaults.standard.set(strList, forKey: "keymapBindings")
    }
    
    private func resetDefaults() {
        let defaults = KytyBridge.defaultKeymapBindings()
        UserDefaults.standard.set(defaults, forKey: "keymapBindings")
        loadBindings()
    }
}
