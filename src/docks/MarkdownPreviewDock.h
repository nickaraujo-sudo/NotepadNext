/*
 * This file is part of Notepad Next (craftpad++ fork).
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef MARKDOWNPREVIEWDOCK_H
#define MARKDOWNPREVIEWDOCK_H

#include <QDockWidget>
#include <QPointer>
#include <QTextBrowser>

#include "ScintillaTypes.h"
#include "MarkdownRenderer.h"

class MainWindow;
class ScintillaNext;
class QAction;
class QTimer;

// Prévia que aceita edição e arrastar imagens
class PreviewBrowser : public QTextBrowser
{
    Q_OBJECT
public:
    using QTextBrowser::QTextBrowser;

signals:
    void imageDropped(const QMimeData *mime, int position);

protected:
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    QMimeData *createMimeDataFromSelection() const override;
};

// Painel lateral com o Markdown do editor atual já formatado.
// Também pode ser editado: as mudanças voltam para o arquivo Markdown.
class MarkdownPreviewDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit MarkdownPreviewDock(MainWindow *parent);

    PreviewBrowser *browser() const { return view; }
    bool previewHasFocus() const;
    ScintillaNext *currentEditor() const { return editor; }

    // Envia já as edições pendentes da prévia para o arquivo
    void syncToSource();
    // Célula da tabela (no arquivo) onde está o cursor da prévia
    bool sourceTableCell(int *headerLine, int *row, int *col) const;
    // Linha do arquivo correspondente ao cursor da prévia
    int sourceLineAtCursor() const;

public slots:
    void renderPreview();
    void chooseFonts();

private slots:
    void connectToEditor(ScintillaNext *editor);
    void editorUIUpdated(Scintilla::Update updated);
    void previewContentsChanged();

private:
    void disconnectFromEditor();
    void syncScroll();
    void handleLink(const QUrl &url);
    bool eventFilter(QObject *watched, QEvent *event) override;
    void fitWideImages();
    int segmentStartPosition(int segment) const;
    void insertDroppedImage(const QMimeData *mime, int position);

    MainWindow *window;
    PreviewBrowser *view;
    QAction *editAction;
    QTimer *renderTimer;
    QTimer *syncTimer;
    QPointer<ScintillaNext> editor;
    QMetaObject::Connection editorConnection;

    MarkdownRenderer renderer;
    QStringList baseline;      // Markdown de cada trecho logo após desenhar
    QString lastRendered;      // texto do editor no último desenho
    bool rendering = false;
    bool dirty = false;
    int lastViewportWidth = 0;
    bool swallowRelease = false;
};

#endif // MARKDOWNPREVIEWDOCK_H
