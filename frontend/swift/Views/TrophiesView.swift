// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct TrophiesView: View {
    @Environment(\.dismiss) private var dismiss
    let game: GameItem
    
    @State private var trophies: [TrophyItem] = []
    @State private var isLoading = true
    @State private var searchText: String = ""
    
    struct TrophyItem: Identifiable {
        let id: Int
        let name: String
        let description: String
        let grade: String
        let hidden: Bool
        let iconData: Data?
    }
    
    var gradeCounts: (platinum: Int, gold: Int, silver: Int, bronze: Int) {
        var p = 0, g = 0, s = 0, b = 0
        for t in trophies {
            switch t.grade.lowercased() {
            case "platinum": p += 1
            case "gold": g += 1
            case "silver": s += 1
            default: b += 1
            }
        }
        return (p, g, s, b)
    }
    
    var filteredTrophies: [TrophyItem] {
        let query = searchText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !query.isEmpty else {
            return trophies
        }
        return trophies.filter { t in
            t.name.localizedCaseInsensitiveContains(query) ||
            t.description.localizedCaseInsensitiveContains(query)
        }
    }
    
    var body: some View {
        NavigationStack {
            Group {
                if isLoading {
                    ProgressView("Loading Trophies...")
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else if trophies.isEmpty {
                    VStack(spacing: 14) {
                        Image(systemName: "trophy")
                            .font(.system(size: 54))
                            .foregroundColor(.secondary)
                        Text("No Trophies Found")
                            .font(.title3)
                            .fontWeight(.bold)
                        Text("This game does not contain trophy packages (sce_sys/trophy2) or trophies could not be loaded.")
                            .font(.subheadline)
                            .foregroundColor(.secondary)
                            .multilineTextAlignment(.center)
                            .padding(.horizontal, 32)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else {
                    VStack(spacing: 0) {
                        // Summary Banner
                        VStack(alignment: .leading, spacing: 10) {
                            HStack(alignment: .firstTextBaseline) {
                                Text(game.title)
                                    .font(.headline)
                                    .lineLimit(1)
                                
                                Spacer(minLength: 8)
                                
                                Text("\(trophies.count) \(trophies.count == 1 ? "Trophy" : "Trophies")")
                                    .font(.subheadline)
                                    .foregroundColor(.secondary)
                                    .fixedSize(horizontal: true, vertical: false)
                            }
                            
                            // Grade breakdown pills
                            ScrollView(.horizontal, showsIndicators: false) {
                                HStack(spacing: 8) {
                                    if gradeCounts.platinum > 0 {
                                        GradeBadge(grade: "Platinum", count: gradeCounts.platinum, color: Color(red: 0.7, green: 0.85, blue: 1.0))
                                    }
                                    if gradeCounts.gold > 0 {
                                        GradeBadge(grade: "Gold", count: gradeCounts.gold, color: Color(red: 1.0, green: 0.84, blue: 0.0))
                                    }
                                    if gradeCounts.silver > 0 {
                                        GradeBadge(grade: "Silver", count: gradeCounts.silver, color: Color(red: 0.75, green: 0.75, blue: 0.78))
                                    }
                                    if gradeCounts.bronze > 0 {
                                        GradeBadge(grade: "Bronze", count: gradeCounts.bronze, color: Color(red: 0.8, green: 0.5, blue: 0.2))
                                    }
                                }
                            }
                        }
                        .padding(.horizontal, 16)
                        .padding(.vertical, 12)
#if os(macOS)
                        .background(Color(NSColor.controlBackgroundColor))
#else
                        .background(Color(UIColor.secondarySystemGroupedBackground))
#endif
                        
                        Divider()
                        
                        List {
                            if filteredTrophies.isEmpty {
                                VStack(spacing: 8) {
                                    Image(systemName: "magnifyingglass")
                                        .font(.title2)
                                        .foregroundColor(.secondary)
                                    Text("No trophies match \"\(searchText)\"")
                                        .font(.subheadline)
                                        .foregroundColor(.secondary)
                                }
                                .frame(maxWidth: .infinity, alignment: .center)
                                .padding(.vertical, 24)
#if os(iOS)
                                .listRowSeparator(.hidden)
#endif
                            } else {
                                ForEach(filteredTrophies) { t in
                                    TrophyRow(trophy: t)
                                }
                            }
                        }
#if os(macOS)
                        .listStyle(.inset)
#else
                        .listStyle(.plain)
#endif
                    }
                }
            }
            .navigationTitle("Trophies")
#if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            .searchable(text: $searchText, prompt: "Search trophies")
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                        .font(.body.weight(.semibold))
                }
            }
#else
            .frame(minWidth: 640, idealWidth: 700, minHeight: 520, idealHeight: 580)
            .toolbar {
                ToolbarItem(placement: .automatic) {
                    HStack(spacing: 6) {
                        Image(systemName: "magnifyingglass")
                            .foregroundColor(.secondary)
                            .font(.system(size: 12))
                        TextField("Search trophies...", text: $searchText)
                            .textFieldStyle(.roundedBorder)
                            .frame(width: 220)
                        if !searchText.isEmpty {
                            Button(action: { searchText = "" }) {
                                Image(systemName: "xmark.circle.fill")
                                    .foregroundColor(.secondary)
                            }
                            .buttonStyle(.plain)
                        }
                    }
                }
                ToolbarItem(placement: .cancellationAction) {
                    Button("Close") { dismiss() }
                        .keyboardShortcut(.cancelAction)
                }
            }
#endif
            .onAppear {
                loadTrophies()
            }
        }
    }
    
    private func loadTrophies() {
        DispatchQueue.global(qos: .userInitiated).async {
            let raw = KytyBridge.getTrophiesForGamePath(game.path)
            var items: [TrophyItem] = []
            for d in raw {
                let id = (d["id"] as? NSNumber)?.intValue ?? 0
                let name = d["name"] as? String ?? "Trophy \(id)"
                let desc = d["description"] as? String ?? ""
                let grade = d["grade"] as? String ?? "Bronze"
                let hidden = (d["hidden"] as? NSNumber)?.boolValue ?? false
                let iconData = d["iconData"] as? Data
                items.append(TrophyItem(
                    id: id,
                    name: name,
                    description: desc,
                    grade: grade,
                    hidden: hidden,
                    iconData: iconData
                ))
            }
            DispatchQueue.main.async {
                self.trophies = items
                self.isLoading = false
            }
        }
    }
}

