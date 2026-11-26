// craftpad++ voicelab — gravação sem bot: microfone e som do computador em canais separados.
//
// - Microfone: AVAudioEngine (quem está na sala, inclusive você).
// - Som do computador: ScreenCaptureKit (Zoom, Teams, Meet, Slack...), sem driver virtual
//   e sem entrar na reunião. Exige a permissão "Gravação de Tela e Áudio do Sistema" para o Terminal.

import AVFoundation
import CoreMedia
import Foundation
import FluidAudio
import ScreenCaptureKit

/// Escreve um canal em disco e guarda o instante (relógio do sistema) do primeiro trecho.
final class TrackWriter {
    let url: URL
    private var file: AVAudioFile?
    private(set) var firstHostSeconds: Double?
    private(set) var frames: Int64 = 0
    private(set) var sampleRate: Double = 0
    private(set) var peak: Float = 0
    private(set) var failure: String?
    private let lock = NSLock()
    // Áudio ainda não transcrito ao vivo (mono, na taxa original)
    private var pending: [Float] = []
    private var pendingStartFrame: Int64 = 0
    var keepPending = false

    init(url: URL) {
        self.url = url
    }

    func write(_ buffer: AVAudioPCMBuffer, hostSeconds: Double) {
        lock.lock()
        defer { lock.unlock() }
        guard buffer.frameLength > 0 else { return }
        if firstHostSeconds == nil { firstHostSeconds = hostSeconds }
        do {
            if file == nil {
                let format = buffer.format
                file = try AVAudioFile(forWriting: url, settings: format.settings,
                                       commonFormat: format.commonFormat, interleaved: format.isInterleaved)
                sampleRate = format.sampleRate
            }
            try file?.write(from: buffer)
            if let data = buffer.floatChannelData {
                let n = Int(buffer.frameLength)
                for i in stride(from: 0, to: n, by: 64) {
                    peak = max(peak, abs(data[0][i]))
                }
                if keepPending {
                    if pending.isEmpty { pendingStartFrame = frames }
                    let channels = Int(buffer.format.channelCount)
                    if buffer.format.isInterleaved {
                        for i in 0..<n {
                            var sum: Float = 0
                            for c in 0..<channels { sum += data[0][i * channels + c] }
                            pending.append(sum / Float(channels))
                        }
                    } else {
                        for i in 0..<n {
                            var sum: Float = 0
                            for c in 0..<channels { sum += data[c][i] }
                            pending.append(sum / Float(channels))
                        }
                    }
                }
            }
            frames += Int64(buffer.frameLength)
        } catch {
            failure = error.localizedDescription
        }
    }

    func close() {
        lock.lock()
        file = nil // fecha o arquivo
        lock.unlock()
    }

    var seconds: Double { sampleRate > 0 ? Double(frames) / sampleRate : 0 }

    /// Pega o áudio ainda não transcrito: amostras, taxa e instante (relógio do sistema) do início.
    func takePending() -> (samples: [Float], rate: Double, startHost: Double)? {
        lock.lock()
        defer { lock.unlock() }
        guard !pending.isEmpty, sampleRate > 0, let first = firstHostSeconds else { return nil }
        let chunk = pending
        let start = first + Double(pendingStartFrame) / sampleRate
        pending.removeAll(keepingCapacity: true)
        return (chunk, sampleRate, start)
    }

    /// Nível recente (para o medidor na tela); zera depois de ler.
    func takePeak() -> Float {
        lock.lock()
        defer { lock.unlock() }
        let p = peak
        peak = 0
        return p
    }
}

extension CMSampleBuffer {
    /// Converte o áudio do ScreenCaptureKit para um buffer PCM.
    func toPCMBuffer() -> AVAudioPCMBuffer? {
        guard let description = formatDescription,
              let asbdPointer = CMAudioFormatDescriptionGetStreamBasicDescription(description) else { return nil }
        var asbd = asbdPointer.pointee
        guard let format = AVAudioFormat(streamDescription: &asbd) else { return nil }
        let frameCount = AVAudioFrameCount(numSamples)
        guard frameCount > 0, let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: frameCount) else { return nil }
        buffer.frameLength = frameCount
        let status = CMSampleBufferCopyPCMDataIntoAudioBufferList(
            self, at: 0, frameCount: Int32(frameCount), into: buffer.mutableAudioBufferList)
        return status == noErr ? buffer : nil
    }
}

/// Som de todos os apps (exceto este) via ScreenCaptureKit.
final class SystemAudioCapture: NSObject, SCStreamOutput, SCStreamDelegate {
    let writer: TrackWriter
    private var stream: SCStream?
    private let queue = DispatchQueue(label: "craftpad.voicelab.system-audio")

