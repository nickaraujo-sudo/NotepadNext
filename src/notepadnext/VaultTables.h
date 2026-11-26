/*
 * NotepadNext - recursos próprios deste fork.
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#ifndef VAULTTABLES_H
#define VAULTTABLES_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

#include "MarkdownTable.h"

// Tabelas nomeadas de todas as notas do vault, para fórmulas que
// buscam dados em outras notas (como vínculos entre arquivos do Excel).
class VaultTables
{
public:
    static VaultTables &instance();

    // Procura a tabela pelo nome nas notas do vault (exceto 'excludePath')
    const MarkdownTable *find(const QString &name, const QString &excludePath = QString());
    // Nota onde a tabela está ("" se não existe)
    QString noteOf(const QString &name, const QString &excludePath = QString());

private:
    struct Entry {
        QDateTime modified;
        QList<MarkdownTable> tables;
    };
    const Entry &load(const QString &path);
    QHash<QString, Entry> cache;
};

#endif // VAULTTABLES_H
