// craftpad++ voicelab — análise: cancelamento de eco, separação de falantes,
// transcrição e reconhecimento das vozes conhecidas.

import FluidAudio
import Foundation

struct AnalyzeOptions {
    var engines = "ambos"
    var maxSpeakers: Int?
    var threshold: Float = 0.62     // semelhança mínima para reconhecer uma voz
    var title: String?
}

private struct Channel {
    let name: String        // "presencial" ou "remoto"
    let prefix: String      // rótulo dos falantes
    var samples: [Float]
}

private struct DiarizedChannel {
    let channel: Channel
    let segments: [TimedSpeakerSegment]
    let speakers: [SpeakerInfo]
}

enum Analyzer {
    static let sampleRate = 16_000.0

    static func analyze(input: URL, options: AnalyzeOptions) async throws -> URL {
        let fm = FileManager.default
        var isDir: ObjCBool = false
        guard fm.fileExists(atPath: input.path, isDirectory: &isDir) else {
            throw VoicelabError.message("Não encontrei \(input.path)")
        }

        // Pasta de saída: a própria pasta da gravação, ou uma pasta ao lado do arquivo
        let folder: URL
        var channels: [Channel] = []
        var notes: [String] = []
        var echoCancelled = false
        let converter = AudioConverter()

        if isDir.boolValue {
            folder = input
            let session = (try? Data(contentsOf: folder.appendingPathComponent("sessao.json")))
                .flatMap { try? jsonDecoder().decode(RecordingSession.self, from: $0) }
            let micURL = folder.appendingPathComponent(session?.micFile ?? "microfone.caf")
            let sysURL = folder.appendingPathComponent(session?.systemFile ?? "sistema.caf")

            var mic: [Float] = fm.fileExists(atPath: micURL.path) ? try converter.resampleAudioFile(micURL) : []
            var sys: [Float] = fm.fileExists(atPath: sysURL.path) ? try converter.resampleAudioFile(sysURL) : []

            // Alinha os dois canais na mesma linha do tempo
            if let m = session?.micStartHost, let s = session?.systemStartHost, !mic.isEmpty, !sys.isEmpty {
                let offset = Int(((m - s) * sampleRate).rounded())
                if offset > 0 { mic.insert(contentsOf: [Float](repeating: 0, count: offset), at: 0) }
                if offset < 0 { sys.insert(contentsOf: [Float](repeating: 0, count: -offset), at: 0) }
                notes.append(String(format: "Canais alinhados (diferença de início: %.3f s).", m - s))
            }

            let systemActive = sys.count > Int(sampleRate) && rms(sys) > 0.002
            if !sys.isEmpty && !systemActive {
                notes.append("Som do computador praticamente mudo: tratado como reunião presencial.")
            }

            if !mic.isEmpty {
                if systemActive {
                    // O microfone também capta o que sai dos alto-falantes: remove esse eco
                    say("🔇 Removendo o eco do som do computador no microfone...")
                    let started = Date()
                    let vqe = try await LocalVqeManager()
                    var reference = sys
                    if reference.count < mic.count {
                        reference.append(contentsOf: [Float](repeating: 0, count: mic.count - reference.count))
                    } else if reference.count > mic.count {
                        reference.removeLast(reference.count - mic.count)
                    }
                    mic = try await vqe.process(mic: mic, reference: reference)
                    echoCancelled = true
                    notes.append(String(format: "Cancelamento de eco aplicado em %.1f s.", Date().timeIntervalSince(started)))
                }
                channels.append(Channel(name: "presencial", prefix: "Pessoa", samples: mic))
            }
            if systemActive {
                channels.append(Channel(name: "remoto", prefix: "Remoto", samples: sys))
            }
        } else {
            // Arquivo avulso (ex.: gravação do Plaud): um canal só
            let base = input.deletingPathExtension().lastPathComponent
            folder = input.deletingLastPathComponent().appendingPathComponent(base + "-voicelab", isDirectory: true)
            try fm.createDirectory(at: folder, withIntermediateDirectories: true)
            channels.append(Channel(name: "presencial", prefix: "Pessoa", samples: try converter.resampleAudioFile(input)))
            notes.append("Arquivo único: todos os falantes estão no mesmo canal.")
        }

        channels.removeAll { $0.samples.count < Int(sampleRate) }
        guard !channels.isEmpty else {
            throw VoicelabError.message("Não há áudio suficiente para analisar.")
        }
        let audioSeconds = channels.map { Double($0.samples.count) / sampleRate }.max() ?? 0
        say(String(format: "🎧 %.1f min de áudio em %d canal(is): %@", audioSeconds / 60, channels.count,
                   channels.map(\.name).joined(separator: " + ")))

        // 1. Quem falou quando (separação de falantes)
        say("👥 Separando os falantes...")
        let diarStart = Date()
        var config = OfflineDiarizerConfig(segmentationStepRatio: 0.1) // passos curtos: pega interrupções rápidas
        if let limit = options.maxSpeakers {
            config = config.withSpeakers(min: 1, max: limit)
        }
        let diarizer = OfflineDiarizerManager(config: config)
        try await diarizer.prepareModels()

        var diarized: [DiarizedChannel] = []
        for channel in channels {
            let result = try await diarizer.process(audio: channel.samples)
            diarized.append(DiarizedChannel(channel: channel, segments: result.segments,
                                            speakers: speakers(for: channel, result: result)))
        }
        let diarizationSeconds = Date().timeIntervalSince(diarStart)

        // 2. Vozes conhecidas
        var allSpeakers = diarized.flatMap(\.speakers)
        let bank = VoiceBank()
        let matches = bank.match(allSpeakers)
        for i in allSpeakers.indices {
            guard let m = matches[allSpeakers[i].key] else { continue }
            allSpeakers[i].bestCandidate = m.name
            allSpeakers[i].bestCandidateScore = m.score
            if m.score >= options.threshold {
                allSpeakers[i].name = m.name
                allSpeakers[i].recognizedScore = m.score
            }
        }
        for s in allSpeakers {
            let status: String
            if let n = s.name, let sc = s.recognizedScore {
                status = String(format: "reconhecido: %@ (%.2f)", n, sc)
            } else if let c = s.bestCandidate, let sc = s.bestCandidateScore {
                status = String(format: "desconhecido (mais parecido: %@, %.2f)", c, sc)
            } else {
                status = "desconhecido"
            }
            say(String(format: "   %@ [%@] %.0f s de fala — %@", s.label, s.channel, s.seconds, status))
        }

        // 3. Transcrição com cada motor
        var engineResults: [EngineResult] = []
        for engine in try makeEngines(options.engines) {
            say("📝 Transcrevendo com \(engine.title)...")
            let start = Date()
            try await engine.prepare()
            var turns: [Turn] = []
            for d in diarized {
                let w = try await engine.transcribe(d.channel.samples)
                turns += buildTurns(words: w, segments: d.segments, channel: d.channel)
            }
            turns.sort { $0.start < $1.start }
            let elapsed = Date().timeIntervalSince(start)
            engineResults.append(EngineResult(engine: engine.id, title: engine.title, processingSeconds: elapsed, turns: turns))
            say(String(format: "   %d falas em %.1f s (%.0fx mais rápido que o tempo real)", turns.count, elapsed,
                       audioSeconds / max(elapsed, 0.001)))
        }

        let title = options.title ?? defaultTitle(for: folder)
        let result = SessionResult(
            title: title,
            createdAt: Date(),
            audioSeconds: audioSeconds,
            channels: channels.map(\.name),
            echoCancelled: echoCancelled,
            diarizationSeconds: diarizationSeconds,
            speakers: allSpeakers,
            engines: engineResults,
            notes: notes
        )
        try Output.write(result, to: folder)
        return folder
    }

