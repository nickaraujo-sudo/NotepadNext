// NotepadNext voicelab — tipos do resultado de uma reunião.

import Foundation

/// Uma palavra reconhecida, com tempo no áudio (segundos, linha do tempo da reunião).
struct Word: Codable {
    var text: String
    var start: Double
    var end: Double
}

/// Uma fala: trecho contínuo de uma pessoa.
struct Turn: Codable {
    var speakerKey: String
    var start: Double
    var end: Double
    var text: String
}

/// Um falante encontrado num canal (presencial = microfone, remoto = som do computador).
struct SpeakerInfo: Codable {
    var key: String           // "presencial:S1"
    var channel: String       // "presencial" ou "remoto"
    var label: String         // "Pessoa 1" / "Remoto 1"
    var name: String?         // nome confirmado ou reconhecido
    var recognizedScore: Float?
    var bestCandidate: String?
    var bestCandidateScore: Float?
    var seconds: Double       // tempo total de fala
    var embedding: [Float]    // assinatura de voz (normalizada)

    var displayName: String { name ?? label }
}

/// Transcrição feita por um motor de reconhecimento de fala.
struct EngineResult: Codable {
    var engine: String        // "nemotron" / "parakeet"
    var title: String
    var processingSeconds: Double
    var turns: [Turn]
}

/// Tudo o que a análise produziu; salvo como resultado.json.
struct SessionResult: Codable {
    var title: String
    var createdAt: Date
    var audioSeconds: Double
    var channels: [String]
    var echoCancelled: Bool
    var diarizationSeconds: Double
    var speakers: [SpeakerInfo]
    var engines: [EngineResult]
    var notes: [String]
}

/// Dados da gravação (sessao.json), para alinhar os dois canais.
struct RecordingSession: Codable {
    var startedAt: Date
    var micFile: String?
    var micStartHost: Double?
    var systemFile: String?
    var systemStartHost: Double?
}

enum VoicelabError: LocalizedError {
    case message(String)
    var errorDescription: String? {
        switch self {
        case .message(let m): return m
        }
    }
}

// MARK: - Utilidades

func timestamp(_ seconds: Double) -> String {
    let s = max(0, Int(seconds.rounded(.down)))
    return String(format: "%02d:%02d:%02d", s / 3600, (s % 3600) / 60, s % 60)
}

func normalized(_ v: [Float]) -> [Float] {
    var sum: Float = 0
    for x in v { sum += x * x }
    let norm = sum.squareRoot()
    guard norm > 0 else { return v }
    return v.map { $0 / norm }
}

func cosine(_ a: [Float], _ b: [Float]) -> Float {
    guard a.count == b.count, !a.isEmpty else { return -1 }
    var dot: Float = 0, na: Float = 0, nb: Float = 0
    for i in 0..<a.count {
        dot += a[i] * b[i]
        na += a[i] * a[i]
        nb += b[i] * b[i]
    }
    guard na > 0, nb > 0 else { return -1 }
    return dot / (na.squareRoot() * nb.squareRoot())
}

func rms(_ samples: [Float]) -> Float {
    guard !samples.isEmpty else { return 0 }
    var acc: Float = 0
    for s in samples { acc += s * s }
    return (acc / Float(samples.count)).squareRoot()
}

func jsonEncoder() -> JSONEncoder {
    let e = JSONEncoder()
    e.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
    e.dateEncodingStrategy = .iso8601
    return e
}

func jsonDecoder() -> JSONDecoder {
    let d = JSONDecoder()
    d.dateDecodingStrategy = .iso8601
    return d
}

func say(_ message: String) {
    FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
}
