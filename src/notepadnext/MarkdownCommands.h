/*
 * NotepadNext - recursos próprios deste fork.
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#ifndef MARKDOWNCOMMANDS_H
#define MARKDOWNCOMMANDS_H

#include <QString>

#include "MarkdownTable.h"

class ScintillaNext;
class QWidget;

// Comandos de formatação que alteram o texto Markdown no editor.
namespace MarkdownCommands
{
    // Negrito, itálico etc.: envolve a seleção (ou remove, se já estiver)
    void toggleWrap(ScintillaNext *editor, const QString &open, const QString &close);

    enum class ListKind { Bullet, Numbered, Task };
    void toggleList(ScintillaNext *editor, ListKind kind);
    void toggleTaskDone(ScintillaNext *editor);
    void indentLines(ScintillaNext *editor, bool indent);
    void setHeading(ScintillaNext *editor, int level); // 0 = texto normal

    // "left", "center", "right" ou "justify"
    void setAlignment(ScintillaNext *editor, const QString &alignment);

    // Tabelas
    struct TableContext {
        bool valid = false;
        MarkdownTable table;
        int row = 0;
        int col = 0;
        int row2 = 0;   // fim da seleção (para sombreamento)
        int col2 = 0;
    };
    TableContext tableAtCaret(ScintillaNext *editor);
    bool selectTableCell(ScintillaNext *editor, int line, int row, int col); // coloca o cursor numa célula

    void insertTable(ScintillaNext *editor, QWidget *parent);
    void insertRow(ScintillaNext *editor, bool below);
    void deleteRow(ScintillaNext *editor);
    void insertColumn(ScintillaNext *editor, bool right);
    void deleteColumn(ScintillaNext *editor);
    void deleteTable(ScintillaNext *editor);
    void tableProperties(ScintillaNext *editor, QWidget *parent);
    void shadeCells(ScintillaNext *editor, QWidget *parent);
    void showFormulaHelp(QWidget *parent);

    // Utilidades
    QString lineText(ScintillaNext *editor, int line);
    void replaceLines(ScintillaNext *editor, int firstLine, int lastLine, const QString &text);
    void insertTextAt(ScintillaNext *editor, int position, const QString &text);
    QString eol(ScintillaNext *editor);
}

#endif // MARKDOWNCOMMANDS_H
