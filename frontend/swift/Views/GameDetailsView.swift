// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct GameDetailsView: View {
    @Environment(\.dismiss) private var dismiss
    let game: GameItem
    let onLaunch: () -> Void
    let onViewTrophies: () -> Void
    
    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 20) {
                    // Artwork Header
                    artworkView
                        .shadow(color: .black.opacity(0.15), radius: 6, x: 0, y: 3)
                    
                    VStack(spacing: 6) {
                        Text(game.title)
                            .font(.title2)
                            .fontWeight(.bold)
                            .multilineTextAlignment(.center)
                            .padding(.horizontal, 16)
                        
                        Text(game.titleId)
                            .font(.system(.subheadline, design: .monospaced))
                            .foregroundColor(.blue)
                            .padding(.horizontal, 8)
                            .padding(.vertical, 3)
                            .background(Color.blue.opacity(0.12))
                            .cornerRadius(6)
                    }
                    
                    // Metadata Details Table
                    VStack(spacing: 12) {
                        DetailInfoRow(label: "Application Version", value: game.rawVersion)
                        Divider()
                        DetailInfoRow(label: "Required System Software", value: game.firmwareVersion)
                        Divider()
                        DetailInfoRow(label: "Storage Size", value: game.sizeString)
                        Divider()
                        DetailInfoRow(label: "Package Format", value: game.isDirectory ? "Game Directory" : "Standalone Executable")
                        Divider()
                        VStack(alignment: .leading, spacing: 4) {
                            Text("Full Path")
                                .font(.caption)
                                .foregroundColor(.secondary)
                            Text(game.path)
                                .font(.system(.caption, design: .monospaced))
                                .foregroundColor(.primary)
                                .textSelection(.enabled)
                        }
                        .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    .padding(16)
#if os(macOS)
                    .background(Color.secondary.opacity(0.1))
#else
                    .background(Color(UIColor.secondarySystemGroupedBackground))
#endif
                    .cornerRadius(12)
                    
                    // Actions
                    VStack(spacing: 10) {
                        Button(action: {
                            dismiss()
                            onLaunch()
                        }) {
                            HStack(spacing: 8) {
                                Image(systemName: "play.fill")
                                Text("Launch Game")
                            }
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 12)
                            .background(Color.blue)
                            .foregroundColor(.white)
                            .cornerRadius(10)
                        }
                        .buttonStyle(.plain)
                        
                        Button(action: {
                            dismiss()
                            DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) {
                                onViewTrophies()
                            }
                        }) {
                            HStack(spacing: 8) {
                                Image(systemName: "trophy.fill")
                                Text("View Trophies")
                            }
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 12)
                            .background(Color.secondary.opacity(0.15))
                            .foregroundColor(.primary)
                            .cornerRadius(10)
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(20)
            }
            .navigationTitle("Game Details")
#if os(macOS)
            .frame(minWidth: 460, idealWidth: 500, minHeight: 480, idealHeight: 540)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Close") { dismiss() }
                }
            }
#else
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                        .font(.body.weight(.semibold))
                }
            }
#endif
        }
    }
    
    @ViewBuilder
    private var artworkView: some View {
#if os(iOS)
        if let iconPath = game.iconPath, let uiImage = UIImage(contentsOfFile: iconPath) {
            Image(uiImage: uiImage)
                .resizable()
                .aspectRatio(contentMode: .fill)
                .frame(width: 130, height: 130)
                .cornerRadius(16)
        } else {
            placeholderArtwork
        }
#else
        if let iconPath = game.iconPath, let nsImage = NSImage(contentsOfFile: iconPath) {
            Image(nsImage: nsImage)
                .resizable()
                .aspectRatio(contentMode: .fill)
                .frame(width: 130, height: 130)
                .cornerRadius(16)
        } else {
            placeholderArtwork
        }
#endif
    }
    
    private var placeholderArtwork: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 16)
                .fill(LinearGradient(colors: [.blue, .purple], startPoint: .topLeading, endPoint: .bottomTrailing))
                .frame(width: 130, height: 130)
            Image(systemName: "gamecontroller.fill")
                .font(.system(size: 48))
                .foregroundColor(.white)
        }
    }
}

struct DetailInfoRow: View {
    let label: String
    let value: String
    
    var body: some View {
        HStack(alignment: .top) {
            Text(label)
                .foregroundColor(.secondary)
            Spacer(minLength: 12)
            Text(value)
                .fontWeight(.medium)
                .font(.system(.body, design: .monospaced))
                .multilineTextAlignment(.trailing)
        }
    }
}
