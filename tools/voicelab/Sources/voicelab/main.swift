// craftpad++ voicelab — protótipo de teste do Notetaker / Meeting Assistant.
//
//   voicelab gravar [pasta] [--sem-sistema] [--sem-analise] [opções de análise]
//   voicelab analisar <pasta-ou-arquivo> [--motor nemotron|parakeet|ambos] [--max-falantes N] [--limiar 0.62] [--titulo "..."]
//   voicelab nomear <pasta>
//   voicelab vozes [--esquecer Nome]
//
// Tudo roda neste Mac. Os modelos são baixados uma vez na primeira execução.

import Foundation

let usage = """
    craftpad++ voicelab — transcrição de reuniões com separação de falantes (protótipo)

    Opção geral: --json  (eventos em JSON, usado pelo craftpad++)

    Comandos:
      gravar [pasta]           Grava microfone + som do computador até você apertar Enter,
                               depois analisa. Sem pasta, cria ~/craftpad-reunioes/AAAA-MM-DD_HHhMM.
          --sem-sistema        Só o microfone (reunião presencial; não pede permissão de tela)
          --sem-analise        Só grava
          --sem-ao-vivo        Não mostra o rascunho da transcrição durante a gravação

      analisar <pasta|arquivo> Analisa uma gravação (pasta do 'gravar' ou um arquivo de áudio,
                               ex.: .m4a do Plaud, .wav, .mp3)
          --motor X            parakeet, nemotron ou ambos (padrão: parakeet)
          --vocabulario arq    Lista de termos (um por linha) para corrigir nomes e siglas
          --max-falantes N     Limite de pessoas, se você souber
          --limiar 0.62        Semelhança mínima para reconhecer uma voz do banco (0 a 1)
          --titulo "texto"     Título da transcrição

      nomear <pasta>           Mostra trechos de cada falante e pergunta o nome.
                               A voz fica guardada para as próximas reuniões.
          --definir "presencial:S1=Ana"  Define nomes sem perguntar (pode repetir)

      vozes                    Lista as vozes guardadas
          --esquecer "Nome"    Apaga uma voz do banco

    """

func value(after flag: String, in args: [String]) -> String? {
    guard let i = args.firstIndex(of: flag), i + 1 < args.count else { return nil }
    return args[i + 1]
}

func positional(_ args: [String]) -> [String] {
    let valued: Set<String> = ["--motor", "--max-falantes", "--limiar", "--titulo", "--esquecer", "--vocabulario", "--definir"]
    var out: [String] = []
    var i = 0
    while i < args.count {
        if valued.contains(args[i]) { i += 2; continue }
        if args[i].hasPrefix("--") { i += 1; continue }
        out.append(args[i])
        i += 1
    }
    return out
}

func analyzeOptions(_ args: [String]) throws -> AnalyzeOptions {
    var o = AnalyzeOptions()
    if let m = value(after: "--motor", in: args) { o.engines = m }
    if let n = value(after: "--max-falantes", in: args) {
        guard let v = Int(n), v > 0 else { throw VoicelabError.message("--max-falantes precisa de um número") }
        o.maxSpeakers = v
    }
    if let l = value(after: "--limiar", in: args) {
        guard let v = Float(l.replacingOccurrences(of: ",", with: ".")), v > 0, v < 1 else {
            throw VoicelabError.message("--limiar precisa ser entre 0 e 1")
        }
        o.threshold = v
    }
    o.title = value(after: "--titulo", in: args)
    if let v = value(after: "--vocabulario", in: args) {
        let vocab = try Vocabulary(file: expand(v))
        o.vocabulary = vocab.isEmpty ? nil : vocab
        say("📚 Vocabulário: \(vocab.terms.count) termos")
    }
    return o
}

func expand(_ path: String) -> URL {
    URL(fileURLWithPath: (path as NSString).expandingTildeInPath).standardizedFileURL
}

func defaultRecordingFolder() -> URL {
    let f = DateFormatter()
    f.dateFormat = "yyyy-MM-dd_HH'h'mm"
    return FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("craftpad-reunioes", isDirectory: true)
        .appendingPathComponent(f.string(from: Date()), isDirectory: true)
}

func finish(_ folder: URL) {
    say("\n✅ Pronto. Arquivos em \(folder.path):")
    let files = (try? FileManager.default.contentsOfDirectory(atPath: folder.path))?.sorted() ?? []
    for f in files where f.hasSuffix(".md") { say("   • \(f)") }
    say("\nPróximo passo: voicelab nomear \"\(folder.path)\"")
}

// MARK: - Nomear

