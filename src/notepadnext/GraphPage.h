/*
 * NotepadNext - recursos de notas no estilo Obsidian (vault, links, tags, grafo).
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#ifndef GRAPHPAGE_H
#define GRAPHPAGE_H

#include <QWidget>

class GraphView;
class MainWindow;
class QAction;
class QLabel;
class ScintillaNext;

// Aba "Grafo": o grafo do vault ocupando a janela inteira.
// As opções viram ações, que aparecem na barra e no menu "Grafo".
class GraphPage : public QWidget
{
    Q_OBJECT

public:
    explicit GraphPage(MainWindow *window);

    QAction *showTagsAction() const { return tagsAction; }
    QAction *showOrphansAction() const { return orphansAction; }
    QAction *spinAction() const { return spinActionPtr; }
    QAction *fitAction() const { return fitActionPtr; }
    // Texto com o total de notas e tags (vai para a barra de ícones)
    QLabel *statsLabel() const { return stats; }

private slots:
    void editorActivated(ScintillaNext *editor);

private:
    GraphView *view;
    QAction *tagsAction;
    QAction *orphansAction;
    QAction *spinActionPtr;
    QAction *fitActionPtr;
    QLabel *stats;
};

#endif // GRAPHPAGE_H
