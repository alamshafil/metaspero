// swift-tools-version: 6.0
// SPDX-License-Identifier: GPL-2.0-or-later
import PackageDescription

let package = Package(
    name: "MetasperoUI",
    defaultLocalization: "en",
    platforms: [.iOS(.v16), .macOS(.v13)],
    products: [
        .library(name: "MetasperoUI", type: .dynamic, targets: ["MetasperoUI"]),
    ],
    dependencies: [
        .package(url: "https://github.com/skiptools/skip.git", from: "1.0.0"),
        .package(url: "https://github.com/skiptools/skip-ui.git", from: "1.0.0"),
    ],
    targets: [
        .target(
            name: "MetasperoUI",
            dependencies: [
                .product(name: "SkipUI", package: "skip-ui"),
            ],
            plugins: [
                .plugin(name: "skipstone", package: "skip"),
            ]
        ),
    ]
)
