// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

#if os(iOS)
import UIKit

class AppDelegate: NSObject, UIApplicationDelegate {
    static var orientationLock: UIInterfaceOrientationMask = .allButUpsideDown
    
    func application(_ application: UIApplication, supportedInterfaceOrientationsFor window: UIWindow?) -> UIInterfaceOrientationMask {
        return AppDelegate.orientationLock
    }
}

enum OrientationManager {
    static func lockLandscape() {
        AppDelegate.orientationLock = .landscape
        if #available(iOS 16.0, *) {
            if let windowScene = UIApplication.shared.connectedScenes
                .compactMap({ $0 as? UIWindowScene })
                .first(where: { $0.activationState == .foregroundActive }) ??
                UIApplication.shared.connectedScenes.compactMap({ $0 as? UIWindowScene }).first {
                windowScene.requestGeometryUpdate(.iOS(interfaceOrientations: .landscapeRight)) { error in
                    NSLog("[OrientationManager] Landscape request error: %@", error.localizedDescription)
                }
            }
        } else {
            UIDevice.current.setValue(UIInterfaceOrientation.landscapeRight.rawValue, forKey: "orientation")
            UIViewController.attemptRotationToDeviceOrientation()
        }
    }
    
    static func unlockOrientation() {
        AppDelegate.orientationLock = .allButUpsideDown
        UIViewController.attemptRotationToDeviceOrientation()
        if #available(iOS 16.0, *) {
            if let windowScene = UIApplication.shared.connectedScenes
                .compactMap({ $0 as? UIWindowScene })
                .first(where: { $0.activationState == .foregroundActive }) ??
                UIApplication.shared.connectedScenes.compactMap({ $0 as? UIWindowScene }).first {
                windowScene.requestGeometryUpdate(.iOS(interfaceOrientations: .portrait)) { error in
                    NSLog("[OrientationManager] Portrait request error: %@", error.localizedDescription)
                }
            }
        } else {
            UIDevice.current.setValue(UIInterfaceOrientation.portrait.rawValue, forKey: "orientation")
            UIViewController.attemptRotationToDeviceOrientation()
        }
    }
}

@main
struct KytyApp: App {
    @UIApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}
#elseif os(macOS)
import AppKit

enum OrientationManager {
    static func lockLandscape() {}
    static func unlockOrientation() {}
}

@main
struct KytyApp: App {
    init() {
        NSApplication.shared.applicationIconImage = NSWorkspace.shared.icon(forFile: Bundle.main.bundlePath)
    }
    
    var body: some Scene {
        WindowGroup {
            ContentView()
                .frame(minWidth: 960, minHeight: 640)
        }
    }
}
#endif