    private static func speakers(for channel: Channel, result: DiarizationResult) -> [SpeakerInfo] {
        var seconds: [String: Double] = [:]
        var embeddingSum: [String: [Float]] = [:]
        for seg in result.segments {
            seconds[seg.speakerId, default: 0] += Double(seg.durationSeconds)
            if !seg.embedding.isEmpty {
                let e = normalized(seg.embedding)
                if var sum = embeddingSum[seg.speakerId], sum.count == e.count {
                    for k in 0..<e.count { sum[k] += e[k] * seg.durationSeconds }
                    embeddingSum[seg.speakerId] = sum
                } else {
                    embeddingSum[seg.speakerId] = e.map { $0 * seg.durationSeconds }
                }
            }
        }
        // Mais tempo de fala primeiro: "Pessoa 1" é quem mais falou
        let ids = seconds.keys.sorted { seconds[$0, default: 0] > seconds[$1, default: 0] }
        return ids.enumerated().map { index, id in
            let embedding = result.speakerDatabase?[id] ?? embeddingSum[id] ?? []
            return SpeakerInfo(
                key: "\(channel.name):\(id)",
                channel: channel.name,
                label: "\(channel.prefix) \(index + 1)",
                name: nil,
                recognizedScore: nil,
                bestCandidate: nil,
                bestCandidateScore: nil,
                seconds: seconds[id, default: 0],
                embedding: normalized(embedding)
            )
        }
    }

    /// Junta palavras em falas, usando os trechos de cada falante.
    /// Numa interrupção, os trechos se sobrepõem: a palavra fica com o trecho mais curto
    /// que a contém (normalmente é quem interrompeu).
    private static func buildTurns(words: [Word], segments: [TimedSpeakerSegment], channel: Channel) -> [Turn] {
        guard !words.isEmpty else { return [] }
        var turns: [Turn] = []
        var lastSpeaker: String?

        for w in words {
            let mid = (w.start + w.end) / 2
            var speaker: String?
            var bestLength = Double.greatestFiniteMagnitude
            for seg in segments {
                let s = Double(seg.startTimeSeconds), e = Double(seg.endTimeSeconds)
                if mid >= s && mid <= e && (e - s) < bestLength {
                    bestLength = e - s
                    speaker = seg.speakerId
                }
            }
            if speaker == nil {
                // Fora de qualquer trecho: o mais próximo (até 1 s), senão quem falava antes
                var bestDistance = 1.0
                for seg in segments {
                    let d = min(abs(Double(seg.startTimeSeconds) - mid), abs(Double(seg.endTimeSeconds) - mid))
                    if d < bestDistance {
                        bestDistance = d
                        speaker = seg.speakerId
                    }
                }
            }
            let key = "\(channel.name):\(speaker ?? lastSpeaker ?? "?")"

            if var last = turns.last, last.speakerKey == key, w.start - last.end < 2.0 {
                last.text += " " + w.text
                last.end = w.end
                turns[turns.count - 1] = last
            } else {
                turns.append(Turn(speakerKey: key, start: w.start, end: w.end, text: w.text))
            }
            lastSpeaker = speaker ?? lastSpeaker
        }
        return turns
    }

    static func defaultTitle(for folder: URL) -> String {
        let f = DateFormatter()
        f.locale = Locale(identifier: "pt_BR")
        f.dateFormat = "dd-MM HH'h'mm"
        return "\(f.string(from: Date())) Reunião"
    }
}
