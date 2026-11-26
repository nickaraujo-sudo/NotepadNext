/*
 * This file is part of Notepad Next (CraftPad fork).
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

#include "ScintillaTypes.h"

class MainWindow;
class ScintillaNext;
class QTextBrowser;
class QTimer;

// Painel lateral que mostra o Markdown do editor atual já formatado.
class MarkdownPreviewDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit MarkdownPreviewDock(MainWindow *parent);

private slots:
    void connectToEditor(ScintillaNext *editor);
    void editorUIUpdated(Scintilla::Update updated);
    void renderPreview();

private:
    void disconnectFromEditor();
    void syncScroll();

    MainWindow *window;
    QTextBrowser *browser;
    QTimer *renderTimer;
    QPointer<ScintillaNext> editor;
    QMetaObject::Connection editorConnection;
};

#endif // MARKDOWNPREVIEWDOCK_H
