// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct LogView: View {
    @Environment(\.presentationMode) var presentationMode
    @State private var logLines: [String] = []
    
    var body: some View {
        NavigationStack {
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 2) {
                        ForEach(Array(logLines.enumerated()), id: \.offset) { index, line in
                            Text(line)
                                .font(.system(size: 10, design: .monospaced))
                                .foregroundColor(colorForLine(line))
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .id(index)
                        }
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 8)
                }
                .background(Color.black)
                .onChange(of: logLines.count) { _ in
                    if !logLines.isEmpty {
                        proxy.scrollTo(logLines.count - 1, anchor: .bottom)
                    }
                }
            }
            .navigationTitle("Logs")
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Close") {
                        presentationMode.wrappedValue.dismiss()
                    }
                    .font(.system(size: 14, weight: .semibold))
                }
            }
            .onAppear {
                refreshLogs()
                NotificationCenter.default.addObserver(forName: NSNotification.Name.KytyLogUpdated, object: nil, queue: .main) { _ in
                    refreshLogs()
                }
            }
        }
        #if os(macOS)
        .frame(minWidth: 640, minHeight: 480)
        #endif
    }
    
    private func refreshLogs() {
        let recent = KytyBridge.getRecentLogs()
        if !recent.isEmpty {
            self.logLines = recent
        } else {
            let diskText = KytyBridge.loadSavedLog()
            if !diskText.isEmpty {
                self.logLines = diskText.components(separatedBy: "\n").filter { !$0.isEmpty }
            }
        }
    }
    
    private func colorForLine(_ line: String) -> Color {
        let lower = line.lowercased()
        if lower.contains("crash") || lower.contains("fatal") || lower.contains("sigsegv") || lower.contains("sigbus") {
            return Color.red
        }
        if lower.contains("error") || lower.contains("fail") {
            return Color.red.opacity(0.9)
        }
        if lower.contains("warn") {
            return Color.orange.opacity(0.9)
        }
        if lower.contains("jit") || lower.contains("success") || lower.contains("ready") {
            return Color.green.opacity(0.9)
        }
        if lower.contains("fex") || lower.contains("rip=") {
            return Color.purple.opacity(0.9)
        }
        if lower.contains("[kytybridge]") || lower.contains("[kyty ios jit]") {
            return Color.cyan.opacity(0.9)
        }
        return Color.white.opacity(0.85)
    }
}