struct GradeBadge: View {
    let grade: String
    let count: Int
    let color: Color
    
    var body: some View {
        HStack(spacing: 4) {
            Image(systemName: "trophy.fill")
                .font(.system(size: 11))
                .foregroundColor(color)
            Text("\(count)")
                .font(.system(size: 12, weight: .semibold))
                .foregroundColor(.primary)
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
        .background(color.opacity(0.15))
        .cornerRadius(6)
    }
}

struct TrophyRow: View {
    let trophy: TrophiesView.TrophyItem
    
    var gradeColor: Color {
        switch trophy.grade.lowercased() {
        case "platinum": return Color(red: 0.7, green: 0.85, blue: 1.0)
        case "gold": return Color(red: 1.0, green: 0.84, blue: 0.0)
        case "silver": return Color(red: 0.75, green: 0.75, blue: 0.78)
        default: return Color(red: 0.8, green: 0.5, blue: 0.2)
        }
    }
    
    var body: some View {
        HStack(spacing: 12) {
            // Trophy Icon
            ZStack {
                if let data = trophy.iconData,
                   let img = platformImage(from: data) {
                    img
                        .resizable()
                        .aspectRatio(contentMode: .fill)
                        .frame(width: 44, height: 44)
                        .cornerRadius(8)
                } else {
                    RoundedRectangle(cornerRadius: 8)
                        .fill(gradeColor.opacity(0.18))
                        .frame(width: 44, height: 44)
                    Image(systemName: "trophy.fill")
                        .font(.system(size: 20))
                        .foregroundColor(gradeColor)
                }
            }
            
            // Details
            VStack(alignment: .leading, spacing: 4) {
                HStack(alignment: .top, spacing: 8) {
                    Text(trophy.name)
                        .font(.system(size: 14, weight: .semibold))
                        .foregroundColor(.primary)
                        .lineLimit(2)
                        .fixedSize(horizontal: false, vertical: true)
                    
                    Spacer(minLength: 8)
                    
                    if trophy.hidden {
                        Text("Hidden")
                            .font(.system(size: 10, weight: .semibold))
                            .foregroundColor(.secondary)
                            .padding(.horizontal, 6)
                            .padding(.vertical, 2)
                            .background(Color.secondary.opacity(0.12))
                            .cornerRadius(4)
                            .fixedSize(horizontal: true, vertical: true)
                    }
                    
                    Text(trophy.grade)
                        .font(.system(size: 10, weight: .bold))
                        .foregroundColor(gradeColor)
                        .padding(.horizontal, 6)
                        .padding(.vertical, 2)
                        .background(gradeColor.opacity(0.15))
                        .cornerRadius(4)
                        .fixedSize(horizontal: true, vertical: true)
                        .layoutPriority(1)
                }
                
                if !trophy.description.isEmpty {
                    Text(trophy.description)
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .lineLimit(nil)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
        }
        .padding(.vertical, 4)
    }
    
    private func platformImage(from data: Data) -> Image? {
#if os(iOS)
        if let ui = UIImage(data: data) {
            return Image(uiImage: ui)
        }
#else
        if let ns = NSImage(data: data) {
            return Image(nsImage: ns)
        }
#endif
        return nil
    }
}
