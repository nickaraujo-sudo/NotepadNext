// NotepadNext voicelab — testes das partes sem modelo (rodados no CI: voicelab teste-interno).

import Foundation

func selfTest() throws {
    var failures = 0
    func check(_ got: String, _ expected: String, _ what: String) {
        if got != expected {
            say("FALHOU \(what): \"\(got)\" (esperado \"\(expected)\")")
            failures += 1
        } else {
            say("ok \(what)")
        }
    }
    func words(_ text: String, from t0: Double = 0) -> [Word] {
        text.split(separator: " ").enumerated().map { i, w in
            Word(text: String(w), start: t0 + Double(i) * 0.3, end: t0 + Double(i) * 0.3 + 0.25)
        }
    }
    func joined(_ g: WordGroup) -> String { g.words.map(\.text).joined(separator: " ") }

    // Vocabulário
    let v = Vocabulary(terms: ["Illumia", "Overlaps", "Accenture", "PIS/Cofins", "Sky", "Ideias"])
    check(v.correct(words("o que eu sei da Ilumia, eles têm")).map(\.text).joined(separator: " "),
          "o que eu sei da Illumia, eles têm", "nome próprio parecido")
    check(v.correct(words("só o Pisofins que morre logo")).map(\.text).joined(separator: " "),
          "só o PIS/Cofins que morre logo", "sigla com símbolo")
    check(v.correct(words("a Overlabs querendo também")).map(\.text).joined(separator: " "),
          "a Overlaps querendo também", "uma letra trocada")
    check(v.correct(words("tenho umas ideias boas")).map(\.text).joined(separator: " "),
          "tenho umas ideias boas", "palavra comum não muda")
    check(v.correct(words("a sky e a casa")).map(\.text).joined(separator: " "),
          "a sky e a casa", "termo curto em minúscula não muda")
    check(v.correct(words("o Pedro chegou")).map(\.text).joined(separator: " "),
          "o Pedro chegou", "nome diferente não muda")

    // Fronteiras entre falantes
    let r1 = TurnSmoothing.smooth([
        WordGroup(key: "A", words: words("mudando a alíquota de")),
        WordGroup(key: "B", words: words("vigência. Sobe uma alíquota diminui a", from: 1.3)),
        WordGroup(key: "A", words: words("outra, é isso aí.", from: 3.5)),
    ])
    check(r1.map(joined).joined(separator: " | "),
          "mudando a alíquota de vigência. | Sobe uma alíquota diminui a outra, | É isso aí.", "regras 1 e 1")
    let r2 = TurnSmoothing.smooth([
        WordGroup(key: "A", words: words("é o grupo Vartai. É")),
        WordGroup(key: "B", words: words("o Verdade. O Illumia", from: 1.6)),
    ])
    check(r2.map(joined).joined(separator: " | "), "é o grupo Vartai. | É o Verdade. O Illumia", "regra 2")
    let r3 = TurnSmoothing.smooth([
        WordGroup(key: "A", words: words("Bom dia.")),
        WordGroup(key: "B", words: words("Bom dia a todos.", from: 5)),
    ])
    check(r3.map(joined).joined(separator: " | "), "Bom dia. | Bom dia a todos.", "sem mudança quando há pausa")

    say(failures == 0 ? "✅ todos os testes passaram" : "❌ \(failures) teste(s) falharam")
    if failures > 0 { exit(1) }
}
