// craftpad++ voicelab — correção de nomes próprios e siglas a partir de uma lista de termos
// (o craftpad++ gera essa lista com os títulos e tags do vault).
//
// A correção é feita no texto, de forma conservadora: só troca palavras muito parecidas com
// um termo da lista e, fora correspondências exatas, só palavras com maiúscula (nomes próprios).

import Foundation

final class Vocabulary {
    struct Term {
        let text: String        // como deve aparecer
        let key: String         // normalizado, sem espaços
        let wordCount: Int

        /// Sigla, nome com maiúscula no meio ou com símbolos (ex.: "PIS/Cofins", "OCI", "iPhone")
        var isSpecial: Bool {
            text.dropFirst().contains(where: { $0.isUppercase }) || text.contains(where: { !$0.isLetter && $0 != " " })
        }
    }

    private(set) var terms: [Term] = []

    init(terms raw: [String]) {
        var seen = Set<String>()
        for line in raw {
            let t = line.trimmingCharacters(in: .whitespaces)
            guard !t.isEmpty, !t.hasPrefix("#") else { continue }
            let key = Vocabulary.normalize(t)
            guard key.count >= 3, !seen.contains(key) else { continue }
            seen.insert(key)
            terms.append(Term(text: t, key: key, wordCount: t.split(separator: " ").count))
        }
    }

    convenience init(file: URL) throws {
        let text = try String(contentsOf: file, encoding: .utf8)
        self.init(terms: text.components(separatedBy: .newlines))
    }

    var isEmpty: Bool { terms.isEmpty }

    /// minúsculas, sem acentos, só letras e números
    static func normalize(_ s: String) -> String {
        let folded = s.folding(options: [.diacriticInsensitive, .caseInsensitive], locale: Locale(identifier: "pt_BR"))
        return String(folded.unicodeScalars.filter { CharacterSet.alphanumerics.contains($0) }.map(Character.init))
    }

    static func similarity(_ a: String, _ b: String) -> Double {
        let x = Array(a), y = Array(b)
        if x.isEmpty || y.isEmpty { return 0 }
        var prev = Array(0...y.count)
        var cur = [Int](repeating: 0, count: y.count + 1)
        for i in 1...x.count {
            cur[0] = i
            for j in 1...y.count {
                cur[j] = min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (x[i - 1] == y[j - 1] ? 0 : 1))
            }
            swap(&prev, &cur)
        }
        return 1 - Double(prev[y.count]) / Double(max(x.count, y.count))
    }

    private static func split(_ word: String) -> (lead: String, core: String, trail: String) {
        let chars = Array(word)
        var a = 0, z = chars.count
        while a < z, !chars[a].isLetter, !chars[a].isNumber { a += 1 }
        while z > a, !chars[z - 1].isLetter, !chars[z - 1].isNumber { z -= 1 }
        return (String(chars[0..<a]), String(chars[a..<z]), String(chars[z..<chars.count]))
    }

    /// Corrige as palavras (mantém os tempos; junta palavras quando um termo tem várias).
    func correct(_ words: [Word]) -> [Word] {
        guard !terms.isEmpty, !words.isEmpty else { return words }
        var out: [Word] = []
        var i = 0
        while i < words.count {
            var replaced = false
            // Tenta juntar 3, 2 e 1 palavras
            for n in stride(from: min(3, words.count - i), through: 1, by: -1) {
                let window = Array(words[i..<(i + n)])
                let parts = window.map { Vocabulary.split($0.text) }
                // Não atravessa pontuação no meio do trecho
                if n > 1, parts.dropLast().contains(where: { !$0.trail.isEmpty }) { continue }
                let core = parts.map { $0.core }.joined()
                let key = Vocabulary.normalize(core)
                guard key.count >= 3 else { continue }
                let capitalized = parts[0].core.first?.isUppercase ?? false

                var bestIndex = -1
                var bestScore = 0.0
                for (index, term) in terms.enumerated() {
                    // Palavras curtas só com correspondência exata
                    let exact = term.key == key
                    // Palavra comum em minúscula igual a um termo simples (ex.: "ideias"): deixa como está
                    if exact && !capitalized && !term.isSpecial { continue }
                    if !exact {
                        guard key.count >= 4, capitalized, abs(term.key.count - key.count) <= 2 else { continue }
                    }
                    let score = exact ? 1.0 : Vocabulary.similarity(key, term.key)
                    if score >= (n > 1 ? 0.86 : 0.8), score > bestScore {
                        bestIndex = index
                        bestScore = score
                    }
                }
                guard bestIndex >= 0 else { continue }
                let term = terms[bestIndex]
                // Juntar várias palavras só se o termo também tiver mais de uma, ou se for exato
                if n > 1 && term.wordCount == 1 && bestScore < 0.95 { continue }
                out.append(Word(text: parts[0].lead + term.text + parts[n - 1].trail,
                                start: window[0].start, end: window[n - 1].end))
                i += n
                replaced = true
                break
            }
            if !replaced {
                out.append(words[i])
                i += 1
            }
        }
        return out
    }
}
