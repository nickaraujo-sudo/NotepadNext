/*
 * CraftPad - extensão do Notepad Next no estilo Obsidian.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#ifndef VAULTINDEX_H
#define VAULTINDEX_H

#include <QHash>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStringList>

class QFileSystemWatcher;
class QTimer;

// Um link [[...]] encontrado numa nota
struct NoteLink {
    QString target;   // texto do link, sem alias/âncora ("Pasta/Nota")
    int line = 0;     // linha (0-based) onde aparece
};

// Dados extraídos de uma nota Markdown
struct NoteInfo {
    QString path;              // caminho absoluto
    QString name;              // nome do arquivo sem extensão
    QStringList tags;          // tags sem o '#', ex. "projeto/craftpad"
    QList<NoteLink> links;     // links [[...]] e [x](nota.md)
};

// Índice do vault: lê todas as notas .md da pasta e mantém
// tags, links e backlinks atualizados (como o Obsidian).
class VaultIndex : public QObject
{
    Q_OBJECT

public:
    explicit VaultIndex(QObject *parent = nullptr);

    void setRoot(const QString &path);
    QString root() const { return rootPath; }
    bool hasRoot() const { return !rootPath.isEmpty(); }
    bool contains(const QString &path) const;

    // Consultas
    const QHash<QString, NoteInfo> &notes() const { return noteMap; }
    QStringList noteNames() const;                       // para autocompletar [[
    QMap<QString, QStringList> tags() const;             // tag -> notas (inclui pais de tags aninhadas)
    QString resolveLink(const QString &target) const;    // nome -> caminho ("" se não existe)
    QList<QPair<QString, int>> backlinks(const QString &path) const; // (nota de origem, linha)

    // Cria a nota de um link que ainda não existe e devolve o caminho
    QString createNote(const QString &target);

    // Usado por testes e pelo índice
    static NoteInfo parse(const QString &path, const QString &text);

public slots:
    void rescan();
    void updateNote(const QString &path, const QString &text); // atualização ao vivo

signals:
    void indexChanged();

private:
    void watchDirectories();

    QString rootPath;
    QHash<QString, NoteInfo> noteMap;
    QHash<QString, QString> nameLookup;  // nome/caminho relativo em minúsculas -> caminho
    QFileSystemWatcher *watcher;
    QTimer *rescanTimer;
};

#endif // VAULTINDEX_H