func nameSpeakers(folder: URL, preset: [String: String]) throws {
    var result = try Output.load(from: folder)
    let bank = VoiceBank()
    let engine = result.engines.first

    // Modo sem perguntas (usado pelo craftpad++): chave=Nome; nome vazio = não guardar
    if !preset.isEmpty {
        for i in result.speakers.indices {
            guard let name = preset[result.speakers[i].key] else { continue }
            if name.isEmpty || name == "-" {
                result.speakers[i].name = nil
                continue
            }
            result.speakers[i].name = name
            bank.learn(name: name, embedding: result.speakers[i].embedding, seconds: result.speakers[i].seconds)
        }
        try bank.save()
        try Output.write(result, to: folder)
        Events.emit("nomes", ["pasta": folder.path])
        say("✅ Nomes aplicados; \(bank.entries.count) voz(es) no banco.")
        return
    }

    say("Para cada falante: digite o nome e Enter.")
    say("  Enter vazio = confirma o nome reconhecido (ou pula, se não houver)")
    say("  '-' = não guardar esta voz (ex.: ruído ou duas pessoas misturadas)\n")

    let order = result.speakers.indices.sorted { result.speakers[$0].seconds > result.speakers[$1].seconds }
    for i in order {
        let s = result.speakers[i]
        guard s.seconds >= 1 else { continue }
        var header = String(format: "── %@ (%@, %.0f s de fala)", s.label, s.channel, s.seconds)
        if let n = s.name { header += "  → reconhecido como \(n)" }
        say(header)
        if let engine {
            let samples = engine.turns.filter { $0.speakerKey == s.key }
                .sorted { $0.text.count > $1.text.count }
                .prefix(3)
            for t in samples {
                let text = t.text.count > 160 ? String(t.text.prefix(160)) + "…" : t.text
                say("   [\(timestamp(t.start))] \(text)")
            }
        }
        FileHandle.standardError.write("   Nome: ".data(using: .utf8)!)
        let answer = (readLine() ?? "").trimmingCharacters(in: .whitespaces)

        if answer == "-" {
            result.speakers[i].name = nil
            continue
        }
        let name = answer.isEmpty ? s.name : answer
        guard let name, !name.isEmpty else { continue }
        result.speakers[i].name = name
        bank.learn(name: name, embedding: s.embedding, seconds: s.seconds)
    }

    try bank.save()
    try Output.write(result, to: folder)
    say("\n✅ Nomes aplicados às transcrições e \(bank.entries.count) voz(es) no banco: \(bank.url.path)")
}

// MARK: - Execução

let args = Array(CommandLine.arguments.dropFirst())
Events.enabled = args.contains("--json")
let command = args.first ?? "ajuda"
let rest = Array(args.dropFirst())

do {
    switch command {
    case "gravar":
        let options = try analyzeOptions(rest)
        let folder = positional(rest).first.map(expand) ?? defaultRecordingFolder()
        _ = try await Recorder.record(into: folder, captureSystem: !rest.contains("--sem-sistema"),
                                      live: !rest.contains("--sem-ao-vivo"), vocabulary: options.vocabulary)
        if !rest.contains("--sem-analise") {
            _ = try await Analyzer.analyze(input: folder, options: options)
            finish(folder)
        } else {
            say("Para analisar depois: voicelab analisar \"\(folder.path)\"")
        }

    case "analisar":
        guard let path = positional(rest).first else { throw VoicelabError.message("Informe a pasta ou o arquivo de áudio.") }
        let folder = try await Analyzer.analyze(input: expand(path), options: try analyzeOptions(rest))
        finish(folder)

    case "nomear":
        guard let path = positional(rest).first else { throw VoicelabError.message("Informe a pasta da reunião.") }
        var folder = expand(path)
        // Aceita também o arquivo de áudio original
        var isDir: ObjCBool = false
        if FileManager.default.fileExists(atPath: folder.path, isDirectory: &isDir), !isDir.boolValue {
            folder = folder.deletingLastPathComponent()
                .appendingPathComponent(folder.deletingPathExtension().lastPathComponent + "-voicelab")
        }
        var preset: [String: String] = [:]
        var k = 0
        while k < rest.count {
            if rest[k] == "--definir", k + 1 < rest.count {
                let pair = rest[k + 1]
                if let eq = pair.firstIndex(of: "=") {
                    preset[String(pair[..<eq])] = String(pair[pair.index(after: eq)...]).trimmingCharacters(in: .whitespaces)
                }
                k += 2
            } else {
                k += 1
            }
        }
        try nameSpeakers(folder: folder, preset: preset)

    case "vozes":
        let bank = VoiceBank()
        if let name = value(after: "--esquecer", in: rest) {
            bank.forget(name: name)
            try bank.save()
            say("Voz de \(name) apagada.")
        }
        if bank.entries.isEmpty {
            say("Nenhuma voz guardada ainda. Use: voicelab nomear <pasta>")
        } else {
            let f = DateFormatter()
            f.dateFormat = "dd/MM/yyyy"
            for e in bank.entries.sorted(by: { $0.name < $1.name }) {
                say(String(format: "  %@  — %.0f s de áudio aprendido, atualizado em %@", e.name, e.seconds, f.string(from: e.updatedAt)))
            }
            say("Arquivo: \(bank.url.path)")
        }

    case "teste-interno":
        try selfTest()

    default:
        print(usage)
    }
} catch {
    Events.emit("erro", ["texto": error.localizedDescription])
    say("❌ \(error.localizedDescription)")
    exit(1)
}
