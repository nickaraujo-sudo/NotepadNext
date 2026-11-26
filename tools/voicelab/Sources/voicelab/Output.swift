// craftpad++ voicelab — arquivos de saída: transcrição em Markdown (pronta para o
// Obsidian e para o craftpad++), relatório de avaliação e resultado.json.

import Foundation

enum Output {
    static func write(_ result: SessionResult, to folder: URL) throws {
        try jsonEncoder().encode(result).write(to: folder.appendingPathComponent("resultado.json"))
        for engine in result.engines {
            let md = transcript(result, engine: engine)
            try md.write(to: folder.appendingPathComponent("transcricao-\(engine.engine).md"), atomically: true, encoding: .utf8)
        }
        try report(result).write(to: folder.appendingPathComponent("relatorio.md"), atomically: true, encoding: .utf8)
    }

    static func load(from folder: URL) throws -> SessionResult {
        let url = folder.appendingPathComponent("resultado.json")
        guard let data = try? Data(contentsOf: url) else {
            throw VoicelabError.message("Não encontrei \(url.path). Rode primeiro: voicelab analisar \(folder.path)")
        }
        return try jsonDecoder().decode(SessionResult.self, from: data)
    }

    static func transcript(_ result: SessionResult, engine: EngineResult) -> String {
        let names = Dictionary(uniqueKeysWithValues: result.speakers.map { ($0.key, $0.displayName) })
        let channelOf = Dictionary(uniqueKeysWithValues: result.speakers.map { ($0.key, $0.channel) })
        let participants = result.speakers.filter { $0.seconds >= 2 }.map(\.displayName)
        let date = ISO8601DateFormatter.string(from: result.createdAt, timeZone: .current, formatOptions: [.withFullDate])

        var out = """
        ---
        tipo: reuniao
        data: \(date)
        participantes: [\(participants.map { "\"\($0)\"" }.joined(separator: ", "))]
        motor: \(engine.title)
        ---

        # \(result.title)


        """
        let showChannel = result.channels.count > 1
        for t in engine.turns {
            let name = names[t.speakerKey] ?? "?"
            let channel = showChannel ? " _(\(channelOf[t.speakerKey] ?? ""))_" : ""
            out += "**\(timestamp(t.start)) · \(name)**\(channel) — \(t.text)\n\n"
        }
        return out
    }

    static func report(_ result: SessionResult) -> String {
        var out = "# Relatório do teste — \(result.title)\n\n"
        out += String(format: "- Duração do áudio: %.1f min\n", result.audioSeconds / 60)
        out += "- Canais: \(result.channels.joined(separator: " + "))\n"
        out += "- Cancelamento de eco: \(result.echoCancelled ? "sim" : "não")\n"
        out += String(format: "- Separação de falantes: %.1f s\n", result.diarizationSeconds)
        for e in result.engines {
            out += String(format: "- %@: %.1f s (%d falas)\n", e.title, e.processingSeconds, e.turns.count)
        }
        for n in result.notes { out += "- \(n)\n" }

        out += "\n## Falantes\n\n| Rótulo | Canal | Fala | Nome | Semelhança com o banco de vozes |\n|---|---|---|---|---|\n"
        for s in result.speakers {
            let score: String
            if let c = s.bestCandidate, let sc = s.bestCandidateScore {
                score = String(format: "%@ (%.2f)", c, sc)
            } else {
                score = "—"
            }
            out += String(format: "| %@ | %@ | %.0f s | %@ | %@ |\n", s.label, s.channel, s.seconds, s.name ?? "—", score)
        }

        out += """

        ## Como avaliar

        Abra as transcrições (uma por motor) e anote:

        1. **Separação de falantes** — cada interrupção virou uma fala separada? Alguém foi confundido com outra pessoa? Quantas pessoas o programa achou vs. quantas havia?
        2. **Texto** — qual motor errou menos palavras, nomes próprios e números? A pontuação ajuda a leitura?
        3. **Vozes** — depois de `voicelab nomear`, grave outra conversa com as mesmas pessoas e veja se elas são reconhecidas sozinhas.

        """
        return out
    }
}
