// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

#if SKIP
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.__
import androidx.compose.material.icons.outlined.__
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
#endif

#if SKIP
public enum PlatformIconMap {
    public static func resolve(_ name: String) -> ImageVector {
        switch name {
        // Gaming & Emulation
        case "gamecontroller", "gamecontroller.fill":
            return Icons.Filled.SportsEsports
        case "trophy", "trophy.fill":
            return Icons.Filled.EmojiEvents
        case "circle.circle.fill":
            return Icons.Filled.RadioButtonChecked
        case "rectangle.inset.filled":
            return Icons.Filled.CropSquare

        // Hardware & Performance
        case "cpu", "cpu.fill":
            return Icons.Filled.Memory
        case "display", "display.fill":
            return Icons.Filled.ConnectedTv
        case "waveform.path.ecg":
            return Icons.Filled.ShowChart

        // Developer, Debug & Logs
        case "terminal", "terminal.fill":
            return Icons.Filled.Terminal
        case "ladybug.fill":
            return Icons.Filled.BugReport
        case "arrow.clockwise", "arrow.clockwise.fill":
            return Icons.Filled.Refresh
        case "doc.on.doc", "doc.on.doc.fill":
            return Icons.Filled.ContentCopy
        case "folder", "folder.fill":
            return Icons.Filled.Folder
        case "network":
            return Icons.Filled.Language
        case "arrow.down.doc.fill":
            return Icons.Filled.Download
        case "arrow.up.right":
            return Icons.Filled.OpenInNew

        // UI Controls & Navigation
        case "slider.horizontal.3":
            return Icons.Filled.Tune
        case "square.grid.2x2", "square.grid.2x2.fill":
            return Icons.Filled.GridView
        case "list.bullet", "list.bullet.rectangle":
            return Icons.Filled.List
        case "gearshape", "gearshape.fill":
            return Icons.Filled.Settings
        case "info.circle", "info.circle.fill":
            return Icons.Filled.Info
        case "magnifyingglass":
            return Icons.Filled.Search
        case "trash":
            return Icons.Filled.Delete
        case "play.fill", "play.circle.fill":
            return Icons.Filled.PlayArrow
        case "plus", "plus.circle.fill":
            return Icons.Filled.Add
        case "minus.circle", "minus.circle.fill":
            return Icons.Filled.RemoveCircle
        case "xmark", "xmark.circle", "xmark.circle.fill":
            return Icons.Filled.Cancel
        case "checkmark", "checkmark.seal.fill":
            return Icons.Filled.Check
        case "chevron.left":
            return Icons.Filled.KeyboardArrowLeft
        case "chevron.right":
            return Icons.Filled.KeyboardArrowRight
        case "chevron.up":
            return Icons.Filled.KeyboardArrowUp
        case "chevron.down":
            return Icons.Filled.KeyboardArrowDown
        case "ellipsis", "ellipsis.circle", "ellipsis.circle.fill":
            return Icons.Filled.MoreHoriz
        case "exclamationmark.triangle.fill", "exclamationmark.octagon", "exclamationmark.octagon.fill":
            return Icons.Filled.Warning
        case "photo.fill":
            return Icons.Filled.Image
        case "questionmark.circle":
            return Icons.Filled.HelpOutline

        default:
            return Icons.Outlined.HelpOutline
        }
    }
}
#endif

public struct PlatformIcon: View {
    let name: String
    var size: CGFloat? = nil

    public init(_ name: String, size: CGFloat? = nil) {
        self.name = name
        self.size = size
    }

    public var body: some View {
        #if SKIP
        ComposeView { _ in
            let vector: ImageVector = PlatformIconMap.resolve(name)
            let effectiveSize: Double = Double(size ?? 22.0)
            Icon(
                imageVector = vector,
                contentDescription: name,
                modifier = Modifier.size(effectiveSize.dp),
                tint = LocalContentColor.current
            )
        }
        #else
        if let s = size {
            Image(systemName: name)
                .font(.system(size: s))
        } else {
            Image(systemName: name)
        }
        #endif
    }
}

public struct PlatformLabel: View {
    let title: LocalizedStringKey
    let systemImage: String

    public init(_ title: LocalizedStringKey, systemImage: String) {
        self.title = title
        self.systemImage = systemImage
    }

    public init(_ titleString: String, systemImage: String) {
        self.title = LocalizedStringKey(titleString)
        self.systemImage = systemImage
    }

    public var body: some View {
        Label {
            Text(title)
        } icon: {
            PlatformIcon(systemImage)
        }
    }
}

// MARK: - Cross-Platform Dispatch

#if SKIP
public struct KytyDispatch {
    public static func background(_ block: @escaping () -> Void) {
        java.lang.Thread({
            block()
        }).start()
    }
    public static func main(_ block: @escaping () -> Void) {
        DispatchQueue.main.async {
            block()
        }
    }
}
#else
public struct KytyDispatch {
    public static func background(_ block: @escaping @Sendable () -> Void) {
        DispatchQueue.global(qos: .userInitiated).async {
            block()
        }
    }
    public static func main(_ block: @escaping @Sendable () -> Void) {
        DispatchQueue.main.async {
            block()
        }
    }
}
#endif

// MARK: - Cross-Platform Image Abstraction

public func readFileData(path: String) -> Data? {
    #if SKIP
    return try? Data(contentsOfFile: path)
    #else
    return try? Data(contentsOf: URL(fileURLWithPath: path))
    #endif
}

#if os(macOS)
import AppKit
public typealias PlatformImage = NSImage

public func platformImage(_ image: PlatformImage) -> Image {
    return Image(nsImage: image)
}

public func decodePlatformImage(from data: Data) -> PlatformImage? {
    return NSImage(data: data)
}

public func loadPlatformImage(fromPath path: String) -> PlatformImage? {
    return NSImage(contentsOfFile: path)
}
#else
import UIKit
public typealias PlatformImage = UIImage

public func platformImage(_ image: PlatformImage) -> Image {
    return Image(uiImage: image)
}

public func decodePlatformImage(from data: Data) -> PlatformImage? {
    return UIImage(data: data)
}

public func loadPlatformImage(fromPath path: String) -> PlatformImage? {
    guard let data = readFileData(path: path) else { return nil }
    return UIImage(data: data)
}
#endif

