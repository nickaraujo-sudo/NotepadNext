/*
 * NotepadNext - recursos de notas no estilo Obsidian (vault, links, tags, grafo).
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#ifndef NOTEPADNEXTHUB_H
#define NOTEPADNEXTHUB_H

#include <QObject>

class VaultIndex;

// Ponto central do NotepadNext: guarda o índice do vault e transmite
// pedidos de "abrir nota" e "mostrar tag" entre editor, painéis e grafo.
class NotepadNextHub : public QObject
{
    Q_OBJECT

public:
    static NotepadNextHub *instance();
    VaultIndex *index() const { return vaultIndex; }

    // Traz configurações, pastas e arquivos do nome antigo do app (craftpad)
    // para os nomes atuais (NotepadNext). Roda uma vez, na abertura.
    static void migrateLegacyNames();

    // File formats shown in the sidebar file tree ("md", "txt", "csv"). Markdown is always shown.
    static QStringList availableSidebarFormats();
    static QStringList sidebarFormats();
    void setSidebarFormats(const QStringList &formats);

    // Cores do NotepadNext, usadas no editor, na prévia e no grafo
    static constexpr unsigned int LinkColor = 0x9B6BE0; // roxo do logo
    static constexpr unsigned int TagColor = 0xFF2E97;  // rosa neon

signals:
    void openNoteRequested(const QString &target, const QString &fromPath);
    void openFileRequested(const QString &path, int line);
    void tagRequested(const QString &tag);
    void vaultRequested();
    void sidebarFormatsChanged(const QStringList &formats);

private:
    explicit NotepadNextHub(QObject *parent = nullptr);
    VaultIndex *vaultIndex;
};

#endif // NOTEPADNEXTHUB_H
