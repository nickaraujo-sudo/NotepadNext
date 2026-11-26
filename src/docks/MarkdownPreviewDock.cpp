/*
 * This file is part of Notepad Next (CraftPad fork).
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "MarkdownPreviewDock.h"

#include "MainWindow.h"
#include "ScintillaNext.h"
#include "CraftPadHub.h"
#include "VaultIndex.h"

#include <QScrollBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QUrl>

// Converte a sintaxe do Obsidian em Markdown comum que o Qt sabe exibir:
// [[Nota|apelido]] vira link clicável e #tag vira link de tag.
static QString obsidianToMarkdown(const QString &text)
{
    static const QRegularExpression embedRe(QStringLiteral(R"(!\[\[([^\]\|\n]+\.(?:png|jpe?g|gif|svg|webp|bmp))(?:\|[^\]\n]*)?\]\])"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression wikiRe(QStringLiteral(R"(!?\[\[([^\]\|\n]+)(?:\|([^\]\n]*))?\]\])"));
    static const QRegularExpression tagRe(QStringLiteral(R"((?<![\p{L}\p{N}_/#&\]\(])#([\p{L}\p{N}_\-/]*[\p{L}_\-][\p{L}\p{N}_\-/]*))"));
    static const QRegularExpression inlineCodeRe(QStringLiteral("`[^`\n]*`"));

    QStringList lines = text.split(QLatin1Char('\n'));

    // Frontmatter YAML (--- ... ---) não é conteúdo: remove da prévia
    if (!lines.isEmpty() && lines.first().trimmed() == QLatin1String("---")) {
        for (int i = 1; i < lines.size(); ++i) {
            const QString t = lines.at(i).trimmed();
            if (t == QLatin1String("---") || t == QLatin1String("...")) {
                lines = lines.mid(i + 1);
                break;
            }
        }
    }

    bool inFence = false;
    for (QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"))) {
            inFence = !inFence;
            continue;
        }
        if (inFence || line.contains(inlineCodeRe))
            continue;

        line.replace(embedRe, QStringLiteral("![](\\1)"));
        // Espaços no caminho da imagem quebram o Markdown
        QRegularExpressionMatchIterator imgs = QRegularExpression(QStringLiteral(R"(!\[\]\(([^)]*\s[^)]*)\))")).globalMatch(line);
        while (imgs.hasNext()) {
            const auto m = imgs.next();
            line.replace(m.captured(0), QStringLiteral("![](%1)").arg(QString(m.captured(1)).replace(QLatin1Char(' '), QStringLiteral("%20"))));
        }

        QString out;
        int last = 0;
        auto it = wikiRe.globalMatch(line);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString target = m.captured(1).section(QLatin1Char('#'), 0, 0).trimmed();
            QString label = m.captured(2).isEmpty() ? m.captured(1).trimmed() : m.captured(2).trimmed();
            out += line.mid(last, m.capturedStart() - last);
            out += QStringLiteral("[%1](craftpad:%2)").arg(label, QString::fromUtf8(QUrl::toPercentEncoding(target)));
            last = m.capturedEnd();
        }
        out += line.mid(last);
        line = out;

        const bool heading = line.startsWith(QLatin1String("# ")) || line.startsWith(QLatin1String("##"));
        if (!heading) {
            line.replace(tagRe, QStringLiteral("[#\\1](craftpad-tag:\\1)"));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

MarkdownPreviewDock::MarkdownPreviewDock(MainWindow *parent) :
    QDockWidget(tr("Prévia Markdown"), parent),
    window(parent)
{
    // Necessário para o app lembrar se o painel estava aberto
    setObjectName(QStringLiteral("MarkdownPreviewDock"));

    browser = new QTextBrowser(this);
    // Links tratados aqui: notas e tags do CraftPad, e endereços web
    browser->setOpenLinks(false);
    QPalette pal = browser->palette();
    pal.setColor(QPalette::Link, QColor::fromRgb(CraftPadHub::LinkColor));
    browser->setPalette(pal);
    connect(browser, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == QLatin1String("craftpad")) {
            const QString target = QUrl::fromPercentEncoding(url.path().toUtf8());
            emit CraftPadHub::instance()->openNoteRequested(target, editor ? editor->getFilePath() : QString());
        }
        else if (url.scheme() == QLatin1String("craftpad-tag")) {
            emit CraftPadHub::instance()->tagRequested(url.path());
        }
        else {
            QDesktopServices::openUrl(url);
        }
    });
    browser->document()->setDocumentMargin(16);
    setWidget(browser);

    // Espera uma pausa curta na digitação antes de redesenhar
    renderTimer = new QTimer(this);
    renderTimer->setSingleShot(true);
    renderTimer->setInterval(250);
    connect(renderTimer, &QTimer::timeout, this, &MarkdownPreviewDock::renderPreview);

    // Só acompanha o editor enquanto o painel está visível
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && !isFloating() && !property("sized").toBool()) {
            // Primeira abertura: divide a tela meio a meio com o editor
            setProperty("sized", true);
            QTimer::singleShot(0, this, [this]() {
                window->resizeDocks({this}, {qMax(320, window->width() * 2 / 5)}, Qt::Horizontal);
            });
        }
        if (visible) {
            connectToEditor(window->currentEditor());
            connect(window, &MainWindow::editorActivated, this, &MarkdownPreviewDock::connectToEditor);
        }
        else {
            disconnectFromEditor();
            disconnect(window, &MainWindow::editorActivated, this, &MarkdownPreviewDock::connectToEditor);
        }
    });
}

void MarkdownPreviewDock::connectToEditor(ScintillaNext *newEditor)
{
    disconnectFromEditor();

    editor = newEditor;
    if (!editor) {
        browser->clear();
        return;
    }

    editorConnection = connect(editor, &ScintillaNext::updateUi, this, &MarkdownPreviewDock::editorUIUpdated);
    renderPreview();
}

void MarkdownPreviewDock::disconnectFromEditor()
{
    if (editorConnection) {
        disconnect(editorConnection);
    }
    renderTimer->stop();
}

void MarkdownPreviewDock::editorUIUpdated(Scintilla::Update updated)
{
    if (FlagSet(updated, Scintilla::Update::Content)) {
        renderTimer->start();
    }
    else if (FlagSet(updated, Scintilla::Update::VScroll)) {
        syncScroll();
    }
}

void MarkdownPreviewDock::renderPreview()
{
    if (!editor) {
        return;
    }

    // Permite exibir imagens com caminho relativo ao arquivo
    const QFileInfo info = editor->getFileInfo();
    if (!info.filePath().isEmpty()) {
        QStringList paths{info.absolutePath()};
        if (CraftPadHub::instance()->index()->hasRoot())
            paths << CraftPadHub::instance()->index()->root();
        browser->setSearchPaths(paths);
    }

    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(editor->characterPointer()), editor->textLength());

    // Mantém a posição de leitura ao redesenhar
    const int scrollPos = browser->verticalScrollBar()->value();
    browser->setMarkdown(obsidianToMarkdown(text));
    browser->verticalScrollBar()->setValue(scrollPos);
}

void MarkdownPreviewDock::syncScroll()
{
    if (!editor) {
        return;
    }

    // Rola a prévia na mesma proporção que o editor
    const int maxLine = editor->lineCount() - editor->linesOnScreen();
    if (maxLine <= 0) {
        return;
    }

    const double ratio = qBound(0.0, double(editor->firstVisibleLine()) / maxLine, 1.0);
    QScrollBar *bar = browser->verticalScrollBar();
    bar->setValue(qRound(ratio * bar->maximum()));
}
