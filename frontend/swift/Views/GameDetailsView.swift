// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct GameDetailsView: View {
    @Environment(\.dismiss) private var dismiss
    let game: GameItem
    let onLaunch: () -> Void
    let onViewTrophies: () -> Void
    
    @State private var showDeleteSaveConfirmation = false
    @State private var hasSaveData = false
    
    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 20) {
                    // Artwork Header
                    artworkView
                        .shadow(color: .black.opacity(0.15), radius: 6, x: 0, y: 3)
                    
                    VStack(spacing: 8) {
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
                    
                    // Compatibility Report Navigation Button
                    NavigationLink(destination: CompatibilityReportView(game: game)) {
                        HStack(spacing: 10) {
                            Image(systemName: "network")
                                .foregroundColor(.blue)
                            Text("Check Compatibility Report")
                                .fontWeight(.medium)
                            Spacer()
                            Image(systemName: "chevron.right")
                                .font(.caption)
                                .foregroundColor(.secondary)
                        }
                        .padding(.horizontal, 16)
                        .padding(.vertical, 12)
#if os(macOS)
                        .background(Color.secondary.opacity(0.1))
#else
                        .background(Color(UIColor.secondarySystemGroupedBackground))
#endif
                        .cornerRadius(12)
                    }
                    .buttonStyle(.plain)
                    
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
                            HStack(spacing: 6) {
                                Image(systemName: "trophy.fill")
                                Text("Trophies")
                            }
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 10)
                            .background(Color.secondary.opacity(0.15))
                            .foregroundColor(.primary)
                            .cornerRadius(10)
                        }
                        .buttonStyle(.plain)
                        
                        if hasSaveData {
                            Button(role: .destructive, action: {
                                showDeleteSaveConfirmation = true
                            }) {
                                HStack(spacing: 6) {
                                    Image(systemName: "trash")
                                    Text("Clear Save Data")
                                }
                                .frame(maxWidth: .infinity)
                                .padding(.vertical, 8)
                                .background(Color.red.opacity(0.1))
                                .foregroundColor(.red)
                                .cornerRadius(8)
                            }
                            .buttonStyle(.plain)
                        }
                    }
                }
                .padding(20)
            }
            .navigationTitle("Game Details")
