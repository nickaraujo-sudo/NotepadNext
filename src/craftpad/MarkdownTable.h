/*
 * craftpad++ - extensão do Notepad Next.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#ifndef MARKDOWNTABLE_H
#define MARKDOWNTABLE_H

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

// Sombreamento de um intervalo de células, ex. "A1:C1" -> #E8E0F8
struct CellShade {
    QString range;   // "B2" ou "A1:C1"
    QColor color;
};

// Propriedades da tabela, guardadas num comentário HTML logo acima dela:
// <!-- craftpad-tabela nome="Vendas" borda="#B8B8C8 solid 1" sombra="A1:C1=#EEE8FA B2=#FFF3C4" -->
// O comentário é invisível no Obsidian e no GitHub.
struct TableMeta {
    QString name;
    QColor borderColor;
    QString borderStyle = QStringLiteral("solid"); // solid, dashed, dotted, double, none
    int borderWidth = 1;
    QList<CellShade> shading;

    bool hasBorderSettings() const { return borderColor.isValid() || borderStyle != QLatin1String("solid") || borderWidth != 1; }
    QString toComment() const;
    static bool parseComment(const QString &line, TableMeta *out);

    // Cor de fundo da célula (linha e coluna começam em 0), inválida se não houver
    QColor shadeFor(int row, int col) const;
    void setShade(int row1, int col1, int row2, int col2, const QColor &color);
};

// Uma tabela Markdown (pipe table) encontrada no texto
struct MarkdownTable {
    int metaLine = -1;     // linha do comentário de propriedades (-1 se não há)
    int firstLine = 0;     // linha do cabeçalho
    int lastLine = 0;      // última linha da tabela (inclusive)
    TableMeta meta;
    QVector<QStringList> rows;   // rows[0] é o cabeçalho; a linha separadora não entra
    QStringList aligns;          // "", "left", "center", "right" por coluna

    int rowCount() const { return rows.size(); }
    int columnCount() const;
    QString cell(int row, int col) const;
    int startLine() const { return metaLine >= 0 ? metaLine : firstLine; }

    // Gera o Markdown da tabela (comentário + linhas), sem quebra de linha final
    QString toMarkdown() const;
    void normalize(); // todas as linhas com o mesmo número de colunas

    static bool isTableLine(const QString &line);
    static bool isSeparatorLine(const QString &line);
    static QStringList splitRow(const QString &line);
    // Índice da coluna (0-based) na posição 'column' (em caracteres) de uma linha
    static int columnAt(const QString &line, int column);
};

// Encontra todas as tabelas de um texto (ignora blocos de código)
QList<MarkdownTable> findMarkdownTables(const QStringList &lines);

// "A" -> 0, "AB" -> 27; -1 se inválido
int columnFromLetters(const QString &letters);
QString lettersFromColumn(int col);
// "B3" -> (2, 1); false se inválido
bool parseCellRef(const QString &ref, int *row, int *col);
QString cellRefName(int row, int col);

#endif // MARKDOWNTABLE_H
