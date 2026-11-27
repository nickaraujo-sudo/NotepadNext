/*
 * NotepadNext - recursos de notas no estilo Obsidian (vault, links, tags, grafo).
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#include "VaultDock.h"
#include "NotepadNextHub.h"
#include "VaultIndex.h"

#include <QDir>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSet>
#include <QFileInfo>
#include <QSplitter>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>

// Esconde pastas e arquivos ocultos (.obsidian, .git, .DS_Store)
// Hides dot files and shows only the file formats chosen in Preferences (folders always show)
class HiddenFilterProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setFormats(const QStringList &list)
    {
        formats.clear();
        for (const QString &f : list) {
            formats.insert(f);
            if (f == QLatin1String("md")) formats.insert(QStringLiteral("markdown"));
        }
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const QModelIndex idx = sourceModel()->index(row, 0, parent);
        const QString name = idx.data().toString();
        if (name.startsWith(QLatin1Char('.')))
            return false;
        auto *fs = qobject_cast<QFileSystemModel *>(sourceModel());
        if (!fs || fs->isDir(idx))
            return true;
        return formats.contains(QFileInfo(name).suffix().toLower());
    }

private:
    QSet<QString> formats;
};

static QToolButton *makeButton(const QString &text, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}

static QLabel *makeHeader(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    QFont f = l->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 0.9);
    l->setFont(f);
    l->setContentsMargins(6, 6, 6, 2);
    return l;
}

VaultDock::VaultDock(QWidget *parent) :
    QDockWidget(tr("Vault"), parent),
    index(NotepadNextHub::instance()->index())
{
    setObjectName(QStringLiteral("NotepadNextVaultDock"));

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Barra superior: nome do vault + botões
    auto *bar = new QWidget(container);
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(6, 4, 2, 4);
    vaultLabel = new QLabel(bar);
    QFont bold = vaultLabel->font();
    bold.setBold(true);
    vaultLabel->setFont(bold);
    barLayout->addWidget(vaultLabel, 1);

    auto *newBtn = makeButton(QStringLiteral("＋"), tr("New note"), bar);
    auto *openBtn = makeButton(QStringLiteral("📂"), tr("Choose vault folder"), bar);
    barLayout->addWidget(newBtn);
    barLayout->addWidget(openBtn);
    connect(newBtn, &QToolButton::clicked, this, &VaultDock::newNote);
    connect(openBtn, &QToolButton::clicked, this, &VaultDock::chooseVault);
    layout->addWidget(bar);

    auto *splitter = new QSplitter(Qt::Vertical, container);
    splitter->setChildrenCollapsible(false); // arquivos e tags sempre visíveis

    // Parte de cima: pastas e arquivos
    auto *filesBox = new QWidget(splitter);
    auto *filesLayout = new QVBoxLayout(filesBox);
    filesLayout->setContentsMargins(0, 0, 0, 0);
    filesLayout->setSpacing(0);
    filesLayout->addWidget(makeHeader(tr("FILES"), filesBox));

    fsModel = new QFileSystemModel(this);
    fsModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    fsModel->setReadOnly(false); // permite renomear com duplo clique lento / F2
    auto *filter = new HiddenFilterProxy(this);
    proxy = filter;
    filter->setFormats(NotepadNextHub::sidebarFormats());
    connect(NotepadNextHub::instance(), &NotepadNextHub::sidebarFormatsChanged, filter, &HiddenFilterProxy::setFormats);
    proxy->setSourceModel(fsModel);

    fileTree = new QTreeView(filesBox);
    fileTree->setModel(proxy);
    fileTree->setHeaderHidden(true);
    for (int c = 1; c < 4; ++c)
        fileTree->hideColumn(c);
    fileTree->setAnimated(true);
    fileTree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    filesLayout->addWidget(fileTree);

    connect(fileTree, &QTreeView::activated, this, [this](const QModelIndex &idx) {
        const QString path = fsModel->filePath(proxy->mapToSource(idx));
        if (QFileInfo(path).isFile())
            emit NotepadNextHub::instance()->openFileRequested(path, -1);
    });

    // Parte de baixo: tags
    auto *tagsBox = new QWidget(splitter);
    auto *tagsLayout = new QVBoxLayout(tagsBox);
    tagsLayout->setContentsMargins(0, 0, 0, 0);
    tagsLayout->setSpacing(0);
    tagsLayout->addWidget(makeHeader(tr("TAGS"), tagsBox));

    tagTree = new QTreeWidget(tagsBox);
    tagTree->setColumnCount(2);
    tagTree->setHeaderHidden(true);
    tagTree->header()->setStretchLastSection(false);
    tagTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tagTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tagsLayout->addWidget(tagTree);

    // Clicar numa nota dentro de uma tag abre a nota
    connect(tagTree, &QTreeWidget::itemActivated, this, [](QTreeWidgetItem *item) {
        const QString path = item->data(0, Qt::UserRole + 1).toString();
        if (!path.isEmpty())
            emit NotepadNextHub::instance()->openFileRequested(path, -1);
    });

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);
    setWidget(container);

    connect(index, &VaultIndex::indexChanged, this, &VaultDock::refreshTags);
    connect(NotepadNextHub::instance(), &NotepadNextHub::tagRequested, this, &VaultDock::showTag);
    connect(NotepadNextHub::instance(), &NotepadNextHub::vaultRequested, this, &VaultDock::chooseVault);

    // Abre o último vault usado
    const QString saved = QSettings().value(QStringLiteral("NotepadNext/VaultPath")).toString();
    if (!saved.isEmpty() && QDir(saved).exists())
        index->setRoot(saved);
    vaultChanged();
}

void VaultDock::vaultChanged()
{
    if (!index->hasRoot()) {
        vaultLabel->setText(tr("No vault open"));
        fileTree->setModel(nullptr);
        refreshTags();
        return;
    }

    vaultLabel->setText(QDir(index->root()).dirName());
    vaultLabel->setToolTip(index->root());
    fileTree->setModel(proxy);
    const QModelIndex rootIdx = fsModel->setRootPath(index->root());
    fileTree->setRootIndex(proxy->mapFromSource(rootIdx));
    for (int c = 1; c < 4; ++c)
        fileTree->hideColumn(c);
    refreshTags();
}

void VaultDock::chooseVault()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose the vault folder"), index->root());
    if (dir.isEmpty())
        return;

    QSettings().setValue(QStringLiteral("NotepadNext/VaultPath"), dir);
    index->setRoot(dir);
    vaultChanged();
    show();
    raise();
}

void VaultDock::newNote()
{
    if (!index->hasRoot()) {
        chooseVault();
        if (!index->hasRoot())
            return;
    }

    // Cria a nota na pasta selecionada (ou na raiz do vault)
    QString folder = index->root();
    const QModelIndex current = fileTree->currentIndex();
    if (current.isValid()) {
        const QFileInfo info(fsModel->filePath(proxy->mapToSource(current)));
        folder = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New note"), tr("Note name:"), QLineEdit::Normal, tr("Untitled"), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    const QString rel = QDir(index->root()).relativeFilePath(QDir(folder).filePath(name));
    const QString path = index->createNote(rel);
    if (!path.isEmpty())
        emit NotepadNextHub::instance()->openFileRequested(path, -1);
}

void VaultDock::refreshTags()
{
    // Lembra quais tags estavam abertas
    QSet<QString> expanded;
    for (QTreeWidgetItemIterator it(tagTree); *it; ++it) {
        if ((*it)->isExpanded())
            expanded.insert((*it)->data(0, Qt::UserRole).toString());
    }

    tagTree->clear();
    const QMap<QString, QStringList> tags = index->tags();
    QHash<QString, QTreeWidgetItem *> items;

    const QColor tagColor = QColor::fromRgb(NotepadNextHub::TagColor);

    // Cria a árvore de tags aninhadas: #projeto/NotepadNext vira projeto > NotepadNext
    std::function<QTreeWidgetItem *(const QString &)> itemFor = [&](const QString &tag) -> QTreeWidgetItem * {
        if (items.contains(tag))
            return items.value(tag);
        const int slash = tag.lastIndexOf(QLatin1Char('/'));
        QTreeWidgetItem *item = slash > 0 ? new QTreeWidgetItem(itemFor(tag.left(slash))) : new QTreeWidgetItem(tagTree);
        item->setText(0, QLatin1Char('#') + tag.mid(slash + 1));
        item->setForeground(0, tagColor);
        item->setData(0, Qt::UserRole, tag);
        items.insert(tag, item);
        return item;
    };

    for (auto it = tags.constBegin(); it != tags.constEnd(); ++it) {
        QTreeWidgetItem *tagItem = itemFor(it.key());
        tagItem->setText(1, QString::number(it.value().size()));
        tagItem->setForeground(1, palette().color(QPalette::PlaceholderText));
        for (const QString &path : it.value()) {
            auto *noteItem = new QTreeWidgetItem(tagItem);
            noteItem->setText(0, QFileInfo(path).completeBaseName());
            noteItem->setToolTip(0, QDir(index->root()).relativeFilePath(path));
            noteItem->setData(0, Qt::UserRole + 1, path);
        }
    }

    for (QTreeWidgetItem *item : std::as_const(items)) {
        if (expanded.contains(item->data(0, Qt::UserRole).toString()))
            item->setExpanded(true);
    }
}

void VaultDock::showTag(const QString &tag)
{
    show();
    raise();
    for (QTreeWidgetItemIterator it(tagTree); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toString().compare(tag, Qt::CaseInsensitive) == 0) {
            for (QTreeWidgetItem *p = (*it)->parent(); p; p = p->parent())
                p->setExpanded(true);
            (*it)->setExpanded(true);
            tagTree->setCurrentItem(*it);
            tagTree->scrollToItem(*it, QAbstractItemView::PositionAtTop);
            return;
        }
    }
}

void VaultDock::selectFile(const QString &path)
{
    if (!index->contains(path) || !fileTree->model())
        return;
    const QModelIndex idx = proxy->mapFromSource(fsModel->index(path));
    if (idx.isValid()) {
        fileTree->setCurrentIndex(idx);
        fileTree->scrollTo(idx);
    }
}
