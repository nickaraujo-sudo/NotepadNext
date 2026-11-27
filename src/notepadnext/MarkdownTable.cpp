/*
 * NotepadNext - recursos próprios deste fork.
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#include "MarkdownTable.h"

#include <QRegularExpression>

static const QString kMetaTag = QStringLiteral("notepadnext-tabela");
// Nome antigo (antes de o app se chamar NotepadNext): ainda é lido
static const QString kLegacyMetaTag = QStringLiteral("craftpad-tabela");

int columnFromLetters(const QString &letters)
{
    if (letters.isEmpty() || letters.size() > 3)
        return -1;
    int col = 0;
    for (QChar ch : letters) {
        const QChar up = ch.toUpper();
        if (up < QLatin1Char('A') || up > QLatin1Char('Z'))
            return -1;
        col = col * 26 + (up.unicode() - 'A' + 1);
    }
    return col - 1;
}

QString lettersFromColumn(int col)
{
    QString s;
    int n = col + 1;
    while (n > 0) {
        const int r = (n - 1) % 26;
        s.prepend(QChar('A' + r));
        n = (n - 1) / 26;
    }
    return s;
}

bool parseCellRef(const QString &ref, int *row, int *col)
{
    static const QRegularExpression re(QStringLiteral("^\\$?([A-Za-z]{1,3})\\$?([0-9]{1,6})$"));
    const auto m = re.match(ref.trimmed());
    if (!m.hasMatch())
        return false;
    const int c = columnFromLetters(m.captured(1));
    const int r = m.captured(2).toInt() - 1;
    if (c < 0 || r < 0)
        return false;
    if (row) *row = r;
    if (col) *col = c;
    return true;
}

QString cellRefName(int row, int col)
{
    return lettersFromColumn(col) + QString::number(row + 1);
}

// ---------------------------------------------------------------------------
// TableMeta
// ---------------------------------------------------------------------------

QString TableMeta::toComment() const
{
    QStringList parts;
    parts << kMetaTag;
    parts << QStringLiteral("nome=\"%1\"").arg(QString(name).remove(QLatin1Char('"')));
    if (hasBorderSettings()) {
        const QString color = borderColor.isValid() ? borderColor.name() : QStringLiteral("auto");
        parts << QStringLiteral("borda=\"%1 %2 %3\"").arg(color, borderStyle).arg(borderWidth);
    }
    if (!shading.isEmpty()) {
        QStringList sh;
        for (const CellShade &s : shading)
            sh << QStringLiteral("%1=%2").arg(s.range, s.color.name());
        parts << QStringLiteral("sombra=\"%1\"").arg(sh.join(QLatin1Char(' ')));
    }
    return QStringLiteral("<!-- %1 -->").arg(parts.join(QLatin1Char(' ')));
}

bool TableMeta::parseComment(const QString &line, TableMeta *out)
{
    const QString t = line.trimmed();
    if (!t.startsWith(QLatin1String("<!--")) || !t.endsWith(QLatin1String("-->")) || (!t.contains(kMetaTag) && !t.contains(kLegacyMetaTag)))
        return false;

    TableMeta meta;
    static const QRegularExpression kv(QStringLiteral("([\\p{L}_]+)\\s*=\\s*\"([^\"]*)\""));
    auto it = kv.globalMatch(t);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString key = m.captured(1).toLower();
        const QString value = m.captured(2).trimmed();
        if (key == QLatin1String("nome") || key == QLatin1String("name")) {
            meta.name = value;
        }
        else if (key == QLatin1String("borda") || key == QLatin1String("border")) {
            const QStringList p = value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (p.size() > 0 && p.at(0) != QLatin1String("auto"))
                meta.borderColor = QColor(p.at(0));
            if (p.size() > 1)
                meta.borderStyle = p.at(1);
            if (p.size() > 2)
                meta.borderWidth = qBound(0, p.at(2).toInt(), 8);
        }
        else if (key == QLatin1String("sombra") || key == QLatin1String("shade")) {
            for (const QString &item : value.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                const int eq = item.indexOf(QLatin1Char('='));
                if (eq <= 0)
                    continue;
                CellShade s;
                s.range = item.left(eq).toUpper();
                s.color = QColor(item.mid(eq + 1));
                if (s.color.isValid())
                    meta.shading << s;
            }
        }
    }
    if (out)
        *out = meta;
    return true;
}

static bool parseRange(const QString &range, int *r1, int *c1, int *r2, int *c2)
{
    const QStringList p = range.split(QLatin1Char(':'));
    if (!parseCellRef(p.value(0), r1, c1))
        return false;
    if (p.size() > 1) {
        if (!parseCellRef(p.value(1), r2, c2))
            return false;
    }
    else {
        *r2 = *r1;
        *c2 = *c1;
    }
    if (*r1 > *r2) std::swap(*r1, *r2);
    if (*c1 > *c2) std::swap(*c1, *c2);
    return true;
}

QColor TableMeta::shadeFor(int row, int col) const
{
    QColor color;
    for (const CellShade &s : shading) {
        int r1, c1, r2, c2;
        if (parseRange(s.range, &r1, &c1, &r2, &c2) && row >= r1 && row <= r2 && col >= c1 && col <= c2)
            color = s.color; // o último vence
    }
    return color;
}

void TableMeta::setShade(int row1, int col1, int row2, int col2, const QColor &color)
{
    const QString range = (row1 == row2 && col1 == col2)
            ? cellRefName(row1, col1)
            : cellRefName(row1, col1) + QLatin1Char(':') + cellRefName(row2, col2);

    // Remove sombreamentos que ficam totalmente cobertos pelo novo
    for (int i = shading.size() - 1; i >= 0; --i) {
        int r1, c1, r2, c2;
        if (parseRange(shading.at(i).range, &r1, &c1, &r2, &c2)
                && r1 >= row1 && r2 <= row2 && c1 >= col1 && c2 <= col2)
            shading.removeAt(i);
    }
    if (color.isValid())
        shading << CellShade{range, color};
}

// ---------------------------------------------------------------------------
// MarkdownTable
// ---------------------------------------------------------------------------

int MarkdownTable::columnCount() const
{
    int n = 0;
    for (const QStringList &r : rows)
        n = qMax(n, int(r.size()));
    return n;
}

QString MarkdownTable::cell(int row, int col) const
{
    if (row < 0 || row >= rows.size())
        return QString();
    return rows.at(row).value(col);
}

void MarkdownTable::normalize()
{
    const int cols = qMax(1, columnCount());
    for (QStringList &r : rows) {
        while (r.size() < cols)
            r << QString();
    }
    while (aligns.size() < cols)
        aligns << QString();
    while (aligns.size() > cols)
        aligns.removeLast();
}

// Width of the text on screen in a monospaced editor: accents and other combining
// marks take no space; CJK characters and emoji take two columns.
int MarkdownTable::displayWidth(const QString &text)
{
    int width = 0;
    const QList<uint> codepoints = text.toUcs4();
    for (uint cp : codepoints) {
        const QChar::Category cat = QChar::category(char32_t(cp));
        if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_Enclosing || cat == QChar::Other_Format
            || (cp >= 0xFE00 && cp <= 0xFE0F))
            continue;
        const bool wide = (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3)
                          || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60)
                          || (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x20000 && cp <= 0x3FFFD);
        width += wide ? 2 : 1;
    }
    return width;
}

QString MarkdownTable::toMarkdown() const
{
    MarkdownTable t = *this;
    t.normalize();
    const int cols = t.columnCount();

    QVector<int> widths(cols, 3);
    for (const QStringList &r : t.rows)
        for (int c = 0; c < cols; ++c)
            widths[c] = qMax(widths[c], displayWidth(r.at(c)));

    // Pads each cell to the column width, following the column alignment
    auto rowText = [&](const QStringList &r) {
        QString s = QStringLiteral("|");
        for (int c = 0; c < cols; ++c) {
            const QString cellText = r.at(c);
            const int gap = widths[c] - displayWidth(cellText);
            const QString a = t.aligns.value(c);
            int left = 0;
            if (a == QLatin1String("right")) left = gap;
            else if (a == QLatin1String("center")) left = gap / 2;
            s += QLatin1Char(' ') + QString(left, QLatin1Char(' ')) + cellText + QString(gap - left, QLatin1Char(' ')) + QStringLiteral(" |");
        }
        return s;
    };

    QStringList out;
    if (!meta.name.isEmpty() || meta.hasBorderSettings() || !meta.shading.isEmpty() || metaLine >= 0)
        out << meta.toComment();
    out << rowText(t.rows.value(0));
    QString sep = QStringLiteral("|");
    for (int c = 0; c < cols; ++c) {
        const QString a = t.aligns.value(c);
        QString dashes(widths[c], QLatin1Char('-'));
        if (a == QLatin1String("left")) dashes[0] = QLatin1Char(':');
        else if (a == QLatin1String("right")) dashes[widths[c] - 1] = QLatin1Char(':');
        else if (a == QLatin1String("center")) { dashes[0] = QLatin1Char(':'); dashes[widths[c] - 1] = QLatin1Char(':'); }
        sep += QLatin1Char(' ') + dashes + QStringLiteral(" |");
    }
    out << sep;
    for (int i = 1; i < t.rows.size(); ++i)
        out << rowText(t.rows.at(i));
    return out.join(QLatin1Char('\n'));
}

bool MarkdownTable::isTableLine(const QString &line)
{
    const QString t = line.trimmed();
    return t.startsWith(QLatin1Char('|')) || (t.contains(QLatin1Char('|')) && !t.startsWith(QLatin1String("<!--")));
}

bool MarkdownTable::isSeparatorLine(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("^\\s*\\|?\\s*:?-{1,}:?\\s*(\\|\\s*:?-{1,}:?\\s*)*\\|?\\s*$"));
    return line.contains(QLatin1Char('-')) && re.match(line).hasMatch();
}

QStringList MarkdownTable::splitRow(const QString &line)
{
    QString t = line.trimmed();
    QStringList cells;
    QString cur;
    bool inCode = false;
    for (int i = 0; i < t.size(); ++i) {
        const QChar ch = t.at(i);
        if (ch == QLatin1Char('\\') && i + 1 < t.size() && t.at(i + 1) == QLatin1Char('|')) {
            cur += QStringLiteral("\\|");
            ++i;
            continue;
        }
        if (ch == QLatin1Char('`'))
            inCode = !inCode;
        if (ch == QLatin1Char('|') && !inCode) {
            cells << cur.trimmed();
            cur.clear();
            continue;
        }
        cur += ch;
    }
    cells << cur.trimmed();
    // Pipes no início e no fim não criam células
    if (t.startsWith(QLatin1Char('|')) && !cells.isEmpty())
        cells.removeFirst();
    if (t.endsWith(QLatin1Char('|')) && !t.endsWith(QLatin1String("\\|")) && !cells.isEmpty())
        cells.removeLast();
    return cells;
}

int MarkdownTable::columnAt(const QString &line, int column)
{
    const bool leading = line.trimmed().startsWith(QLatin1Char('|'));
    const int lead = line.indexOf(QLatin1Char('|'));
    int col = 0;
    bool inCode = false;
    for (int i = 0; i < qMin(column, int(line.size())); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('\\')) { ++i; continue; }
        if (ch == QLatin1Char('`')) inCode = !inCode;
        if (ch == QLatin1Char('|') && !inCode && !(leading && i == lead))
            ++col;
    }
    return col;
}

QList<MarkdownTable> findMarkdownTables(const QStringList &lines)
{
    QList<MarkdownTable> tables;
    bool inFence = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"))) {
            inFence = !inFence;
            continue;
        }
        if (inFence)
            continue;

        if (i + 1 < lines.size() && MarkdownTable::isTableLine(lines.at(i)) && MarkdownTable::isSeparatorLine(lines.at(i + 1))) {
            MarkdownTable t;
            t.firstLine = i;
            if (i > 0 && TableMeta::parseComment(lines.at(i - 1), &t.meta))
                t.metaLine = i - 1;

            t.rows << MarkdownTable::splitRow(lines.at(i));
            for (const QString &a : MarkdownTable::splitRow(lines.at(i + 1))) {
                const bool l = a.startsWith(QLatin1Char(':'));
                const bool r = a.endsWith(QLatin1Char(':'));
                t.aligns << (l && r ? QStringLiteral("center") : r ? QStringLiteral("right") : l ? QStringLiteral("left") : QString());
            }
            int j = i + 2;
            while (j < lines.size() && !lines.at(j).trimmed().isEmpty() && MarkdownTable::isTableLine(lines.at(j))) {
                t.rows << MarkdownTable::splitRow(lines.at(j));
                ++j;
            }
            t.lastLine = j - 1;
            tables << t;
            i = j - 1;
        }
    }
    return tables;
}