    init(writer: TrackWriter) {
        self.writer = writer
    }

    func start() async throws {
        let content: SCShareableContent
        do {
            content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
        } catch {
            throw VoicelabError.message("""
                Sem permissão para gravar o som do computador.
                Abra Ajustes do Sistema ▸ Privacidade e Segurança ▸ Gravação de Tela e Áudio do Sistema,
                ative o Terminal e rode de novo. (Para gravar só o microfone, use --sem-sistema.)
                """)
        }
        guard let display = content.displays.first else {
            throw VoicelabError.message("Nenhuma tela encontrada para capturar o áudio.")
        }
        let filter = SCContentFilter(display: display, excludingApplications: [], exceptingWindows: [])
        let config = SCStreamConfiguration()
        config.capturesAudio = true
        config.excludesCurrentProcessAudio = true
        config.sampleRate = 48_000
        config.channelCount = 2
        // Vídeo mínimo: só o áudio interessa
        config.width = 2
        config.height = 2
        config.minimumFrameInterval = CMTime(value: 1, timescale: 1)
        config.queueDepth = 5

        let stream = SCStream(filter: filter, configuration: config, delegate: self)
        try stream.addStreamOutput(self, type: .screen, sampleHandlerQueue: queue)
        try stream.addStreamOutput(self, type: .audio, sampleHandlerQueue: queue)
        try await stream.startCapture()
        self.stream = stream
    }

    func stop() async {
        if let stream {
            try? await stream.stopCapture()
        }
        stream = nil
        writer.close()
    }

    func stream(_ stream: SCStream, didOutputSampleBuffer sampleBuffer: CMSampleBuffer, of type: SCStreamOutputType) {
        guard type == .audio, sampleBuffer.isValid, let buffer = sampleBuffer.toPCMBuffer() else { return }
        let pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer)
        writer.write(buffer, hostSeconds: pts.seconds)
    }

    func stream(_ stream: SCStream, didStopWithError error: Error) {
        say("⚠️  A captura do som do computador parou: \(error.localizedDescription)")
    }
}

/// Microfone via AVAudioEngine.
final class MicrophoneCapture {
    let writer: TrackWriter
    private let engine = AVAudioEngine()

    init(writer: TrackWriter) {
        self.writer = writer
    }

    func start() async throws {
        let granted = await withCheckedContinuation { (c: CheckedContinuation<Bool, Never>) in
            AVCaptureDevice.requestAccess(for: .audio) { c.resume(returning: $0) }
        }
        guard granted else {
            throw VoicelabError.message("""
                Sem permissão para o microfone. Abra Ajustes do Sistema ▸ Privacidade e Segurança ▸ Microfone,
                ative o Terminal e rode de novo.
                """)
        }
        let input = engine.inputNode
        let format = input.outputFormat(forBus: 0)
        guard format.sampleRate > 0, format.channelCount > 0 else {
            throw VoicelabError.message("Nenhum microfone disponível.")
        }
        let writer = self.writer
        input.installTap(onBus: 0, bufferSize: 4096, format: format) { buffer, time in
            let host = time.isHostTimeValid ? AVAudioTime.seconds(forHostTime: time.hostTime) : ProcessInfo.processInfo.systemUptime
            writer.write(buffer, hostSeconds: host)
        }
        engine.prepare()
        try engine.start()
    }

    func stop() {
        engine.inputNode.removeTap(onBus: 0)
        engine.stop()
        writer.close()
    }
}

/// Espera Enter ou Ctrl+C.
final class StopSignal {
    private var continuation: CheckedContinuation<Void, Never>?
    private var fired = false
    private let lock = NSLock()
    private var source: DispatchSourceSignal?

    func wait() async {
        await withCheckedContinuation { (c: CheckedContinuation<Void, Never>) in
            lock.lock()
            if fired {
                lock.unlock()
                c.resume()
                return
            }
            continuation = c
            lock.unlock()

            signal(SIGINT, SIG_IGN)
            let s = DispatchSource.makeSignalSource(signal: SIGINT, queue: .global())
            s.setEventHandler { [weak self] in self?.fire() }
            s.resume()
            source = s

            DispatchQueue.global().async { [weak self] in
                _ = readLine()
                self?.fire()
            }
        }
    }

    private func fire() {
        lock.lock()
        let c = continuation
        continuation = nil
        fired = true
        lock.unlock()
        c?.resume()
    }
}

