// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "VmpLoaderIOS",
    platforms: [.iOS(.v15)],
    products: [
        .library(name: "VmpLoaderIOS", targets: ["VmpLoaderIOS"]),
    ],
    targets: [
        .target(
            name: "VmpLoaderIOS",
            path: ".",
            exclude: ["README.md"],
            sources: [],
            linkerSettings: [
                .unsafeFlags(["-lvmp_loader_ios"])
            ]
        )
    ]
)

// Placeholder manifest: when the CMake iOS build is active, the consuming Xcode project
// should point the linker search path at the generated static archive (libvmp_loader_ios.a).
