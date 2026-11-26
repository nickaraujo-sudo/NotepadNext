# voicelab — protótipo de teste (craftpad++ Notetaker / Meeting Assistant)

Grava reuniões **sem bot**, separa quem falou o quê (cada interrupção vira uma fala) e
**guarda a voz de cada pessoa** para reconhecê-la nas próximas reuniões. Tudo roda no Mac,
no Neural Engine, sem conta nem chave de API.

## Requisitos

- Mac com Apple Silicon, **macOS 15 ou mais novo** (no macOS 14 há um bug da Apple que derruba a separação de falantes)
- Xcode ou as Command Line Tools com Swift 6 (`swift --version`)
- Uns 3 GB livres para os modelos (baixados automaticamente na primeira execução)

## Instalar

```bash
cd ~/NotepadNext
git fetch origin
git checkout voicelab
cd tools/voicelab
swift build -c release
```

O programa fica em `.build/release/voicelab`. Para usar de qualquer pasta:

```bash
sudo ln -sf "$PWD/.build/release/voicelab" /usr/local/bin/voicelab
```

## Permissões (uma vez)

Na primeira gravação, o macOS pede acesso ao **Microfone** e à **Gravação de Tela e Áudio do Sistema**
para o Terminal. Se recusar sem querer: Ajustes do Sistema ▸ Privacidade e Segurança.

## Usar

| Situação | Comando |
|---|---|
| Reunião online (Zoom, Teams, Meet, Slack) | `voicelab gravar` |
| Reunião presencial (só o microfone) | `voicelab gravar --sem-sistema` |
| Gravação que você já tem (Plaud, .m4a, .wav, .mp3) | `voicelab analisar ~/caminho/arquivo.m4a` |
| Dar nome às vozes depois da análise | `voicelab nomear <pasta>` |
| Ver ou apagar vozes guardadas | `voicelab vozes` / `voicelab vozes --esquecer "Nome"` |

`gravar` para com **Enter** (ou Ctrl+C) e já analisa. As gravações ficam em `~/craftpad-reunioes/`.

Opções de análise: `--motor nemotron|parakeet|ambos` (padrão: ambos, para comparar),
`--max-falantes N`, `--limiar 0.62` (semelhança mínima para reconhecer uma voz), `--titulo "..."`.

## O que sai

Na pasta da reunião:

- `transcricao-nemotron.md` e `transcricao-parakeet.md` — uma por motor, para comparar
- `relatorio.md` — tempos, falantes encontrados e a semelhança de cada um com o banco de vozes
- `resultado.json` — tudo em formato estruturado (é o que o craftpad++ vai ler)
- `microfone.caf`, `sistema.caf`, `sessao.json` — o áudio original de cada canal

O banco de vozes fica em `~/Library/Application Support/craftpad/vozes.json`.

## Roteiro de teste sugerido

1. **Presencial**: 5–10 minutos com 2–3 pessoas, com interrupções de propósito → `voicelab gravar --sem-sistema`
2. **Online**: uma chamada real no Zoom/Teams → `voicelab gravar`
3. **Arquivo existente**: uma gravação do Plaud → `voicelab analisar arquivo.m4a`
4. `voicelab nomear` em uma delas; depois grave outra conversa com as mesmas pessoas e veja se são reconhecidas sozinhas
5. Compare as duas transcrições e o `relatorio.md`

## Como funciona

1. **Canais separados** — microfone (sala) e som do computador (participantes remotos) são gravados à parte, sem bot e sem driver.
2. **Cancelamento de eco** — se a reunião sai pelos alto-falantes, o microfone também capta os remotos; o LocalVQE remove esse eco.
3. **Separação de falantes** — pipeline pyannote community-1 (segmentação + WeSpeaker + VBx), com passos curtos para pegar interrupções rápidas.
4. **Transcrição** — Nemotron Unified 0.6B (português, com pontuação) e Parakeet Ultra.
5. **Vozes** — cada falante tem uma assinatura de voz; ela é comparada com o banco e, quando você confirma o nome, o banco aprende.

Motores e modelos: [FluidAudio](https://github.com/FluidInference/FluidAudio) (Apache 2.0). Os modelos baixam
sozinhos do Hugging Face, sem login nem aceite de termos. Licenças: pyannote/WeSpeaker e Parakeet (CC-BY-4.0),
Nemotron (NVIDIA Open Model License).

> Assinatura de voz é dado biométrico (dado pessoal sensível na LGPD). Use com o consentimento das pessoas gravadas.
