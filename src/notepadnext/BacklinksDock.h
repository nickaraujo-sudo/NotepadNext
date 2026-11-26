/*
 * NotepadNext - recursos de notas no estilo Obsidian (vault, links, tags, grafo).
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#ifndef BACKLINKSDOCK_H
#define BACKLINKSDOCK_H

#include <QDockWidget>
#include <QPointer>

class MainWindow;
class ScintillaNext;
class QLabel;
class QTreeWidget;

// Mostra quem aponta para a nota atual (backlinks) e para onde ela aponta
class BacklinksDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit BacklinksDock(MainWindow *parent);

private slots:
    void setEditor(ScintillaNext *editor);
    void refresh();

private:
    MainWindow *window;
    QPointer<ScintillaNext> editor;
    QLabel *title;
    QTreeWidget *tree;
};

#endif // BACKLINKSDOCK_H
