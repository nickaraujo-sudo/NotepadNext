/*
 * craftpad++ - extensão do Notepad Next.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#include "MarkdownCommands.h"
#include "FormulaEngine.h"
#include "ScintillaNext.h"
#include "VaultTables.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>

namespace MarkdownCommands
{

static QByteArray utf8(const QString &s) { return s.toUtf8(); }

QString eol(ScintillaNext *editor)
{
    switch (editor->eOLMode()) {
    case 0: return QStringLiteral("\r\n"); // SC_EOL_CRLF
    case 1: return QStringLiteral("\r");   // SC_EOL_CR
    default: return QStringLiteral("\n");
    }
}

static QString withEol(ScintillaNext *editor, QString text)
{
    const QString e = eol(editor);
    if (e != QLatin1String("\n"))
        text.replace(QLatin1Char('\n'), e);
    return text;
}

QString lineText(ScintillaNext *editor, int line)
{
    return QString::fromUtf8(editor->textRange(editor->positionFromLine(line), editor->lineEndPosition(line)));
}

void replaceLines(ScintillaNext *editor, int firstLine, int lastLine, const QString &text)
{
    const QByteArray bytes = utf8(withEol(editor, text));
    editor->setTargetRange(editor->positionFromLine(firstLine), editor->lineEndPosition(lastLine));
    editor->replaceTarget(bytes.size(), bytes.constData());
}

void insertTextAt(ScintillaNext *editor, int position, const QString &text)
{
    editor->insertText(position, utf8(withEol(editor, text)).constData());
}

static QStringList allLines(ScintillaNext *editor)
{
    QString text = QString::fromUtf8(editor->getText(editor->textLength() + 1));
    text.remove(QLatin1Char('\r'));
    return text.split(QLatin1Char('\n'));
}

// Coluna (em caracteres) de uma posição do editor
static int charColumn(ScintillaNext *editor, int pos)
{
    const int line = editor->lineFromPosition(pos);
    return QString::fromUtf8(editor->textRange(editor->positionFromLine(line), pos)).size();
}

// Posição do editor de uma coluna (em caracteres) numa linha
static int positionAt(ScintillaNext *editor, int line, int charCol)
{
    const QString text = lineText(editor, line);
    return editor->positionFromLine(line) + text.left(charCol).toUtf8().size();
}

// ---------------------------------------------------------------------------
// Negrito, itálico, ...
// ---------------------------------------------------------------------------

void toggleWrap(ScintillaNext *editor, const QString &open, const QString &close)
{
    const QByteArray o = utf8(open), c = utf8(close);
    const int start = editor->selectionStart();
    const int end = editor->selectionEnd();

    editor->beginUndoAction();
    const QByteArray before = editor->textRange(qMax(0, start - int(o.size())), start);
    const QByteArray after = editor->textRange(end, qMin(int(editor->textLength()), end + int(c.size())));

    if (before == o && after == c) {
        // Já está formatado: remove os marcadores ao redor
        editor->setTargetRange(end, end + c.size());
        editor->replaceTarget(0, "");
        editor->setTargetRange(start - o.size(), start);
        editor->replaceTarget(0, "");
        editor->setSel(start - o.size(), end - o.size());
    }
    else if (start != end) {
        const QByteArray sel = editor->textRange(start, end);
        if (sel.startsWith(o) && sel.endsWith(c) && sel.size() >= o.size() + c.size()) {
            const QByteArray inner = sel.mid(o.size(), sel.size() - o.size() - c.size());
            editor->setTargetRange(start, end);
            editor->replaceTarget(inner.size(), inner.constData());
            editor->setSel(start, start + inner.size());
        }
        else {
            // Espaços nas pontas ficam fora dos marcadores
            int a = 0, z = sel.size();
            while (a < z && (sel.at(a) == ' ' || sel.at(a) == '\t')) ++a;
            while (z > a && (sel.at(z - 1) == ' ' || sel.at(z - 1) == '\t' || sel.at(z - 1) == '\n' || sel.at(z - 1) == '\r')) --z;
            const QByteArray wrapped = sel.left(a) + o + sel.mid(a, z - a) + c + sel.mid(z);
            editor->setTargetRange(start, end);
            editor->replaceTarget(wrapped.size(), wrapped.constData());
            editor->setSel(start + a + o.size(), start + o.size() + z);
        }
    }
    else {
        const QByteArray both = o + c;
        editor->insertText(start, both.constData());
        editor->setEmptySelection(start + o.size());
    }
    editor->endUndoAction();
}

// ---------------------------------------------------------------------------
// Listas e títulos
// ---------------------------------------------------------------------------

static const QRegularExpression &prefixRe()
{
    // indentação, marcador (-, *, +, 1.), caixa [ ] opcional
    static const QRegularExpression r(QStringLiteral("^(\\s*)(?:([-*+])\\s+(\\[[ xX]\\]\\s+)?|(\\d+)[.)]\\s+)?(.*)$"));
    return r;
}

static void selectedLines(ScintillaNext *editor, int *first, int *last)
{
    const int start = editor->selectionStart();
    int end = editor->selectionEnd();
    *first = editor->lineFromPosition(start);
    *last = editor->lineFromPosition(end);
    // Seleção que termina no começo de uma linha não inclui essa linha
    if (*last > *first && end == editor->positionFromLine(*last))
        --*last;
}

void toggleList(ScintillaNext *editor, ListKind kind)
{
    int first, last;
    selectedLines(editor, &first, &last);

    QStringList lines;
    bool allKind = true;
    for (int l = first; l <= last; ++l) {
        const QString t = lineText(editor, l);
        lines << t;
        if (t.trimmed().isEmpty() && first != last) continue;
        const auto m = prefixRe().match(t);
        const bool isTask = !m.captured(3).isEmpty();
        const bool isBullet = !m.captured(2).isEmpty() && !isTask;
        const bool isNumber = !m.captured(4).isEmpty();
        if ((kind == ListKind::Task && !isTask) || (kind == ListKind::Bullet && !isBullet) || (kind == ListKind::Numbered && !isNumber))
            allKind = false;
    }

    int number = 1;
    for (QString &t : lines) {
        if (t.trimmed().isEmpty() && first != last) continue;
        const auto m = prefixRe().match(t);
        const QString indent = m.captured(1);
        const QString content = m.captured(5);
        if (allKind) {
            t = indent + content;
        }
        else {
            switch (kind) {
            case ListKind::Bullet: t = indent + QStringLiteral("- ") + content; break;
            case ListKind::Numbered: t = indent + QStringLiteral("%1. ").arg(number++) + content; break;
            case ListKind::Task: t = indent + QStringLiteral("- [ ] ") + content; break;
            }
        }
    }

    editor->beginUndoAction();
    replaceLines(editor, first, last, lines.join(QLatin1Char('\n')));
    editor->endUndoAction();
    editor->setEmptySelection(editor->lineEndPosition(last));
}

void toggleTaskDone(ScintillaNext *editor)
{
    int first, last;
    selectedLines(editor, &first, &last);
    QStringList lines;
    static const QRegularExpression box(QStringLiteral("^(\\s*[-*+]\\s+)\\[([ xX])\\]"));
    for (int l = first; l <= last; ++l) {
        QString t = lineText(editor, l);
        const auto m = box.match(t);
        if (m.hasMatch()) {
            const bool done = m.captured(2) != QLatin1String(" ");
            t.replace(m.capturedStart(2), 1, done ? QStringLiteral(" ") : QStringLiteral("x"));
        }
        else if (!t.trimmed().isEmpty()) {
            // Ainda não é tarefa: vira tarefa
            const auto p = prefixRe().match(t);
            t = p.captured(1) + QStringLiteral("- [ ] ") + p.captured(5);
        }
        lines << t;
    }
    const int caret = editor->currentPos();
    editor->beginUndoAction();
    replaceLines(editor, first, last, lines.join(QLatin1Char('\n')));
    editor->endUndoAction();
    editor->setEmptySelection(qMin(caret, int(editor->lineEndPosition(last))));
}

void indentLines(ScintillaNext *editor, bool indent)
{
    int first, last;
    selectedLines(editor, &first, &last);
    QStringList lines;
    for (int l = first; l <= last; ++l) {
        QString t = lineText(editor, l);
        if (indent) {
            if (!t.trimmed().isEmpty()) t.prepend(QStringLiteral("    "));
        }
        else if (t.startsWith(QLatin1Char('\t'))) {
            t.remove(0, 1);
        }
        else {
            int n = 0;
            while (n < 4 && n < t.size() && t.at(n) == QLatin1Char(' ')) ++n;
            t.remove(0, n);
        }
        lines << t;
    }
    editor->beginUndoAction();
    replaceLines(editor, first, last, lines.join(QLatin1Char('\n')));
    editor->endUndoAction();
    editor->setSel(editor->positionFromLine(first), editor->lineEndPosition(last));
}

void setHeading(ScintillaNext *editor, int level)
{
    int first, last;
    selectedLines(editor, &first, &last);
    static const QRegularExpression h(QStringLiteral("^#{1,6}\\s+"));
    QStringList lines;
    for (int l = first; l <= last; ++l) {
        QString t = lineText(editor, l);
        t.remove(h);
        if (level > 0 && !t.trimmed().isEmpty())
            t = QString(level, QLatin1Char('#')) + QLatin1Char(' ') + t;
        lines << t;
    }
    editor->beginUndoAction();
    replaceLines(editor, first, last, lines.join(QLatin1Char('\n')));
    editor->endUndoAction();
    editor->setEmptySelection(editor->lineEndPosition(last));
}

// ---------------------------------------------------------------------------
// Alinhamento: <div align="center"> ... </div>
// ---------------------------------------------------------------------------

void setAlignment(ScintillaNext *editor, const QString &alignment)
{
    static const QRegularExpression divRe(QStringLiteral("^\\s*<div\\s+align=\"(left|center|right|justify)\"\\s*>\\s*$"), QRegularExpression::CaseInsensitiveOption);
    const int count = editor->lineCount();
    int first, last;
    selectedLines(editor, &first, &last);

    auto blank = [&](int l) { return lineText(editor, l).trimmed().isEmpty(); };

    // Parágrafo ao redor do cursor
    while (first > 0 && !blank(first - 1) && !divRe.match(lineText(editor, first - 1)).hasMatch()) --first;
    while (last + 1 < count && !blank(last + 1) && !lineText(editor, last + 1).trimmed().startsWith(QLatin1String("</div"))) ++last;

    // Já está dentro de um <div align>?
    int open = first - 1;
    while (open >= 0 && blank(open)) --open;
    int close = last + 1;
    while (close < count && blank(close)) ++close;
    const bool wrapped = open >= 0 && close < count && divRe.match(lineText(editor, open)).hasMatch()
            && lineText(editor, close).trimmed().startsWith(QLatin1String("</div"), Qt::CaseInsensitive);

    editor->beginUndoAction();
    if (wrapped) {
        if (alignment == QLatin1String("left")) {
            // Remove o envoltório: fica só o conteúdo
            QStringList inner;
            for (int l = first; l <= last; ++l) inner << lineText(editor, l);
            replaceLines(editor, open, close, inner.join(QLatin1Char('\n')));
        }
        else {
            replaceLines(editor, open, open, QStringLiteral("<div align=\"%1\">").arg(alignment));
        }
    }
    else if (alignment != QLatin1String("left")) {
        const QString endTag = QStringLiteral("\n\n</div>");
        insertTextAt(editor, editor->lineEndPosition(last), endTag);
        insertTextAt(editor, editor->positionFromLine(first), QStringLiteral("<div align=\"%1\">\n\n").arg(alignment));
    }
    editor->endUndoAction();
}

// ---------------------------------------------------------------------------
// Tabelas
// ---------------------------------------------------------------------------

TableContext tableAtCaret(ScintillaNext *editor)
{
    TableContext ctx;
    const QStringList lines = allLines(editor);
    const int caretLine = editor->lineFromPosition(editor->currentPos());
    const int anchorLine = editor->lineFromPosition(editor->selectionStart() == editor->currentPos() ? editor->selectionEnd() : editor->selectionStart());

    for (const MarkdownTable &t : findMarkdownTables(lines)) {
        if (caretLine < t.startLine() || caretLine > t.lastLine)
            continue;
        ctx.valid = true;
        ctx.table = t;
        auto rowOf = [&](int line) {
            if (line <= t.firstLine + 1) return 0;
            return qMin(line - t.firstLine - 1, t.rowCount() - 1);
        };
        auto colOf = [&](int pos) {
            const int line = editor->lineFromPosition(pos);
            if (line < t.firstLine || line == t.firstLine + 1) return 0;
            return qMin(MarkdownTable::columnAt(lines.value(line), charColumn(editor, pos)), qMax(0, t.columnCount() - 1));
        };
        ctx.row = rowOf(caretLine);
        ctx.col = colOf(editor->currentPos());
        const int otherPos = editor->currentPos() == editor->selectionStart() ? editor->selectionEnd() : editor->selectionStart();
        if (anchorLine >= t.startLine() && anchorLine <= t.lastLine) {
            ctx.row2 = rowOf(anchorLine);
            ctx.col2 = colOf(otherPos);
        }
        else {
            ctx.row2 = ctx.row;
            ctx.col2 = ctx.col;
        }
        break;
    }
    return ctx;
}

bool selectTableCell(ScintillaNext *editor, int line, int row, int col)
{
    // line = primeira linha do cabeçalho; a linha 1 da tabela é o separador
    const int target = line + (row == 0 ? 0 : row + 1);
    if (target >= editor->lineCount())
        return false;
    const QString text = lineText(editor, target);
    int pipes = 0;
    const bool leading = text.trimmed().startsWith(QLatin1Char('|'));
    int idx = leading ? text.indexOf(QLatin1Char('|')) + 1 : 0;
    while (pipes < col && idx < text.size()) {
        if (text.at(idx) == QLatin1Char('\\')) { idx += 2; continue; }
        if (text.at(idx) == QLatin1Char('|')) ++pipes;
        ++idx;
    }
    // Pula o espaço depois do | (e o resto, se a célula tem texto)
    int j = idx;
    while (j < text.size() && text.at(j) == QLatin1Char(' ')) ++j;
    idx = (j < text.size() && text.at(j) != QLatin1Char('|')) ? j : qMin(int(text.size()), idx + 1);
    // Seleciona o conteúdo da célula: digitar substitui
    int end = idx;
    while (end < text.size() && text.at(end) != QLatin1Char('|')) {
        if (text.at(end) == QLatin1Char('\\')) ++end;
        ++end;
    }
    while (end > idx && text.at(end - 1) == QLatin1Char(' ')) --end;
    editor->setSel(positionAt(editor, target, end), positionAt(editor, target, idx));
    return true;
}

static void writeTable(ScintillaNext *editor, const MarkdownTable &original, const MarkdownTable &updated, int row, int col)
{
    editor->beginUndoAction();
    replaceLines(editor, original.startLine(), original.lastLine, updated.toMarkdown());
    editor->endUndoAction();
    const bool hasMeta = !updated.toMarkdown().startsWith(QLatin1Char('|'));
    selectTableCell(editor, original.startLine() + (hasMeta ? 1 : 0), qBound(0, row, updated.rowCount() - 1), qBound(0, col, updated.columnCount() - 1));
}

static bool requireTable(ScintillaNext *editor, TableContext *ctx)
{
    *ctx = tableAtCaret(editor);
    if (!ctx->valid) {
        QMessageBox::information(editor, QObject::tr("Tabela"), QObject::tr("Coloque o cursor dentro de uma tabela."));
        return false;
    }
    ctx->table.normalize();
    return true;
}

static QString askTableName(QWidget *parent, ScintillaNext *editor, const QString &current, const MarkdownTable *self, bool *ok)
{
    *ok = false;
    QString name = current.simplified();
    bool askUser = name.isEmpty();
    for (;;) {
        if (askUser) {
            QDialog d(parent);
            d.setWindowTitle(QObject::tr("Nome da tabela"));
            auto *layout = new QVBoxLayout(&d);
            auto *label = new QLabel(QObject::tr("Toda tabela precisa de um nome, usado nas fórmulas (ex.: Vendas!B2):"), &d);
            label->setWordWrap(true);
            auto *edit = new QLineEdit(name, &d);
            layout->addWidget(label);
            layout->addWidget(edit);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
            layout->addWidget(buttons);
            QObject::connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
            if (d.exec() != QDialog::Accepted)
                return QString();
            name = edit->text().simplified();
        }
        askUser = true; // se o nome não servir, pergunta de novo

        static const QRegularExpression valid(QStringLiteral("^[\\p{L}_][\\p{L}\\p{N}_ ]*$"));
        if (!valid.match(name).hasMatch()) {
            QMessageBox::warning(parent, QObject::tr("Nome inválido"), QObject::tr("Use letras, números, espaço ou _ e comece com uma letra."));
            continue;
        }
        // O nome não pode repetir na nota nem no vault
        bool duplicate = false;
        for (const MarkdownTable &t : findMarkdownTables(allLines(editor)))
            if ((!self || t.firstLine != self->firstLine) && QString::compare(t.meta.name, name, Qt::CaseInsensitive) == 0)
                duplicate = true;
        const QString other = VaultTables::instance().noteOf(name, editor->getFilePath());
        if (duplicate || !other.isEmpty()) {
            QMessageBox::warning(parent, QObject::tr("Nome em uso"), QObject::tr("Já existe uma tabela chamada \"%1\"%2.")
                                 .arg(name, other.isEmpty() ? QString() : QObject::tr(" em %1").arg(other)));
            continue;
        }
        *ok = true;
        return name;
    }
}

void insertTable(ScintillaNext *editor, QWidget *parent)
{
    QDialog d(parent);
    d.setWindowTitle(QObject::tr("Inserir tabela"));
    auto *form = new QFormLayout(&d);
    auto *name = new QLineEdit(&d);
    name->setPlaceholderText(QObject::tr("ex.: Vendas"));
    auto *cols = new QSpinBox(&d);
    cols->setRange(1, 30);
    cols->setValue(3);
    auto *rows = new QSpinBox(&d);
    rows->setRange(1, 200);
    rows->setValue(3);
    form->addRow(QObject::tr("Nome (obrigatório):"), name);
    form->addRow(QObject::tr("Colunas:"), cols);
    form->addRow(QObject::tr("Linhas (além do cabeçalho):"), rows);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return;

    bool ok;
    const QString tableName = askTableName(parent, editor, name->text(), nullptr, &ok);
    if (!ok)
        return;

    MarkdownTable t;
    t.meta.name = tableName;
    t.metaLine = 0;
    QStringList header;
    for (int c = 0; c < cols->value(); ++c)
        header << QObject::tr("Coluna %1").arg(c + 1);
    t.rows << header;
    for (int r = 0; r < rows->value(); ++r)
        t.rows << QStringList(QVector<QString>(cols->value()).toList());
    t.normalize();

    const int line = editor->lineFromPosition(editor->currentPos());
    const bool lineEmpty = lineText(editor, line).trimmed().isEmpty();
    editor->beginUndoAction();
    int tableLine;
    if (lineEmpty) {
        // Linha vazia antes da tabela, se a anterior tiver texto
        const bool needGap = line > 0 && !lineText(editor, line - 1).trimmed().isEmpty();
        replaceLines(editor, line, line, (needGap ? QStringLiteral("\n") : QString()) + t.toMarkdown() + QStringLiteral("\n"));
        tableLine = line + (needGap ? 1 : 0);
    }
    else {
        insertTextAt(editor, editor->lineEndPosition(line), QStringLiteral("\n\n") + t.toMarkdown() + QStringLiteral("\n"));
        tableLine = line + 2;
    }
    editor->endUndoAction();
    selectTableCell(editor, tableLine + 1, 0, 0);
}

void insertRow(ScintillaNext *editor, bool below)
{
    TableContext ctx;
    if (!requireTable(editor, &ctx)) return;
    MarkdownTable t = ctx.table;
    int at = below ? ctx.row + 1 : ctx.row;
    if (at == 0) at = 1; // nada acima do cabeçalho
    t.rows.insert(at, QStringList(QVector<QString>(t.columnCount()).toList()));
    writeTable(editor, ctx.table, t, at, ctx.col);
}

void deleteRow(ScintillaNext *editor)
{
    TableContext ctx;
    if (!requireTable(editor, &ctx)) return;
    if (ctx.row == 0) {
        QMessageBox::information(editor, QObject::tr("Tabela"), QObject::tr("O cabeçalho não pode ser excluído. Use \"Excluir tabela\"."));
        return;
    }
    MarkdownTable t = ctx.table;
    t.rows.removeAt(ctx.row);
    writeTable(editor, ctx.table, t, ctx.row, ctx.col);
}

void insertColumn(ScintillaNext *editor, bool right)
{
    TableContext ctx;
    if (!requireTable(editor, &ctx)) return;
    MarkdownTable t = ctx.table;
    const int at = right ? ctx.col + 1 : ctx.col;
    for (int r = 0; r < t.rows.size(); ++r)
        t.rows[r].insert(at, r == 0 ? QObject::tr("Coluna %1").arg(t.columnCount() + 1) : QString());
    t.aligns.insert(at, QString());
    writeTable(editor, ctx.table, t, ctx.row, at);
}

void deleteColumn(ScintillaNext *editor)
{
    TableContext ctx;
    if (!requireTable(editor, &ctx)) return;
    if (ctx.table.columnCount() <= 1) {
        deleteTable(editor);
        return;
    }
    MarkdownTable t = ctx.table;
    for (QStringList &r : t.rows)
        if (ctx.col < r.size()) r.removeAt(ctx.col);
    t.aligns.removeAt(ctx.col);
    writeTable(editor, ctx.table, t, ctx.row, qMax(0, ctx.col - 1));
}

void deleteTable(ScintillaNext *editor)
{
    TableContext ctx;
    if (!requireTable(editor, &ctx)) return;
    if (QMessageBox::question(editor, QObject::tr("Excluir tabela"),
                              QObject::tr("Excluir a tabela \"%1\" inteira?").arg(ctx.table.meta.name.isEmpty() ? QObject::tr("sem nome") : ctx.table.meta.name))
            != QMessageBox::Yes)
        return;
    int last = ctx.table.lastLine;
    if (last + 1 < editor->lineCount() && lineText(editor, last + 1).trimmed().isEmpty())
        ++last;
    editor->beginUndoAction();
    editor->setTargetRange(editor->positionFromLine(ctx.table.startLine()), editor->positionFromLine(last + 1 < editor->lineCount() ? last + 1 : last));
    if (last + 1 >= editor->lineCount())
        editor->setTargetRange(editor->positionFromLine(ctx.table.startLine()), editor->textLength());
    editor->replaceTarget(0, "");
    editor->endUndoAction();
}

// Botão que escolhe uma cor
static QPushButton *colorButton(QWidget *parent, QColor *color, const QString &emptyText)
{
    auto *b = new QPushButton(parent);
    auto refresh = [b, color, emptyText]() {
        if (color->isValid()) {
            b->setText(color->name());
            b->setStyleSheet(QStringLiteral("QPushButton { background:%1; color:%2; }").arg(color->name(), color->lightnessF() > 0.55 ? QStringLiteral("#000") : QStringLiteral("#fff")));
        }
        else {
            b->setText(emptyText);
            b->setStyleSheet(QString());
        }
    };
    refresh();
    QObject::connect(b, &QPushButton::clicked, b, [b, color, refresh]() {
        const QColor c = QColorDialog::getColor(color->isValid() ? *color : QColor(Qt::gray), b);
        if (c.isValid()) { *color = c; refresh(); }
    });
    return b;
}

void tableProperties(ScintillaNext *editor, QWidget *parent)
{
    TableContext ctx;
    if (!requireTable(editor, &ctx)) return;
    MarkdownTable t = ctx.table;

    QDialog d(parent);
    d.setWindowTitle(QObject::tr("Propriedades da tabela"));
    auto *form = new QFormLayout(&d);
    auto *name = new QLineEdit(t.meta.name, &d);
    QColor border = t.meta.borderColor;
    auto *borderBtn = colorButton(&d, &border, QObject::tr("Automática"));
    auto *resetBorder = new QPushButton(QObject::tr("Automática"), &d);
    auto *style = new QComboBox(&d);
    const QList<QPair<QString, QString>> styles = {
        {QStringLiteral("solid"), QObject::tr("Contínua")}, {QStringLiteral("dashed"), QObject::tr("Tracejada")},
        {QStringLiteral("dotted"), QObject::tr("Pontilhada")}, {QStringLiteral("double"), QObject::tr("Dupla")},
        {QStringLiteral("none"), QObject::tr("Sem linhas")}};
    for (const auto &s : styles) style->addItem(s.second, s.first);
    style->setCurrentIndex(qMax(0, style->findData(t.meta.borderStyle)));
    auto *width = new QSpinBox(&d);
    width->setRange(1, 8);
    width->setValue(qMax(1, t.meta.borderWidth));
    auto *borderRow = new QWidget(&d);
    auto *bl = new QHBoxLayout(borderRow);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addWidget(borderBtn);
    bl->addWidget(resetBorder);
    QObject::connect(resetBorder, &QPushButton::clicked, &d, [&]() { border = QColor(); borderBtn->setText(QObject::tr("Automática")); borderBtn->setStyleSheet(QString()); });

    auto *clearShade = new QPushButton(QObject::tr("Remover todo o sombreamento"), &d);
    bool clearAllShade = false;
    QObject::connect(clearShade, &QPushButton::clicked, &d, [&]() { clearAllShade = true; clearShade->setEnabled(false); });

    form->addRow(QObject::tr("Nome:"), name);
    form->addRow(QObject::tr("Cor das linhas:"), borderRow);
    form->addRow(QObject::tr("Estilo das linhas:"), style);
    form->addRow(QObject::tr("Espessura:"), width);
    form->addRow(QString(), clearShade);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return;

    QString newName = name->text().simplified();
    if (QString::compare(newName, t.meta.name, Qt::CaseInsensitive) != 0 || newName.isEmpty()) {
        bool ok;
        newName = askTableName(parent, editor, newName, &ctx.table, &ok);
        if (!ok) return;
    }
    t.meta.name = newName;
    t.meta.borderColor = border;
    t.meta.borderStyle = style->currentData().toString();
    t.meta.borderWidth = width->value();
    if (clearAllShade) t.meta.shading.clear();
    writeTable(editor, ctx.table, t, ctx.row, ctx.col);
}

void shadeCells(ScintillaNext *editor, QWidget *parent)
{
    TableContext ctx;
    if (!requireTable(editor, &ctx)) return;
    MarkdownTable t = ctx.table;
    const int r1 = qMin(ctx.row, ctx.row2), r2 = qMax(ctx.row, ctx.row2);
    const int c1 = qMin(ctx.col, ctx.col2), c2 = qMax(ctx.col, ctx.col2);

    QColorDialog dialog(t.meta.shadeFor(r1, c1).isValid() ? t.meta.shadeFor(r1, c1) : QColor(QStringLiteral("#FFF3C4")), parent);
    dialog.setWindowTitle(QObject::tr("Sombreamento de %1").arg(r1 == r2 && c1 == c2 ? cellRefName(r1, c1) : cellRefName(r1, c1) + QLatin1Char(':') + cellRefName(r2, c2)));
    auto *none = new QPushButton(QObject::tr("Sem sombreamento"), &dialog);
    bool remove = false;
    QObject::connect(none, &QPushButton::clicked, &dialog, [&]() { remove = true; dialog.accept(); });
    if (auto *box = dialog.findChild<QDialogButtonBox *>())
        box->addButton(none, QDialogButtonBox::ResetRole);
    if (dialog.exec() != QDialog::Accepted)
        return;

    t.meta.setShade(r1, c1, r2, c2, remove ? QColor() : dialog.selectedColor());
    if (t.meta.name.isEmpty()) {
        bool ok;
        t.meta.name = askTableName(parent, editor, QString(), &ctx.table, &ok);
        if (!ok) return;
    }
    writeTable(editor, ctx.table, t, ctx.row, ctx.col);
}

void showFormulaHelp(QWidget *parent)
{
    QString text = QObject::tr(
        "<h3>Fórmulas nas tabelas</h3>"
        "<p>Comece a célula com <b>=</b>. As colunas são letras e as linhas números; o cabeçalho é a linha 1 "
        "(a primeira linha de dados é a 2).</p>"
        "<ul>"
        "<li><code>=B2*C2</code> — conta com outras células</li>"
        "<li><code>=SOMA(B2:B9)</code> ou <code>=SOMA(B:B)</code> — intervalo ou coluna inteira</li>"
        "<li><code>=Vendas!B2</code> — célula da tabela <i>Vendas</i>, mesmo em outra nota do vault</li>"
        "<li><code>='Custos 2026'!C4</code> — nome com espaço vai entre aspas simples</li>"
        "<li><code>=SE(B2&gt;100;\"alto\";\"baixo\")</code> — use <b>;</b> entre argumentos (com <b>;</b> a vírgula é decimal)</li>"
        "</ul><p><b>Funções:</b> %1</p>"
        "<p>Os nomes em inglês também funcionam (SUM, AVERAGE, IF, VLOOKUP...).</p>")
        .arg(FormulaEngine::functionNames().join(QStringLiteral(", ")));
    QMessageBox box(parent);
    box.setWindowTitle(QObject::tr("Ajuda de fórmulas"));
    box.setTextFormat(Qt::RichText);
    box.setText(text);
    box.exec();
}

} // namespace MarkdownCommands
