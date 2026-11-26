/*
 * NotepadNext - recursos de notas no estilo Obsidian (vault, links, tags, grafo).
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#ifndef GRAPHDOCK_H
#define GRAPHDOCK_H

#include <QDockWidget>

class GraphView;
class MainWindow;
class ScintillaNext;

class GraphDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit GraphDock(MainWindow *parent);

private slots:
    void editorActivated(ScintillaNext *editor);

private:
    GraphView *view;
};

#endif // GRAPHDOCK_H