enum Recorder {
    /// Grava até Enter/Ctrl+C. Com `live`, vai transcrevendo trechos durante a gravação
    /// (rascunho por canal, sem separar falantes; a versão final vem na análise).
    static func record(into folder: URL, captureSystem: Bool, live: Bool, vocabulary: Vocabulary?) async throws -> URL {
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)

        let micWriter = TrackWriter(url: folder.appendingPathComponent("microfone.caf"))
        let sysWriter = TrackWriter(url: folder.appendingPathComponent("sistema.caf"))
        micWriter.keepPending = live
        sysWriter.keepPending = live
        let mic = MicrophoneCapture(writer: micWriter)
        let system = captureSystem ? SystemAudioCapture(writer: sysWriter) : nil

        var liveEngine: ParakeetEngine?
        if live {
            Events.stage("⏳ Carregando o modelo de transcrição ao vivo...")
            let engine = ParakeetEngine()
            try await engine.prepare()
            liveEngine = engine
        }

        try await mic.start()
        if let system {
            do {
                try await system.start()
            } catch {
                mic.stop()
                throw error
            }
        }
        let started = Date()

        say("🎙  Gravando em \(folder.path)")
        say(captureSystem ? "    Canais: microfone (sala) + som do computador (reunião online)"
                          : "    Canal: só microfone (sala)")
        say("    Pressione Enter (ou Ctrl+C) para parar.\n")
        Events.emit("gravando", ["pasta": folder.path, "sistema": captureSystem])

        // Medidor de nível a cada segundo
        let meter = Task {
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 1_000_000_000)
                let m = micWriter.takePeak()
                let s = sysWriter.takePeak()
                let elapsed = Date().timeIntervalSince(started)
                if Events.enabled {
                    Events.emit("nivel", ["tempo": elapsed, "microfone": Double(m), "sistema": Double(s)])
                    continue
                }
                let bar: (Float) -> String = { level in
                    let n = min(20, Int(level * 40))
                    return String(repeating: "█", count: n) + String(repeating: "·", count: 20 - n)
                }
                let line = captureSystem
                    ? "\r  \(timestamp(elapsed))  mic \(bar(m))  sistema \(bar(s))  "
                    : "\r  \(timestamp(elapsed))  mic \(bar(m))  "
                FileHandle.standardError.write(line.data(using: .utf8)!)
            }
        }

        // Transcrição ao vivo a cada ~10 s
        let liveTask = Task {
            guard let engine = liveEngine else { return }
            let converter = AudioConverter()
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 10_000_000_000)
                let zero = [micWriter.firstHostSeconds, sysWriter.firstHostSeconds].compactMap { $0 }.min() ?? 0
                for (name, writer) in [("presencial", micWriter), ("remoto", sysWriter)] {
                    guard let chunk = writer.takePending(), Double(chunk.samples.count) > chunk.rate * 0.5 else { continue }
                    guard rms(chunk.samples) > 0.003 else { continue } // silêncio
                    do {
                        let samples = try converter.resample(chunk.samples, from: chunk.rate)
                        var words = try await engine.transcribe(samples)
                        if let vocabulary { words = vocabulary.correct(words) }
                        let text = words.map(\.text).joined(separator: " ")
                        guard !text.isEmpty else { continue }
                        let start = chunk.startHost - zero
                        let end = start + Double(chunk.samples.count) / chunk.rate
                        Events.emit("trecho", ["canal": name, "inicio": start, "fim": end, "texto": text])
                        if !Events.enabled {
                            say("\n  [\(timestamp(start))] (\(name)) \(text)")
                        }
                    } catch {
                        say("⚠️  Transcrição ao vivo falhou: \(error.localizedDescription)")
                    }
                }
            }
        }

        await StopSignal().wait()
        meter.cancel()
        liveTask.cancel()
        _ = await liveTask.value
        say("\n⏹  Parando...")
        Events.emit("parado")

        mic.stop()
        await system?.stop()

        if let f = micWriter.failure { say("⚠️  Erro ao gravar o microfone: \(f)") }
        if let f = sysWriter.failure { say("⚠️  Erro ao gravar o som do computador: \(f)") }

        let session = RecordingSession(
            startedAt: started,
            micFile: micWriter.frames > 0 ? "microfone.caf" : nil,
            micStartHost: micWriter.firstHostSeconds,
            systemFile: sysWriter.frames > 0 ? "sistema.caf" : nil,
            systemStartHost: sysWriter.firstHostSeconds
        )
        try jsonEncoder().encode(session).write(to: folder.appendingPathComponent("sessao.json"))
        say(String(format: "    Microfone: %.0f s   Som do computador: %.0f s", micWriter.seconds, sysWriter.seconds))
        return folder
    }
}
