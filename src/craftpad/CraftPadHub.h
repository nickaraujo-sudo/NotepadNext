/*
 * CraftPad - extensão do Notepad Next no estilo Obsidian.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#ifndef CRAFTPADHUB_H
#define CRAFTPADHUB_H

#include <QObject>

class VaultIndex;

// Ponto central do CraftPad: guarda o índice do vault e transmite
// pedidos de "abrir nota" e "mostrar tag" entre editor, painéis e grafo.
class CraftPadHub : public QObject
{
    Q_OBJECT

public:
    static CraftPadHub *instance();
    VaultIndex *index() const { return vaultIndex; }

    // Cores do CraftPad, usadas no editor, na prévia e no grafo
    static constexpr unsigned int LinkColor = 0x9B6BE0; // roxo do logo
    static constexpr unsigned int TagColor = 0xFF2E97;  // rosa neon

signals:
    void openNoteRequested(const QString &target, const QString &fromPath);
    void openFileRequested(const QString &path, int line);
    void tagRequested(const QString &tag);
    void vaultRequested();

private:
    explicit CraftPadHub(QObject *parent = nullptr);
    VaultIndex *vaultIndex;
};

#endif // CRAFTPADHUB_H
