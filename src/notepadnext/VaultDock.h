/*
 * NotepadNext - recursos de notas no estilo Obsidian (vault, links, tags, grafo).
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#ifndef VAULTDOCK_H
#define VAULTDOCK_H

#include <QDockWidget>

class QFileSystemModel;
class QLabel;
class QSortFilterProxyModel;
class QTreeView;
class QTreeWidget;
class QTreeWidgetItem;
class VaultIndex;

// Painel esquerdo no estilo Obsidian: pastas em cima, tags embaixo
class VaultDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit VaultDock(QWidget *parent);

public slots:
    void chooseVault();
    void newNote();
    void showTag(const QString &tag);
    void selectFile(const QString &path);

private slots:
    void refreshTags();
    void vaultChanged();

private:
    VaultIndex *index;
    QLabel *vaultLabel;
    QFileSystemModel *fsModel;
    QSortFilterProxyModel *proxy;
    QTreeView *fileTree;
    QTreeWidget *tagTree;
};

#endif // VAULTDOCK_H
