/*
 * NotepadNext - recursos próprios deste fork.
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#ifndef FORMULAENGINE_H
#define FORMULAENGINE_H

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

struct MarkdownTable;

// Valor de uma célula ou de uma expressão
struct FormulaValue {
    enum Type { Empty, Number, Text, Bool, Error };
    Type type = Empty;
    double number = 0;
    QString text;   // texto, ou código do erro (#DIV/0!, #REF!, ...)
    bool boolean = false;

    static FormulaValue num(double v) { FormulaValue x; x.type = Number; x.number = v; return x; }
    static FormulaValue str(const QString &s) { FormulaValue x; x.type = Text; x.text = s; return x; }
    static FormulaValue logical(bool b) { FormulaValue x; x.type = Bool; x.boolean = b; return x; }
    static FormulaValue error(const QString &code) { FormulaValue x; x.type = Error; x.text = code; return x; }
    bool isError() const { return type == Error; }
};

// Calcula fórmulas do tipo Excel nas células das tabelas.
// Endereços: coluna em letra e linha em número, com o cabeçalho na linha 1
// (A1 é a primeira célula do cabeçalho). Outras tabelas: Vendas!B2, 'Minha tabela'!A1:A9.
class FormulaEngine
{
public:
    using TableLookup = std::function<const MarkdownTable *(const QString &name)>;

    explicit FormulaEngine(TableLookup lookup);

    // Texto que deve aparecer na célula (o resultado, se for fórmula)
    QString displayValue(const MarkdownTable *table, int row, int col);
    FormulaValue cellValue(const MarkdownTable *table, int row, int col);
    // Avalia uma fórmula avulsa ("=SOMA(A2:A5)") no contexto de uma tabela
    FormulaValue evaluate(const QString &formula, const MarkdownTable *context);

    static bool isFormula(const QString &cellText);
    static QString format(const FormulaValue &v);
    static bool parseNumber(const QString &text, double *out);
    static QStringList functionNames();

    // Cell whose formula is being evaluated (used by B2# and B:B to stop above the result cell)
    struct CurrentCell { const MarkdownTable *table = nullptr; int row = -1; int col = -1; };
    CurrentCell currentCell() const { return cellStack.isEmpty() ? CurrentCell() : cellStack.last(); }

private:
    QList<CurrentCell> cellStack;
    TableLookup lookup;
    QHash<QString, FormulaValue> cache;
    QSet<QString> evaluating;
};

#endif // FORMULAENGINE_H
