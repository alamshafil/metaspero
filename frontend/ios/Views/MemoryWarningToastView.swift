// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct MemoryPressureToastData: Identifiable, Equatable {
    let id = UUID()
    let level: String
    let reason: String
    let usedMB: Double
    let limitMB: Double
    let availMB: Double
    let message: String
}

@MainActor
final class MemoryPressureMonitor: ObservableObject {
    static let shared = MemoryPressureMonitor()
    
    @Published var activeToast: MemoryPressureToastData? = nil
    private var dismissTask: Task<Void, Never>? = nil
    
    private init() {
        NotificationCenter.default.addObserver(
            forName: NSNotification.Name.KytyMemoryPressure,
            object: nil,
            queue: .main
        ) { [weak self] notif in
            guard let self = self else { return }
            
            // Check user settings preference (default true)
            let isEnabled = UserDefaults.standard.object(forKey: "enableMemoryWarningToast") as? Bool ?? true
            guard isEnabled else { return }
            
            guard let info = notif.userInfo else { return }
            let level = info[KytyMemoryPressureLevelKey] as? String ?? "Warning"
            let reason = info[KytyMemoryPressureReasonKey] as? String ?? "High memory pressure"
            let usedMB = (info[KytyMemoryUsedMBKey] as? NSNumber)?.doubleValue ?? 0
            let availMB = (info[KytyMemoryAvailMBKey] as? NSNumber)?.doubleValue ?? 0
            let limitMB = (info[KytyMemoryLimitMBKey] as? NSNumber)?.doubleValue ?? 0
            let message = info["message"] as? String ?? "Memory pressure detected"
            
            let data = MemoryPressureToastData(
                level: level,
                reason: reason,
                usedMB: usedMB,
                limitMB: limitMB,
                availMB: availMB,
                message: message
            )
            
            withAnimation(.spring(response: 0.4, dampingFraction: 0.8)) {
                self.activeToast = data
            }
            
            self.dismissTask?.cancel()
            self.dismissTask = Task {
                try? await Task.sleep(nanoseconds: 5_000_000_000)
                if !Task.isCancelled {
                    withAnimation(.easeInOut(duration: 0.3)) {
                        self.activeToast = nil
                    }
                }
            }
        }
    }
    
    func dismiss() {
        dismissTask?.cancel()
        withAnimation(.easeInOut(duration: 0.25)) {
            activeToast = nil
        }
    }
}

struct MemoryWarningToastBanner: View {
    let data: MemoryPressureToastData
    let onDismiss: () -> Void
    
    var isCritical: Bool {
        data.level.lowercased() == "critical"
    }
    
    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: isCritical ? "exclamationmark.octagon.fill" : "exclamationmark.triangle.fill")
                .foregroundColor(isCritical ? .red : .orange)
                .font(.system(size: 20))
            
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 6) {
                    Text("Jetsam Memory \(data.level)")
                        .font(.system(size: 13, weight: .semibold))
                        .foregroundColor(isCritical ? .red : .orange)
                    
                    Text("(\(Int(data.usedMB)) / \(Int(data.limitMB)) MB)")
                        .font(.system(size: 12))
                        .foregroundColor(.secondary)
                }
                
                Text("\(Int(data.availMB)) MB remaining before kill - \(data.reason)")
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)
                    .lineLimit(1)
            }
            
            Spacer(minLength: 4)
            
            Button(action: onDismiss) {
                Image(systemName: "xmark")
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundColor(.secondary)
                    .padding(6)
            }
            .buttonStyle(.plain)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
        .background(
            RoundedRectangle(cornerRadius: 14, style: .continuous)
                #if os(iOS)
                .fill(Color(UIColor.secondarySystemBackground))
                #else
                .fill(Color(NSColor.windowBackgroundColor))
                #endif
                .shadow(color: Color.black.opacity(0.12), radius: 8, x: 0, y: 3)
        )
        .padding(.horizontal, 16)
        .padding(.top, 8)
        .gesture(
            DragGesture(minimumDistance: 10)
                .onEnded { value in
                    if value.translation.height < -10 {
                        onDismiss()
                    }
                }
        )
    }
}

struct MemoryWarningToastModifier: ViewModifier {
    @ObservedObject private var monitor = MemoryPressureMonitor.shared
    
    func body(content: Content) -> some View {
        ZStack(alignment: .top) {
            content
            
            if let toast = monitor.activeToast {
                MemoryWarningToastBanner(data: toast) {
                    monitor.dismiss()
                }
                .transition(.move(edge: .top).combined(with: .opacity))
                .zIndex(9999)
            }
        }
    }
}

extension View {
    func withMemoryWarningToast() -> some View {
        self.modifier(MemoryWarningToastModifier())
    }
}
