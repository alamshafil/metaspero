// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct TrophiesView: View {
    @Environment(\.dismiss) private var dismiss
    let game: GameItem
    
    @State private var trophies: [TrophyItem] = []
    @State private var isLoading = true
    @State private var selectedFilter: TrophyFilter = .all
    @State private var searchText: String = ""
    
    enum TrophyFilter: String, CaseIterable, Identifiable {
        case all = "All"
        case earned = "Earned"
        case locked = "Locked"
        var id: String { rawValue }
    }
    
    struct TrophyItem: Identifiable {
        let id: Int
        let name: String
        let description: String
        let grade: String
        let hidden: Bool
        let unlocked: Bool
        let earnedDate: String?
        let iconData: Data?
    }
    
    var unlockedCount: Int {
        trophies.filter { $0.unlocked }.count
    }
    
    var filteredTrophies: [TrophyItem] {
        trophies.filter { t in
            switch selectedFilter {
            case .all: break
            case .earned: if !t.unlocked { return false }
            case .locked: if t.unlocked { return false }
            }
            if !searchText.isEmpty {
                return t.name.localizedCaseInsensitiveContains(searchText) || t.description.localizedCaseInsensitiveContains(searchText)
            }
            return true
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
                        VStack(spacing: 12) {
                            HStack {
                                VStack(alignment: .leading, spacing: 3) {
                                    Text(game.title)
                                        .font(.headline)
                                    Text("\(unlockedCount) of \(trophies.count) Unlocked (\(Int((Double(unlockedCount) / Double(max(1, trophies.count))) * 100))%)")
                                        .font(.caption)
                                        .foregroundColor(.secondary)
                                }
                                Spacer()
                            }
                            
                            ProgressView(value: Double(unlockedCount), total: Double(max(1, trophies.count)))
                                .accentColor(.blue)
                            
                            // Filter Picker
                            Picker("", selection: $selectedFilter) {
                                ForEach(TrophyFilter.allCases) { filter in
                                    Text(filter.rawValue).tag(filter)
                                }
                            }
                            .pickerStyle(.segmented)
                        }
                        .padding(.horizontal, 20)
                        .padding(.vertical, 14)
#if os(macOS)
                        .background(Color(NSColor.controlBackgroundColor))
#else
                        .background(Color(UIColor.secondarySystemGroupedBackground))
#endif
                        
                        Divider()
                        
                        List {
                            if filteredTrophies.isEmpty {
                                Text("No trophies match the filter.")
                                    .font(.subheadline)
                                    .foregroundColor(.secondary)
                                    .padding(.vertical, 8)
                            } else {
                                ForEach(filteredTrophies) { t in
                                    TrophyRow(trophy: t)
                                }
                            }
                        }
#if os(macOS)
                        .listStyle(.inset)
#endif
                    }
#if os(iOS)
                    .searchable(text: $searchText, prompt: "Search trophies")
#endif
                }
            }
            .navigationTitle("Trophies")
#if os(macOS)
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
#else
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Done") { dismiss() }
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
                let unlocked = (d["unlocked"] as? NSNumber)?.boolValue ?? false
                let earnedDate = d["earnedDate"] as? String
                let iconData = d["iconData"] as? Data
                items.append(TrophyItem(
                    id: id,
                    name: name,
                    description: desc,
                    grade: grade,
                    hidden: hidden,
                    unlocked: unlocked,
                    earnedDate: earnedDate,
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
                        .opacity(trophy.unlocked ? 1.0 : 0.4)
                } else {
                    RoundedRectangle(cornerRadius: 8)
                        .fill(gradeColor.opacity(trophy.unlocked ? 0.25 : 0.12))
                        .frame(width: 44, height: 44)
                    Image(systemName: trophy.unlocked ? "trophy.fill" : "trophy")
                        .font(.system(size: 22))
                        .foregroundColor(trophy.unlocked ? gradeColor : .secondary)
                }
            }
            
            // Details
            VStack(alignment: .leading, spacing: 4) {
                HStack(alignment: .top, spacing: 8) {
                    Text(trophy.hidden && !trophy.unlocked ? "Hidden Trophy" : trophy.name)
                        .font(.system(size: 14, weight: .semibold))
                        .foregroundColor(trophy.unlocked ? .primary : .secondary)
                        .lineLimit(2)
                        .fixedSize(horizontal: false, vertical: true)
                    
                    Spacer(minLength: 8)
                    
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
                
                Text(trophy.hidden && !trophy.unlocked ? "Details hidden until earned" : trophy.description)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .lineLimit(nil)
                    .fixedSize(horizontal: false, vertical: true)
                
                if let earned = trophy.earnedDate, !earned.isEmpty {
                    Text("Earned on \(earned)")
                        .font(.system(size: 9))
                        .foregroundColor(.green)
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
