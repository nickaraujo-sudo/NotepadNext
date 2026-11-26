// craftpad++ voicelab — eventos em JSON (uma linha cada) para o craftpad++ acompanhar
// a gravação e a análise. Ativado com --json; sem ele, a saída é só para pessoas.

import Foundation

enum Events {
    static var enabled = false
    private static let lock = NSLock()

    static func emit(_ event: String, _ fields: [String: Any] = [:]) {
        guard enabled else { return }
        var payload = fields
        payload["evento"] = event
        guard JSONSerialization.isValidJSONObject(payload),
              let data = try? JSONSerialization.data(withJSONObject: payload, options: [.sortedKeys]) else { return }
        lock.lock()
        FileHandle.standardOutput.write(data)
        FileHandle.standardOutput.write("\n".data(using: .utf8)!)
        lock.unlock()
    }

    /// Etapa da análise (mostrada na barra de status do app)
    static func stage(_ text: String) {
        say(text)
        emit("etapa", ["texto": text])
    }
}
