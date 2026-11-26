/*
 * craftpad++ - extensão do Notepad Next.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#ifndef IMAGESUPPORT_H
#define IMAGESUPPORT_H

#include <QString>

class QMimeData;
class QWidget;
class ScintillaNext;

// Imagens nas notas: arrastar do Finder ou do navegador, inserir pelo
// menu e ajustar tamanho e posição em relação ao texto.
namespace ImageSupport
{
    bool canHandle(const QMimeData *mime);
    // Salva/copia a imagem para a pasta "anexos" da nota e insere a marcação na posição
    bool insertFromMime(ScintillaNext *editor, const QMimeData *mime, int position);
    void insertFromFile(ScintillaNext *editor, QWidget *parent);
    // Abre as propriedades da imagem na linha do cursor
    void editProperties(ScintillaNext *editor, QWidget *parent);
    // Seleciona a imagem com esse caminho (como aparece no arquivo) e abre as propriedades
    void editImageAt(ScintillaNext *editor, QWidget *parent, int line, const QString &markup);

    // Encontra a imagem pelo nome: ao lado da nota, na raiz do vault ou em qualquer pasta do vault
    QString resolveAttachment(const QString &name, const QString &noteDir);
}

#endif // IMAGESUPPORT_H
