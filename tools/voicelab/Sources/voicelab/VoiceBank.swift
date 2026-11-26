// craftpad++ voicelab — banco de vozes: guarda a assinatura de voz de cada pessoa
// para reconhecê-la nas próximas reuniões, sem precisar dar o nome de novo.

import Foundation

struct VoiceEntry: Codable {
    var name: String
    var embedding: [Float]     // média normalizada das amostras
    var seconds: Double        // quanto áudio já foi usado para aprender
    var updatedAt: Date
}

final class VoiceBank {
    static var defaultURL: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        return base.appendingPathComponent("craftpad", isDirectory: true).appendingPathComponent("vozes.json")
    }

    let url: URL
    private(set) var entries: [VoiceEntry] = []

    init(url: URL = VoiceBank.defaultURL) {
        self.url = url
        if let data = try? Data(contentsOf: url),
           let list = try? jsonDecoder().decode([VoiceEntry].self, from: data) {
            entries = list
        }
    }

    func save() throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try jsonEncoder().encode(entries).write(to: url, options: .atomic)
    }

    /// Aprende (ou reforça) a voz de uma pessoa. Amostras longas pesam mais.
    func learn(name: String, embedding: [Float], seconds: Double) {
        let e = normalized(embedding)
        let weight = max(1, min(seconds, 600))
        if let i = entries.firstIndex(where: { $0.name.caseInsensitiveCompare(name) == .orderedSame }) {
            let old = entries[i]
            let total = old.seconds + weight
            var mixed = [Float](repeating: 0, count: e.count)
            if old.embedding.count == e.count {
                for k in 0..<e.count {
                    mixed[k] = Float((Double(old.embedding[k]) * old.seconds + Double(e[k]) * weight) / total)
                }
            } else {
                mixed = e
            }
            entries[i] = VoiceEntry(name: old.name, embedding: normalized(mixed), seconds: total, updatedAt: Date())
        } else {
            entries.append(VoiceEntry(name: name, embedding: e, seconds: weight, updatedAt: Date()))
        }
    }

    func forget(name: String) {
        entries.removeAll { $0.name.caseInsensitiveCompare(name) == .orderedSame }
    }

    /// Associa falantes a nomes do banco (cada nome no máximo uma vez por canal).
    /// Devolve, por chave de falante, o melhor candidato e a semelhança (0 a 1).
    func match(_ speakers: [SpeakerInfo]) -> [String: (name: String, score: Float)] {
        var best: [String: (name: String, score: Float)] = [:]
        guard !entries.isEmpty else { return best }

        // Todas as combinações, da mais parecida para a menos
        var pairs: [(key: String, channel: String, name: String, score: Float)] = []
        for s in speakers {
            for e in entries {
                pairs.append((s.key, s.channel, e.name, cosine(s.embedding, e.embedding)))
            }
        }
        pairs.sort { $0.score > $1.score }

        var usedPerChannel: [String: Set<String>] = [:]
        for p in pairs {
            if best[p.key] != nil { continue }
            if usedPerChannel[p.channel, default: []].contains(p.name) { continue }
            best[p.key] = (p.name, p.score)
            usedPerChannel[p.channel, default: []].insert(p.name)
        }
        return best
    }
}
