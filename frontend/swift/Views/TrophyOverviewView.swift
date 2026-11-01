// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct TrophyOverviewView: View {
    @Environment(\.dismiss) private var dismiss
    let games: [GameItem]
    
    @State private var gameTrophies: [(game: GameItem, trophies: [TrophiesView.TrophyItem])] = []
    @State private var isLoading = true
    
    var totalCounts: (platinum: Int, gold: Int, silver: Int, bronze: Int, total: Int) {
        var p = 0, g = 0, s = 0, b = 0, total = 0
        for (_, list) in gameTrophies {
            for t in list {
                total += 1
                switch t.grade.lowercased() {
                case "platinum": p += 1
                case "gold": g += 1
                case "silver": s += 1
                default: b += 1
                }
            }
        }
        return (p, g, s, b, total)
    }
    
    var body: some View {
        NavigationStack {
            Group {
                if isLoading {
                    ProgressView("Analyzing Library Trophies...")
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else if gameTrophies.isEmpty {
                    VStack(spacing: 14) {
                        Image(systemName: "trophy")
                            .font(.system(size: 54))
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
                        
                        Section(header: Text("Games with Trophies (\(gameTrophies.count))")) {
                            ForEach(gameTrophies, id: \.game.id) { item in
                                NavigationLink(destination: TrophiesView(game: item.game)) {
                                    HStack(spacing: 12) {
                                        if let iconPath = item.game.iconPath {
#if os(iOS)
                                            if let uiImage = UIImage(contentsOfFile: iconPath) {
                                                Image(uiImage: uiImage)
                                                    .resizable()
                                                    .aspectRatio(contentMode: .fill)
                                                    .frame(width: 38, height: 38)
                                                    .cornerRadius(6)
                                            }
#else
                                            if let nsImage = NSImage(contentsOfFile: iconPath) {
                                                Image(nsImage: nsImage)
                                                    .resizable()
                                                    .aspectRatio(contentMode: .fill)
                                                    .frame(width: 38, height: 38)
                                                    .cornerRadius(6)
                                            }
#endif
                                        }
                                        
                                        VStack(alignment: .leading, spacing: 2) {
                                            Text(item.game.title)
                                                .font(.system(size: 14, weight: .semibold))
                                                .foregroundColor(.primary)
                                                .lineLimit(1)
                                            Text("\(item.game.titleId) · \(item.trophies.count) Trophies")
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
#if os(macOS)
            .frame(minWidth: 500, idealWidth: 560, minHeight: 460, idealHeight: 520)
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
                loadAllTrophies()
            }
        }
    }
    
    private func loadAllTrophies() {
        DispatchQueue.global(qos: .userInitiated).async {
            var results: [(game: GameItem, trophies: [TrophiesView.TrophyItem])] = []
            
            for g in games {
                if KytyBridge.hasTrophies(forGamePath: g.path) {
                    let raw = KytyBridge.getTrophiesForGamePath(g.path)
                    var list: [TrophiesView.TrophyItem] = []
                    for (idx, d) in raw.enumerated() {
                        let name = d["name"] as? String ?? "Trophy #\(idx)"
                        let desc = d["description"] as? String ?? ""
                        let grade = d["grade"] as? String ?? "Bronze"
                        let hidden = (d["hidden"] as? NSNumber)?.boolValue ?? false
                        let iconData = d["iconData"] as? Data
                        list.append(TrophiesView.TrophyItem(id: idx, name: name, description: desc, grade: grade, hidden: hidden, iconData: iconData))
                    }
                    if !list.isEmpty {
                        results.append((game: g, trophies: list))
                    }
                }
            }
            
            DispatchQueue.main.async {
                self.gameTrophies = results
                self.isLoading = false
            }
        }
    }
}
