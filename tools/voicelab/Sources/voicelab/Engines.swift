// craftpad++ voicelab — motores de reconhecimento de fala (português), todos locais.

import FluidAudio
import Foundation

protocol SpeechEngine: AnyObject {
    var id: String { get }
    var title: String { get }
    func prepare() async throws
    /// Áudio 16 kHz mono → palavras com tempo
    func transcribe(_ samples: [Float]) async throws -> [Word]
}

func words(from timings: [WordTiming]) -> [Word] {
    timings.compactMap { t in
        let text = t.word.trimmingCharacters(in: .whitespacesAndNewlines)
        return text.isEmpty ? nil : Word(text: text, start: t.startTime, end: t.endTime)
    }
}

/// NVIDIA Nemotron Unified 0.6B com o idioma fixo em português: pontuação e maiúsculas.
final class NemotronEngine: SpeechEngine {
    let id = "nemotron"
    let title = "Nemotron Unified 0.6B (pt)"
    private let manager = UnifiedAsrManager(
        config: UnifiedConfig(family: .nemotronMultilingual(.pt)),
        encoderPrecision: .fp16)
    private var loaded = false

    func prepare() async throws {
        if !loaded {
            try await manager.loadModels()
            loaded = true
        }
    }

    func transcribe(_ samples: [Float]) async throws -> [Word] {
        try await prepare()
        let result = try await manager.transcribeWithTimings(samples)
        return words(from: buildWordTimings(from: result.tokenTimings))
    }
}

/// Parakeet Ultra (pós-treino do Parakeet TDT v3), com filtro de alfabeto para português.
final class ParakeetEngine: SpeechEngine {
    let id = "parakeet"
    let title = "Parakeet Ultra (v3, 25 línguas)"
    private let manager = AsrManager(config: .default)
    private var loaded = false

    func prepare() async throws {
        if !loaded {
            let models = try await AsrModels.downloadAndLoad(version: .ultra)
            try await manager.loadModels(models)
            loaded = true
        }
    }

    func transcribe(_ samples: [Float]) async throws -> [Word] {
        try await prepare()
        var state = TdtDecoderState.make(decoderLayers: await manager.decoderLayerCount)
        let result = try await manager.transcribe(samples, decoderState: &state, language: .portuguese)
        return words(from: buildWordTimings(from: result.tokenTimings ?? []))
    }
}

func makeEngines(_ spec: String) throws -> [SpeechEngine] {
    switch spec.lowercased() {
    case "nemotron": return [NemotronEngine()]
    case "parakeet": return [ParakeetEngine()]
    case "ambos", "todos", "both": return [ParakeetEngine(), NemotronEngine()]
    default: throw VoicelabError.message("Motor desconhecido: \(spec). Use nemotron, parakeet ou ambos.")
    }
}
