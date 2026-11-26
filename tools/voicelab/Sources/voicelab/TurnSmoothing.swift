// NotepadNext voicelab — ajuste das fronteiras entre falantes.
//
// A troca de falante e o tempo das palavras às vezes ficam desencontrados em uma ou duas
// palavras. Usa a pontuação para devolver cada palavra à frase a que ela pertence:
//   "…mudando a alíquota de" | "vigência. Sobe uma…"  →  "…alíquota de vigência." | "Sobe uma…"
//   "…o grupo Vartai. É"     | "o Verdade."           →  "…o grupo Vartai."      | "É o Verdade."

import Foundation

struct WordGroup {
    var key: String
    var words: [Word]
}

enum TurnSmoothing {
    static let maxGap = 1.0          // só mexe quando as falas estão coladas
    static let maxMoved = 3          // no máximo 3 palavras mudam de dono

    static func endsSentence(_ w: Word) -> Bool {
        guard let last = w.text.last else { return false }
        return ".?!…".contains(last)
    }

    static func endsClause(_ w: Word) -> Bool {
        guard let last = w.text.last else { return false }
        return ".?!…,;:".contains(last)
    }

    static func startsLowercase(_ w: Word) -> Bool {
        guard let first = w.text.first(where: { $0.isLetter }) else { return false }
        return first.isLowercase
    }

    static func capitalizeFirst(_ w: Word) -> Word {
        var x = w
        if let i = x.text.firstIndex(where: { $0.isLetter }) {
            x.text.replaceSubrange(i...i, with: String(x.text[i]).uppercased())
        }
        return x
    }

    static func smooth(_ input: [WordGroup]) -> [WordGroup] {
        var groups = input
        var i = 0
        while i + 1 < groups.count {
            let a = groups[i], b = groups[i + 1]
            guard a.key != b.key, let aLast = a.words.last, let bFirst = b.words.first,
                  bFirst.start - aLast.end < maxGap else {
                i += 1
                continue
            }

            // Regra 1: a fala anterior termina com o começo da frase da seguinte
            //   "…o grupo Vartai. É" | "o Verdade."
            if startsLowercase(bFirst), let lastEnd = a.words.lastIndex(where: endsSentence),
               a.words.count - 1 - lastEnd > 0, a.words.count - 1 - lastEnd <= 2 {
                let tail = Array(a.words[(lastEnd + 1)...])
                groups[i].words.removeLast(tail.count)
                var moved = tail
                moved[0] = capitalizeFirst(moved[0])
                groups[i + 1].words.insert(contentsOf: moved, at: 0)
                i += 1
                continue
            }

            // Regra 2: a fala seguinte começa terminando a frase da anterior
            //   "…mudando a alíquota de" | "vigência. Sobe…"
            if !endsSentence(aLast), startsLowercase(bFirst) {
                if let end = b.words.firstIndex(where: endsClause), end < maxMoved, end + 1 < b.words.count {
                    groups[i].words += b.words[0...end]
                    groups[i + 1].words.removeFirst(end + 1)
                    groups[i + 1].words[0] = capitalizeFirst(groups[i + 1].words[0])
                    continue // reavalia o mesmo par
                }
            }
            i += 1
        }

        // Junta falas seguidas da mesma pessoa e tira as vazias
        var out: [WordGroup] = []
        for g in groups where !g.words.isEmpty {
            if let last = out.last, last.key == g.key,
               let lastEnd = last.words.last?.end, let start = g.words.first?.start, start - lastEnd < 2.0 {
                out[out.count - 1].words += g.words
            } else {
                out.append(g)
            }
        }
        return out
    }
}
