// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct TrophyOverviewCounts {
    var platinum: Int = 0
    var gold: Int = 0
    var silver: Int = 0
    var bronze: Int = 0
    var total: Int = 0
}

struct OverviewGameItem: Identifiable {
    var id: UUID { game.id }
    let game: GameItem
    let trophyCount: Int
    let cachedIcon: PlatformImage?
}

struct TrophyOverviewView: View {
    @Environment(\.dismiss) private var dismiss
    let games: [GameItem]
    
    @State private var overviewItems: [OverviewGameItem] = []
    @State private var totalCounts = TrophyOverviewCounts()
    @State private var isLoading = true
    
    var body: some View {
        NavigationStack {
            Group {
                if isLoading {
                    ProgressView("Analyzing Library Trophies...")
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else if overviewItems.isEmpty {
                    VStack(spacing: 14) {
                        PlatformIcon("trophy", size: 54)
                            .foregroundColor(.secondary)
                        Text("No Trophies in Library")
                            .font(.title3)
                            .fontWeight(.bold)
                        Text("None of your installed games contain trophy packages (sce_sys/trophy2).")
                            .font(.subheadline)
                            .foregroundColor(.secondary)
                            .multilineTextAlignment(.center)
                            .padding(.horizontal, 32)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else {
                    List {
                        Section {
                            VStack(alignment: .leading, spacing: 12) {
                                HStack(alignment: .firstTextBaseline) {
                                    Text("Library Trophy Collection")
                                        .font(.headline)
                                    Spacer()
                                    Text("\(totalCounts.total) Total")
                                        .font(.subheadline)
                                        .foregroundColor(.secondary)
                                }
                                
                                ScrollView(.horizontal, showsIndicators: false) {
                                    HStack(spacing: 8) {
                                        if totalCounts.platinum > 0 {
                                            GradeBadge(grade: "Platinum", count: totalCounts.platinum, color: Color(red: 0.7, green: 0.85, blue: 1.0))
                                        }
                                        if totalCounts.gold > 0 {
                                            GradeBadge(grade: "Gold", count: totalCounts.gold, color: Color(red: 1.0, green: 0.84, blue: 0.0))
                                        }
                                        if totalCounts.silver > 0 {
                                            GradeBadge(grade: "Silver", count: totalCounts.silver, color: Color(red: 0.75, green: 0.75, blue: 0.78))
                                        }
                                        if totalCounts.bronze > 0 {
                                            GradeBadge(grade: "Bronze", count: totalCounts.bronze, color: Color(red: 0.8, green: 0.5, blue: 0.2))
                                        }
                                    }
                                }
                            }
                            .padding(.vertical, 6)
                        }
                        
                        Section(header: Text("Games with Trophies (\(overviewItems.count))")) {
                            ForEach(overviewItems) { item in
                                NavigationLink(destination: TrophiesView(game: item.game)) {
                                    HStack(spacing: 12) {
                                        ZStack {
                                            if let img = item.cachedIcon {
                                                platformImage(img)
                                                    .resizable()
                                                    .aspectRatio(contentMode: .fill)
                                                    .frame(width: 38, height: 38)
                                                    .cornerRadius(6)
                                            } else {
                                                PlatformIcon("gamecontroller.fill", size: 20)
                                                    .foregroundColor(.secondary)
                                                    .frame(width: 38, height: 38)
                                            }
                                        }
                                        .frame(width: 38, height: 38)
                                        
                                        VStack(alignment: .leading, spacing: 2) {
                                            Text(item.game.title)
                                                .font(.system(size: 14, weight: .semibold))
                                                .foregroundColor(.primary)
                                                .lineLimit(1)
                                            Text("\(item.game.titleId) · \(item.trophyCount) Trophies")
                                                .font(.caption)
                                                .foregroundColor(.secondary)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            .navigationTitle("Trophies Overview")
#if os(iOS) || SKIP
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
#else
            .frame(minWidth: 500, idealWidth: 560, minHeight: 460, idealHeight: 520)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Done") { dismiss() }
                }
            }
#endif
            .onAppear {
                loadAllTrophies()
            }
        }
    }
    
    private func loadAllTrophies() {
        KytyDispatch.background {
            var items: [OverviewGameItem] = []
            var counts = TrophyOverviewCounts()
            
            for g in self.games {
                if KytyBridge.hasTrophies(forGamePath: g.path) {
                    let raw = KytyBridge.getTrophiesForGamePath(g.path, includeIcons: false)
                    if !raw.isEmpty {
                        counts.total += raw.count
                        for d in raw {
                            let grade = (d["grade"] as? String ?? "Bronze").lowercased()
                            switch grade {
                            case "platinum": counts.platinum += 1
                            case "gold": counts.gold += 1
                            case "silver": counts.silver += 1
                            default: counts.bronze += 1
                            }
                        }
                        var icon: PlatformImage? = nil
                        if let path = g.iconPath {
                            icon = loadPlatformImage(fromPath: path)
                        }
                        items.append(OverviewGameItem(
                            game: g,
                            trophyCount: raw.count,
                            cachedIcon: icon
                        ))
                    }
                }
            }
            
            let finalItems = items
            let finalCounts = counts
            KytyDispatch.main {
                self.overviewItems = finalItems
                self.totalCounts = finalCounts
                self.isLoading = false
            }
        }
    }
}