#if os(macOS)
            .frame(minWidth: 480, idealWidth: 540, minHeight: 520, idealHeight: 580)
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
            .alert("Clear Save Data", isPresented: $showDeleteSaveConfirmation) {
                Button("Delete", role: .destructive) {
                    if KytyBridge.deleteSaveData(forTitleId: game.titleId) {
                        hasSaveData = false
                    }
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("Delete all save data for \(game.title)? This action cannot be undone.")
            }
            .onAppear {
                checkSaveData()
            }
        }
    }
    
    private func checkSaveData() {
        hasSaveData = KytyBridge.hasSaveData(forTitleId: game.titleId)
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

// MARK: - Compatibility Report Dedicated View

struct CompatibilityReportView: View {
    let game: GameItem
    @StateObject private var compatService = CompatibilityService.shared
    @State private var isLoading = false
    @State private var hasFetched = false
    
    var body: some View {
        Group {
#if os(iOS)
            Form {
                Section {
                    HStack(spacing: 14) {
                        compatArtworkView
                        
                        VStack(alignment: .leading, spacing: 4) {
                            Text(game.title)
                                .font(.headline)
                            Text(game.titleId)
                                .font(.caption.monospaced())
                                .foregroundColor(.blue)
                        }
                    }
                    .padding(.vertical, 4)
                }
                
                Section("Compatibility Status") {
                    let info = compatService.info(for: game.titleId)
                    let status = info?.status ?? .unknown
                    
                    HStack(spacing: 12) {
                        Image(systemName: status.iconName)
                            .font(.title2)
                            .foregroundColor(status.color)
                        
                        VStack(alignment: .leading, spacing: 2) {
                            Text(status.displayName)
                                .font(.headline)
                                .foregroundColor(status.color)
                            Text("Upstream KytyPS5 Report")
                                .font(.caption)
                                .foregroundColor(.secondary)
                        }
                    }
                    .padding(.vertical, 2)
                    
                    if let comment = info?.comment, !comment.isEmpty {
                        Text(comment)
                            .font(.subheadline)
                            .foregroundColor(.primary)
                    } else if hasFetched && info == nil {
                        Text("No report recorded yet for \(game.titleId).")
                            .font(.subheadline)
                            .foregroundColor(.secondary)
                    }
                }
                
                Section {
                    Button(action: {
                        Task {
                            isLoading = true
                            await compatService.refreshDatabase()
                            isLoading = false
                            hasFetched = true
                        }
                    }) {
                        HStack {
                            if isLoading {
                                ProgressView()
                                    .scaleEffect(0.8)
                            } else {
                                Image(systemName: "arrow.clockwise")
                            }
                            Text(hasFetched ? "Refresh Upstream Database" : "Fetch Latest Report")
                        }
                    }
                    .disabled(isLoading)
                }
                
                Section {
                    HStack(alignment: .top, spacing: 10) {
                        Image(systemName: "exclamationmark.triangle.fill")
                            .foregroundColor(.orange)
                        Text("Notice: Reports come from upstream KytyPS5. Because this is a custom fork, performance and game compatibility may differ on your device.")
                            .font(.footnote)
                            .foregroundColor(.secondary)
                    }
                    .padding(.vertical, 2)
                }
            }
            .navigationBarTitleDisplayMode(.inline)
#else
            ScrollView {
                VStack(spacing: 16) {
                    // Game Header
                    HStack(spacing: 16) {
                        compatArtworkView
                            .shadow(color: .black.opacity(0.15), radius: 5, x: 0, y: 2)
                        
                        VStack(alignment: .leading, spacing: 6) {
                            Text(game.title)
                                .font(.title2)
                                .fontWeight(.bold)
                                .lineLimit(2)
                            
                            Text(game.titleId)
                                .font(.system(.subheadline, design: .monospaced))
                                .foregroundColor(.blue)
                                .padding(.horizontal, 8)
                                .padding(.vertical, 3)
                                .background(Color.blue.opacity(0.12))
                                .cornerRadius(6)
                        }
                        Spacer()
                    }
                    .padding(14)
                    .background(Color.secondary.opacity(0.08))
                    .cornerRadius(12)
                    
                    // Compatibility Status Box
                    let info = compatService.info(for: game.titleId)
                    let status = info?.status ?? .unknown
                    
                    GroupBox {
                        VStack(alignment: .leading, spacing: 12) {
                            HStack(spacing: 12) {
                                Image(systemName: status.iconName)
                                    .font(.system(size: 32))
                                    .foregroundColor(status.color)
                                
                                VStack(alignment: .leading, spacing: 3) {
                                    Text(status.displayName)
                                        .font(.title2.weight(.bold))
                                        .foregroundColor(status.color)
                                    Text("Upstream Compatibility Status")
                                        .font(.caption)
                                        .foregroundColor(.secondary)
                                }
                                
                                Spacer()
                            }
                            
                            if let comment = info?.comment, !comment.isEmpty {
                                Divider()
                                VStack(alignment: .leading, spacing: 4) {
                                    Text("Community Notes:")
                                        .font(.caption.weight(.semibold))
                                        .foregroundColor(.secondary)
                                    Text(comment)
                                        .font(.body)
                                        .foregroundColor(.primary)
                                }
                                .padding(.top, 4)
                            } else if hasFetched && info == nil {
                                Divider()
                                Text("No report recorded yet for \(game.titleId).")
                                    .font(.subheadline)
                                    .foregroundColor(.secondary)
                                    .padding(.top, 4)
                            }
                        }
                        .padding(14)
                    }
                    
                    // Fetch / Refresh Button
                    Button(action: {
                        Task {
                            isLoading = true
                            await compatService.refreshDatabase()
                            isLoading = false
                            hasFetched = true
                        }
                    }) {
                        HStack(spacing: 8) {
                            if isLoading {
                                ProgressView()
                                    .scaleEffect(0.8)
                            } else {
                                Image(systemName: "arrow.clockwise")
                            }
                            Text(hasFetched ? "Refresh Upstream Database" : "Fetch Latest Report")
                        }
                        .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.regular)
                    .disabled(isLoading)
                    
                    // Notice
                    HStack(alignment: .top, spacing: 10) {
                        Image(systemName: "exclamationmark.triangle.fill")
                            .foregroundColor(.orange)
                        Text("Notice: Reports come from upstream KytyPS5. Because this is a custom fork, performance and game compatibility may differ on your device.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    .padding(12)
                    .background(Color.secondary.opacity(0.08))
                    .cornerRadius(10)
                }
                .padding(20)
            }
            .frame(minWidth: 480, idealWidth: 540, minHeight: 520, idealHeight: 580)
#endif
        }
        .navigationTitle("Compatibility")
        .onAppear {
            if compatService.info(for: game.titleId) == nil && !hasFetched {
                Task {
                    isLoading = true
                    await compatService.refreshDatabase()
                    isLoading = false
                    hasFetched = true
                }
            } else {
                hasFetched = true
            }
        }
    }
    
    @ViewBuilder
    private var compatArtworkView: some View {
#if os(iOS)
        if let iconPath = game.iconPath, let uiImage = UIImage(contentsOfFile: iconPath) {
            Image(uiImage: uiImage)
                .resizable()
                .aspectRatio(contentMode: .fill)
                .frame(width: 58, height: 58)
                .cornerRadius(10)
        } else {
            placeholderIcon(size: 58)
        }
#else
        if let iconPath = game.iconPath, let nsImage = NSImage(contentsOfFile: iconPath) {
            Image(nsImage: nsImage)
                .resizable()
                .aspectRatio(contentMode: .fill)
                .frame(width: 64, height: 64)
                .cornerRadius(12)
        } else {
            placeholderIcon(size: 64)
        }
#endif
    }
    
    private func placeholderIcon(size: CGFloat) -> some View {
        ZStack {
            RoundedRectangle(cornerRadius: size > 60 ? 12 : 10)
                .fill(LinearGradient(colors: [.blue, .purple], startPoint: .topLeading, endPoint: .bottomTrailing))
                .frame(width: size, height: size)
            Image(systemName: "gamecontroller.fill")
                .font(.system(size: size * 0.45))
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
