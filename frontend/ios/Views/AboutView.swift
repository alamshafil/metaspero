// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct AboutKytyContentView: View {
    let appVersion: String
    
    private var defaultIconView: some View {
        Image(systemName: "gamecontroller.fill")
            .font(.system(size: 44))
            .foregroundColor(.blue)
    }

    @ViewBuilder
    private var appIconView: some View {
        #if os(macOS)
        Image(nsImage: NSWorkspace.shared.icon(forFile: Bundle.main.bundlePath))
            .resizable()
            .scaledToFit()
            .frame(width: 52, height: 52)
            .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
            .shadow(color: .black.opacity(0.18), radius: 4, x: 0, y: 2)
        #elseif os(iOS)
        if let icons = Bundle.main.infoDictionary?["CFBundleIcons"] as? [String: Any],
           let primary = icons["CFBundlePrimaryIcon"] as? [String: Any],
           let iconFiles = primary["CFBundleIconFiles"] as? [String],
           let lastIcon = iconFiles.last,
           let ui = UIImage(named: lastIcon) {
            Image(uiImage: ui)
                .resizable()
                .scaledToFit()
                .frame(width: 52, height: 52)
                .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
                .shadow(color: .black.opacity(0.18), radius: 4, x: 0, y: 2)
        } else if let ui = UIImage(named: "Icon") {
            Image(uiImage: ui)
                .resizable()
                .scaledToFit()
                .frame(width: 52, height: 52)
                .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
                .shadow(color: .black.opacity(0.18), radius: 4, x: 0, y: 2)
        } else {
            defaultIconView
        }
        #else
        defaultIconView
        #endif
    }
    
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                // Header
                HStack(spacing: 16) {
                    appIconView
                    
                    VStack(alignment: .leading, spacing: 4) {
                        Text("KytyPS5")
                            .font(.title2)
                            .fontWeight(.bold)
                        Text(appVersion)
                            .font(.subheadline)
                            .foregroundColor(.secondary)
                        Text("PlayStation 5 Research Emulator for Apple Platforms")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }
                .padding(.bottom, 8)
                
                Divider()
                
                // Credits Section
                VStack(alignment: .leading, spacing: 10) {
                    Text("Credits")
                        .font(.headline)
                    
                    Text("Upstream KytyPS5 emulator")
                        .font(.subheadline)
                }
                
                Divider()
                
                // Open Source Libraries Section
                VStack(alignment: .leading, spacing: 10) {
                    Text("Open-Source Libraries & Technologies")
                        .font(.headline)
                    
                    VStack(alignment: .leading, spacing: 8) {
                        LibraryCreditRow(title: "FEXCore", desc: "Fast x86_64 to AArch64 dynamic binary translation engine")
                        LibraryCreditRow(title: "MoltenVK", desc: "Vulkan translation layer implemented on Apple Metal")
                    }
                }
                
                Divider()
                
                // License Notice
                VStack(alignment: .leading, spacing: 6) {
                    Text("License")
                        .font(.headline)
                    Text("KytyPS5 is open-source software.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
            .padding(20)
        }
        .navigationTitle("About KytyPS5")
    }
}

struct LibraryCreditRow: View {
    let title: String
    let desc: String
    
    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title)
                .font(.system(size: 13, weight: .semibold))
            Text(desc)
                .font(.caption)
                .foregroundColor(.secondary)
        }
    }
}
