/*
 * craftpad++ - extensão do Notepad Next.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#include "VaultTables.h"
#include "CraftPadHub.h"
#include "VaultIndex.h"

#include <QFile>
#include <QFileInfo>

VaultTables &VaultTables::instance()
{
    static VaultTables tables;
    return tables;
}

const VaultTables::Entry &VaultTables::load(const QString &path)
{
    const QDateTime modified = QFileInfo(path).lastModified();
    auto it = cache.find(path);
    if (it != cache.end() && it->modified == modified)
        return *it;

    Entry e;
    e.modified = modified;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray data = f.readAll();
        // Só lê as tabelas de notas que têm alguma tabela nomeada
        if (data.contains("craftpad-tabela")) {
            QString text = QString::fromUtf8(data);
            text.remove(QLatin1Char('\r'));
            e.tables = findMarkdownTables(text.split(QLatin1Char('\n')));
        }
    }
    cache.insert(path, e);
    return cache[path];
}

const MarkdownTable *VaultTables::find(const QString &name, const QString &excludePath)
{
    VaultIndex *index = CraftPadHub::instance()->index();
    if (!index || name.isEmpty())
        return nullptr;

    for (auto it = index->notes().constBegin(); it != index->notes().constEnd(); ++it) {
        if (it.key() == excludePath)
            continue;
        const Entry &e = load(it.key());
        for (const MarkdownTable &t : e.tables)
            if (QString::compare(t.meta.name, name, Qt::CaseInsensitive) == 0)
                return &t;
    }
    return nullptr;
}

QString VaultTables::noteOf(const QString &name, const QString &excludePath)
{
    VaultIndex *index = CraftPadHub::instance()->index();
    if (!index || name.isEmpty())
        return QString();
    for (auto it = index->notes().constBegin(); it != index->notes().constEnd(); ++it) {
        if (it.key() == excludePath)
            continue;
        for (const MarkdownTable &t : load(it.key()).tables)
            if (QString::compare(t.meta.name, name, Qt::CaseInsensitive) == 0)
                return it.key();
    }
    return QString();
}
