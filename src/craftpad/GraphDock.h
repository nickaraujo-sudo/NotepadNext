/*
 * CraftPad - extensão do Notepad Next no estilo Obsidian.
 * Licença GPL-3.0 (a mesma do Notepad Next).
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
