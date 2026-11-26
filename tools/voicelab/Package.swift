// swift-tools-version: 5.9
// craftpad++ voicelab — protótipo de transcrição de reuniões com separação de falantes.
// Tudo roda no Mac (Neural Engine), via FluidAudio (Apache 2.0).
import PackageDescription

let package = Package(
    name: "voicelab",
    platforms: [.macOS(.v14)],
    dependencies: [
        // Versão fixa para o protótipo ser reproduzível
        .package(url: "https://github.com/FluidInference/FluidAudio.git", revision: "e2ea498dafd6db9b3f18f0f644bc35312b3a7679"),
    ],
    targets: [
        .executableTarget(
            name: "voicelab",
            dependencies: [.product(name: "FluidAudio", package: "FluidAudio")],
            path: "Sources/voicelab"
        ),
    ]
)
