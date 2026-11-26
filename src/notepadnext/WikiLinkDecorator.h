/*
 * NotepadNext - recursos de notas no estilo Obsidian (vault, links, tags, grafo).
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#ifndef WIKILINKDECORATOR_H
#define WIKILINKDECORATOR_H

#include "EditorDecorator.h"

class QTimer;

// Destaca [[links]] e #tags em notas Markdown, abre com Cmd+clique,
// autocompleta ao digitar [[ ou # e mantém o índice do vault atualizado.
class WikiLinkDecorator : public EditorDecorator
{
    Q_OBJECT

public:
    explicit WikiLinkDecorator(ScintillaNext *editor);

public slots:
    void notify(const Scintilla::NotificationData *pscn) override;

private slots:
    void highlight();
    void pushToIndex();

private:
    bool isMarkdown() const;
    void clearAll();
    void showLinkList();
    void showTagList();

    int linkIndicator;
    int tagIndicator;
    QTimer *highlightTimer;
    QTimer *indexTimer;
    int listAnchor = -1;
};

#endif // WIKILINKDECORATOR_H
