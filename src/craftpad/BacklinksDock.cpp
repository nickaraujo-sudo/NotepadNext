/*
 * CraftPad - extensão do Notepad Next no estilo Obsidian.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#include "BacklinksDock.h"
#include "CraftPadHub.h"
#include "VaultIndex.h"

#include "MainWindow.h"
#include "ScintillaNext.h"

#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

static QString lineOf(const QString &path, int line)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    return line < lines.size() ? lines.at(line).trimmed() : QString();
}

BacklinksDock::BacklinksDock(MainWindow *parent) :
    QDockWidget(tr("Backlinks"), parent),
    window(parent)
{
    setObjectName(QStringLiteral("CraftPadBacklinksDock"));

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    title = new QLabel(container);
    title->setContentsMargins(8, 6, 8, 6);
    title->setWordWrap(true);
    layout->addWidget(title);

    tree = new QTreeWidget(container);
    tree->setHeaderHidden(true);
    tree->setWordWrap(true);
    layout->addWidget(tree, 1);
    setWidget(container);

    connect(tree, &QTreeWidget::itemActivated, this, [](QTreeWidgetItem *item) {
        const QString path = item->data(0, Qt::UserRole).toString();
        const QString target = item->data(0, Qt::UserRole + 2).toString();
        if (!path.isEmpty())
            emit CraftPadHub::instance()->openFileRequested(path, item->data(0, Qt::UserRole + 1).toInt());
        else if (!target.isEmpty())
            emit CraftPadHub::instance()->openNoteRequested(target, QString());
    });

    connect(CraftPadHub::instance()->index(), &VaultIndex::indexChanged, this, &BacklinksDock::refresh);
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            setEditor(window->currentEditor());
            connect(window, &MainWindow::editorActivated, this, &BacklinksDock::setEditor);
        }
        else {
            disconnect(window, &MainWindow::editorActivated, this, &BacklinksDock::setEditor);
        }
    });
}

void BacklinksDock::setEditor(ScintillaNext *newEditor)
{
    editor = newEditor;
    refresh();
}

void BacklinksDock::refresh()
{
    if (!isVisible())
        return;

    tree->clear();
    VaultIndex *index = CraftPadHub::instance()->index();

    if (!editor || !editor->isFile() || !index->contains(editor->getFilePath())) {
        title->setText(index->hasRoot() ? tr("Abra uma nota do vault para ver os backlinks.")
                                        : tr("Escolha a pasta do vault no painel Vault."));
        return;
    }

    const QString path = editor->getFilePath();
    const QColor linkColor = QColor::fromRgb(CraftPadHub::LinkColor);
    const QColor dim = palette().color(QPalette::PlaceholderText);

    // Backlinks: notas que apontam para esta, agrupadas por nota
    const QList<QPair<QString, int>> links = index->backlinks(path);
    auto *incoming = new QTreeWidgetItem(tree, {tr("Backlinks (%1)").arg(links.size())});
    QFont bold = incoming->font(0);
    bold.setBold(true);
    incoming->setFont(0, bold);

    QTreeWidgetItem *group = nullptr;
    QString lastPath;
    for (const auto &link : links) {
        if (link.first != lastPath) {
            lastPath = link.first;
            group = new QTreeWidgetItem(incoming, {QFileInfo(link.first).completeBaseName()});
            group->setForeground(0, linkColor);
            group->setData(0, Qt::UserRole, link.first);
            group->setData(0, Qt::UserRole + 1, -1);
            group->setExpanded(true);
        }
        auto *snippet = new QTreeWidgetItem(group, {lineOf(link.first, link.second)});
        snippet->setData(0, Qt::UserRole, link.first);
        snippet->setData(0, Qt::UserRole + 1, link.second);
    }
    incoming->setExpanded(true);

    // Links de saída: para onde esta nota aponta (os que ainda não existem ficam apagados)
    const NoteInfo info = index->notes().value(path);
    QStringList seen;
    auto *outgoing = new QTreeWidgetItem(tree);
    outgoing->setFont(0, bold);
    for (const NoteLink &link : info.links) {
        if (seen.contains(link.target))
            continue;
        seen << link.target;
        const QString resolved = index->resolveLink(link.target);
        const QString label = link.target.startsWith(QLatin1Char('\x01')) ? QFileInfo(link.target.mid(1)).completeBaseName() : link.target;
        auto *item = new QTreeWidgetItem(outgoing, {resolved.isEmpty() ? label + tr("  (criar)") : label});
        item->setForeground(0, resolved.isEmpty() ? dim : linkColor);
        if (resolved.isEmpty())
            item->setData(0, Qt::UserRole + 2, link.target);
        else
            item->setData(0, Qt::UserRole, resolved);
        item->setData(0, Qt::UserRole + 1, -1);
    }
    outgoing->setText(0, tr("Links de saída (%1)").arg(seen.size()));
    outgoing->setExpanded(true);

    title->setText(tr("<b>%1</b>").arg(info.name.toHtmlEscaped()));
}
