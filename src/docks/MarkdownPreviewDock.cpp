/*
 * This file is part of Notepad Next (NotepadNext fork).
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "MarkdownPreviewDock.h"

#include "MainWindow.h"
#include "ScintillaNext.h"
#include "NotepadNextHub.h"
#include "VaultIndex.h"
#include "VaultTables.h"
#include "ThemeManager.h"
#include "ImageSupport.h"
#include "MarkdownCommands.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSpinBox>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextTable>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// PreviewBrowser
// ---------------------------------------------------------------------------

bool PreviewBrowser::canInsertFromMimeData(const QMimeData *source) const
{
    return ImageSupport::canHandle(source) || QTextBrowser::canInsertFromMimeData(source);
}

void PreviewBrowser::insertFromMimeData(const QMimeData *source)
{
    if (ImageSupport::canHandle(source)) {
        emit imageDropped(source, textCursor().position());
        return;
    }
    // Texto colado entra sem formatação estranha (vira Markdown simples)
    if (source->hasText()) {
        textCursor().insertText(source->text());
        return;
    }
    QTextBrowser::insertFromMimeData(source);
}

QMimeData *PreviewBrowser::createMimeDataFromSelection() const
{
    return QTextBrowser::createMimeDataFromSelection();
}

// ---------------------------------------------------------------------------
// MarkdownPreviewDock
// ---------------------------------------------------------------------------

MarkdownPreviewDock::MarkdownPreviewDock(MainWindow *parent) :
    QDockWidget(tr("Prévia Markdown"), parent),
    window(parent)
{
    // Necessário para o app lembrar se o painel estava aberto
    setObjectName(QStringLiteral("MarkdownPreviewDock"));

    QWidget *container = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Barra discreta: editar na prévia e fonte
    QToolBar *bar = new QToolBar(container);
    bar->setIconSize(QSize(16, 16));
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    editAction = bar->addAction(tr("✎ Editar aqui"));
    editAction->setCheckable(true);
    editAction->setChecked(true);
    editAction->setToolTip(tr("Editar o texto direto na prévia; as mudanças vão para o arquivo Markdown. Cmd+clique abre links."));
    QAction *fontAction = bar->addAction(tr("Aa Fonte"));
    fontAction->setToolTip(tr("Fonte e tamanho da prévia (só a visualização)"));
    connect(fontAction, &QAction::triggered, this, &MarkdownPreviewDock::chooseFonts);
    layout->addWidget(bar);

    view = new PreviewBrowser(container);
    view->setOpenLinks(false);
    view->setReadOnly(false);
    view->setUndoRedoEnabled(false); // desfazer fica no editor do arquivo
    view->setFrameShape(QFrame::NoFrame);
    view->document()->setDocumentMargin(20);
    view->viewport()->installEventFilter(this);
    view->viewport()->setMouseTracking(true);
    layout->addWidget(view);
    setWidget(container);

    connect(editAction, &QAction::toggled, this, [this](bool on) {
        syncToSource();
        view->setReadOnly(!on);
        view->setTextInteractionFlags(on ? Qt::TextEditorInteraction | Qt::LinksAccessibleByMouse
                                         : Qt::TextBrowserInteraction);
    });

    connect(view, &QTextBrowser::anchorClicked, this, &MarkdownPreviewDock::handleLink);
    connect(view->document(), &QTextDocument::contentsChanged, this, &MarkdownPreviewDock::previewContentsChanged);
    connect(view, &PreviewBrowser::imageDropped, this, &MarkdownPreviewDock::insertDroppedImage);

    // Espera uma pausa curta na digitação antes de redesenhar
    renderTimer = new QTimer(this);
    renderTimer->setSingleShot(true);
    renderTimer->setInterval(250);
    connect(renderTimer, &QTimer::timeout, this, &MarkdownPreviewDock::renderPreview);

    // Edições na prévia vão para o arquivo depois de uma pausa
    syncTimer = new QTimer(this);
    syncTimer->setSingleShot(true);
    syncTimer->setInterval(500);
    connect(syncTimer, &QTimer::timeout, this, &MarkdownPreviewDock::syncToSource);

    connect(ThemeManager::instance(), &ThemeManager::previewStyleChanged, this, [this]() {
        lastRendered.clear();
        renderPreview();
    });

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
            connect(window, &MainWindow::editorActivated, this, &MarkdownPreviewDock::connectToEditor, Qt::UniqueConnection);
        }
        else {
            syncToSource();
            disconnectFromEditor();
            disconnect(window, &MainWindow::editorActivated, this, &MarkdownPreviewDock::connectToEditor);
        }
    });
}

bool MarkdownPreviewDock::previewHasFocus() const
{
    return isVisible() && view->hasFocus();
}

void MarkdownPreviewDock::connectToEditor(ScintillaNext *newEditor)
{
    syncToSource();
    disconnectFromEditor();

    editor = newEditor;
    lastRendered.clear();
    if (!editor) {
        rendering = true;
        view->clear();
        rendering = false;
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
    else if (FlagSet(updated, Scintilla::Update::VScroll) && !view->hasFocus()) {
        syncScroll();
    }
}

int MarkdownPreviewDock::segmentStartPosition(int segment) const
{
    for (QTextBlock b = view->document()->begin(); b.isValid(); b = b.next())
        if (b.userState() == segment)
            return b.position();
    return -1;
}

void MarkdownPreviewDock::renderPreview()
{
    if (!editor) {
        return;
    }
    if (dirty) {
        // Edições da prévia ainda não enviadas têm prioridade
        syncToSource();
        return;
    }

    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(editor->characterPointer()), editor->textLength());
    if (text == lastRendered && !view->document()->isEmpty())
        return;

    const QString noteDir = editor->isFile() ? editor->getFileInfo().absolutePath() : QString();
    QStringList paths;
    if (!noteDir.isEmpty()) paths << noteDir;
    if (NotepadNextHub::instance()->index()->hasRoot()) paths << NotepadNextHub::instance()->index()->root();
    view->setSearchPaths(paths);

    const QString selfPath = editor->getFilePath();
    renderer.setTableLookup([selfPath](const QString &name) { return VaultTables::instance().find(name, selfPath); });
    renderer.setAttachmentResolver([noteDir](const QString &name) { return ImageSupport::resolveAttachment(name, noteDir); });
    renderer.setStyle(ThemeManager::instance()->previewStyle());

    // Mantém a posição de leitura e do cursor ao redesenhar
    const int scrollPos = view->verticalScrollBar()->value();
    const QTextCursor oldCursor = view->textCursor();
    const int cursorSegment = MarkdownRenderer::segmentOfBlock(oldCursor.block());
    const int cursorOffset = cursorSegment >= 0 ? oldCursor.position() - segmentStartPosition(cursorSegment) : -1;

    rendering = true;
    renderer.render(text, view->document());
    fitWideImages();
    baseline.clear();
    for (int i = 0; i < renderer.segments().size(); ++i)
        baseline << renderer.segmentMarkdown(view->document(), i);
    rendering = false;
    lastRendered = text;
    dirty = false;

    if (view->hasFocus() && cursorSegment >= 0) {
        int seg = qMin(cursorSegment, int(renderer.segments().size()) - 1);
        int start = segmentStartPosition(seg);
        while (start < 0 && seg > 0) start = segmentStartPosition(--seg);
        QTextCursor c(view->document());
        c.setPosition(qBound(0, qMax(0, start) + cursorOffset, view->document()->characterCount() - 1));
        view->setTextCursor(c);
    }
    view->verticalScrollBar()->setValue(scrollPos);
}

void MarkdownPreviewDock::fitWideImages()
{
    // Imagens sem largura definida não passam da largura do painel
    const int maxWidth = qMax(120, view->viewport()->width() - 2 * int(view->document()->documentMargin()) - 8);
    QTextDocument *doc = view->document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.charFormat().isImageFormat()) continue;
            QTextImageFormat img = f.charFormat().toImageFormat();
            if (img.width() > 0) continue;
            QString path = img.name();
            if (!QFileInfo(path).isAbsolute()) {
                for (const QString &sp : view->searchPaths()) {
                    if (QFileInfo::exists(QDir(sp).filePath(path))) { path = QDir(sp).filePath(path); break; }
                }
            }
            const QSize size = QImageReader(path).size();
            if (size.isValid() && size.width() > maxWidth) {
                img.setWidth(maxWidth);
                QTextCursor c(doc);
                c.setPosition(f.position());
                c.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
                c.setCharFormat(img);
            }
        }
    }
}

void MarkdownPreviewDock::previewContentsChanged()
{
    if (rendering || !editor)
        return;
    dirty = true;
    syncTimer->start();
}

void MarkdownPreviewDock::syncToSource()
{
    syncTimer->stop();
    if (!dirty || !editor || rendering)
        return;
    dirty = false;

    const QList<MdSegment> &segs = renderer.segments();
    QList<QPair<int, QString>> changes;
    for (int i = 0; i < segs.size() && i < baseline.size(); ++i) {
        if (segs.at(i).kind == MdSegment::Frontmatter) continue;
        const QString md = renderer.segmentMarkdown(view->document(), i);
        if (md != baseline.at(i))
            changes << qMakePair(i, md);
    }
    if (changes.isEmpty())
        return;

    // Confere se o arquivo não mudou por fora desde o último desenho
    const QString current = QString::fromUtf8(reinterpret_cast<const char *>(editor->characterPointer()), editor->textLength());
    if (current != lastRendered) {
        lastRendered.clear();
        renderPreview();
        return;
    }

    editor->beginUndoAction();
    for (int k = changes.size() - 1; k >= 0; --k) {
        const MdSegment &seg = segs.at(changes.at(k).first);
        const QString md = changes.at(k).second;
        if (md.isEmpty()) {
            // Trecho apagado na prévia: remove as linhas (e a linha vazia seguinte)
            int last = seg.endLine - 1;
            if (last + 1 < editor->lineCount() && MarkdownCommands::lineText(editor, last + 1).trimmed().isEmpty()) ++last;
            const int from = editor->positionFromLine(seg.startLine);
            const int to = last + 1 < editor->lineCount() ? editor->positionFromLine(last + 1) : editor->textLength();
            editor->setTargetRange(from, to);
            editor->replaceTarget(0, "");
        }
        else {
            MarkdownCommands::replaceLines(editor, seg.startLine, seg.endLine - 1, md);
        }
    }
    editor->endUndoAction();

    // Redesenha já, mantendo o cursor da prévia no lugar
    lastRendered.clear();
    renderTimer->stop();
    renderPreview();
}

bool MarkdownPreviewDock::sourceTableCell(int *headerLine, int *row, int *col) const
{
    const QTextCursor c = view->textCursor();
    QTextTable *table = c.currentTable();
    if (!table) return false;
    const int seg = MarkdownRenderer::segmentOfBlock(c.block());
    if (seg < 0 || seg >= renderer.segments().size()) return false;
    const MdSegment &s = renderer.segments().at(seg);
    if (s.kind != MdSegment::Table) return false;
    const QTextTableCell cell = table->cellAt(c);
    *headerLine = renderer.tables().at(s.tableIndex).firstLine;
    *row = cell.row();
    *col = cell.column();
    return true;
}

int MarkdownPreviewDock::sourceLineAtCursor() const
{
    const int seg = MarkdownRenderer::segmentOfBlock(view->textCursor().block());
    if (seg < 0 || seg >= renderer.segments().size()) return -1;
    return renderer.segments().at(seg).endLine - 1;
}

void MarkdownPreviewDock::insertDroppedImage(const QMimeData *mime, int position)
{
    if (!editor) return;
    syncToSource();
    QTextCursor c(view->document());
    c.setPosition(qBound(0, position, view->document()->characterCount() - 1));
    const int seg = MarkdownRenderer::segmentOfBlock(c.block());
    int line = seg >= 0 && seg < renderer.segments().size() ? renderer.segments().at(seg).endLine - 1 : editor->lineCount() - 1;
    line = qBound(0, line, int(editor->lineCount()) - 1);

    // A imagem entra num parágrafo novo, depois do trecho onde foi solta
    editor->beginUndoAction();
    const int end = editor->lineEndPosition(line);
    MarkdownCommands::insertTextAt(editor, end, QStringLiteral("\n\n"));
    const int insertAt = editor->lineEndPosition(line) + MarkdownCommands::eol(editor).toUtf8().size() * 2;
    if (!ImageSupport::insertFromMime(editor, mime, insertAt)) {
        editor->endUndoAction();
        editor->undo();
        return;
    }
    editor->endUndoAction();
}

void MarkdownPreviewDock::handleLink(const QUrl &url)
{
    if (url.scheme() == QLatin1String("notepadnext")) {
        // [[Nota#Seção]] abre a nota
        const QString target = QUrl::fromPercentEncoding(url.toString().mid(9).toUtf8()).section(QLatin1Char('#'), 0, 0).trimmed();
        emit NotepadNextHub::instance()->openNoteRequested(target, editor ? editor->getFilePath() : QString());
    }
    else if (url.scheme() == QLatin1String("notepadnext-tag")) {
        emit NotepadNextHub::instance()->tagRequested(url.path());
    }
    else {
        QDesktopServices::openUrl(url);
    }
}

bool MarkdownPreviewDock::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != view->viewport())
        return QDockWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        const QPoint pos = me->position().toPoint();

        // Clique na caixa de uma tarefa marca/desmarca
        const QTextCursor c = view->cursorForPosition(pos);
        const QTextBlock block = c.block();
        const QTextBlockFormat bf = block.blockFormat();
        if (bf.marker() != QTextBlockFormat::MarkerType::NoMarker && me->button() == Qt::LeftButton) {
            QTextCursor start(block);
            const int textLeft = view->cursorRect(start).left();
            if (pos.x() < textLeft - 2) {
                QTextBlockFormat nf = bf;
                nf.setMarker(bf.marker() == QTextBlockFormat::MarkerType::Checked ? QTextBlockFormat::MarkerType::Unchecked
                                                                                  : QTextBlockFormat::MarkerType::Checked);
                start.setBlockFormat(nf);
                dirty = true;
                syncToSource();
                swallowRelease = true; // o Qt também alternaria a caixa ao soltar o botão
                return true;
            }
        }
    }
    else if (event->type() == QEvent::MouseButtonRelease) {
        if (swallowRelease) {
            swallowRelease = false;
            return true;
        }
        auto *me = static_cast<QMouseEvent *>(event);
        // Na edição, Cmd+clique abre links (clique simples posiciona o cursor)
        if (!view->isReadOnly() && (me->modifiers() & Qt::ControlModifier)) {
            const QString href = view->anchorAt(me->position().toPoint());
            if (!href.isEmpty()) {
                handleLink(QUrl(href));
                return true;
            }
        }
    }
    else if (event->type() == QEvent::MouseButtonDblClick) {
        // Duplo clique numa imagem abre tamanho e posição
        auto *me = static_cast<QMouseEvent *>(event);
        QTextCursor c = view->cursorForPosition(me->position().toPoint());
        QString name;
        for (int delta : {0, 1}) {
            QTextCursor probe(view->document());
            probe.setPosition(qBound(0, c.position() + delta, view->document()->characterCount() - 1));
            if (probe.charFormat().isImageFormat()) { name = probe.charFormat().toImageFormat().name(); c = probe; break; }
        }
        if (!name.isEmpty() && editor) {
            const int seg = MarkdownRenderer::segmentOfBlock(c.block());
            if (seg >= 0 && seg < renderer.segments().size()) {
                syncToSource();
                const MdSegment &s = renderer.segments().at(seg);
                for (const auto &img : s.images) {
                    if (img.first != name) continue;
                    for (int line = s.startLine; line < s.endLine; ++line) {
                        if (MarkdownCommands::lineText(editor, line).contains(img.second)) {
                            ImageSupport::editImageAt(editor, window, line, img.second);
                            return true;
                        }
                    }
                }
            }
        }
    }
    else if (event->type() == QEvent::Resize) {
        // Só redesenha se a largura mudou de verdade (imagens largas se ajustam)
        const int w = view->viewport()->width();
        if (qAbs(w - lastViewportWidth) > 24) {
            lastViewportWidth = w;
            QTimer::singleShot(0, this, [this]() {
                if (!dirty && !view->hasFocus()) { lastRendered.clear(); renderTimer->start(); }
            });
        }
    }
    return QDockWidget::eventFilter(watched, event);
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
    QScrollBar *bar = view->verticalScrollBar();
    bar->setValue(qRound(ratio * bar->maximum()));
}

void MarkdownPreviewDock::chooseFonts()
{
    const PreviewStyle s = ThemeManager::instance()->previewStyle();
    QDialog d(this);
    d.setWindowTitle(tr("Fonte da prévia"));
    auto *form = new QFormLayout(&d);

    auto row = [&](const QString &label, const QString &family, int size, QFontComboBox **combo, QSpinBox **spin, bool mono) {
        auto *w = new QWidget(&d);
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        *combo = new QFontComboBox(w);
        if (mono) (*combo)->setFontFilters(QFontComboBox::MonospacedFonts);
        if (!family.isEmpty()) (*combo)->setCurrentFont(QFont(family));
        *spin = new QSpinBox(w);
        (*spin)->setRange(8, 72);
        (*spin)->setValue(size);
        (*spin)->setSuffix(QStringLiteral(" pt"));
        h->addWidget(*combo, 1);
        h->addWidget(*spin);
        form->addRow(label, w);
    };

    QFontComboBox *bodyCombo, *headCombo, *codeCombo;
    QSpinBox *bodySpin, *headSpin, *codeSpin;
    row(tr("Texto:"), s.bodyFont.isEmpty() ? font().family() : s.bodyFont, s.bodySize, &bodyCombo, &bodySpin, false);
    row(tr("Títulos (H1):"), s.headingFont.isEmpty() ? font().family() : s.headingFont, s.headingSize, &headCombo, &headSpin, false);
    row(tr("Código:"), s.codeFont, s.codeSize, &codeCombo, &codeSpin, true);

    auto *note = new QLabel(tr("Só muda a visualização; o arquivo Markdown continua igual."), &d);
    note->setStyleSheet(QStringLiteral("color: gray"));
    form->addRow(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &d);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &d, [&]() {
        ThemeManager::instance()->setPreviewFonts(QString(), 14, QString(), 24, QString(), 12);
        d.reject();
    });
    if (d.exec() != QDialog::Accepted)
        return;
    ThemeManager::instance()->setPreviewFonts(bodyCombo->currentFont().family(), bodySpin->value(),
                                              headCombo->currentFont().family(), headSpin->value(),
                                              codeCombo->currentFont().family(), codeSpin->value());
}
