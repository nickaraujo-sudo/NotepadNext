/*
 * craftpad++ - extensão do Notepad Next.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#include "MarkdownRenderer.h"
#include "FormulaEngine.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextList>
#include <QTextTable>
#include <QUrl>

extern "C" {
#include "md4c-html.h"
}

// ---------------------------------------------------------------------------
// Divisão do Markdown em trechos
// ---------------------------------------------------------------------------

static const QRegularExpression &headingRe() { static const QRegularExpression r(QStringLiteral("^#{1,6}(\\s|$)")); return r; }
static const QRegularExpression &listRe() { static const QRegularExpression r(QStringLiteral("^\\s*([-*+]|\\d{1,9}[.)])(\\s|$)")); return r; }
static const QRegularExpression &ruleRe() { static const QRegularExpression r(QStringLiteral("^\\s{0,3}((\\*\\s*){3,}|(-\\s*){3,}|(_\\s*){3,})$")); return r; }
static const QRegularExpression &divAlignRe() { static const QRegularExpression r(QStringLiteral("^\\s*<div\\s+align=\"(left|center|right|justify)\"\\s*>\\s*$"), QRegularExpression::CaseInsensitiveOption); return r; }

static bool isFence(const QString &line) {
    const QString t = line.trimmed();
    return t.startsWith(QLatin1String("```")) || t.startsWith(QLatin1String("~~~"));
}

QList<MdSegment> MarkdownRenderer::segment(const QStringList &lines, const QList<MarkdownTable> &tables)
{
    QHash<int, int> tableStarts;
    for (int i = 0; i < tables.size(); ++i)
        tableStarts.insert(tables.at(i).startLine(), i);

    QList<MdSegment> out;
    const int n = lines.size();
    int i = 0;

    // Frontmatter YAML
    if (n > 0 && lines.first().trimmed() == QLatin1String("---")) {
        for (int j = 1; j < n; ++j) {
            const QString t = lines.at(j).trimmed();
            if (t == QLatin1String("---") || t == QLatin1String("...")) {
                MdSegment s;
                s.kind = MdSegment::Frontmatter;
                s.startLine = 0;
                s.endLine = j + 1;
                out << s;
                i = j + 1;
                break;
            }
        }
    }

    auto blank = [&](int k) { return lines.at(k).trimmed().isEmpty(); };
    auto startsBlock = [&](int k) {
        const QString &l = lines.at(k);
        return headingRe().match(l).hasMatch() || isFence(l) || tableStarts.contains(k) || divAlignRe().match(l).hasMatch();
    };

    while (i < n) {
        if (blank(i)) { ++i; continue; }
        MdSegment s;
        s.startLine = i;
        const QString &line = lines.at(i);

        if (isFence(line)) {
            s.kind = MdSegment::Code;
            const QString fence = line.trimmed().left(3);
            s.fenceOpen = line;
            int j = i + 1;
            while (j < n && !lines.at(j).trimmed().startsWith(fence)) ++j;
            if (j < n) { s.fenceClose = lines.at(j); ++j; }
            s.endLine = j;
        }
        else if (tableStarts.contains(i)) {
            s.kind = MdSegment::Table;
            s.tableIndex = tableStarts.value(i);
            s.endLine = tables.at(s.tableIndex).lastLine + 1;
        }
        else if (divAlignRe().match(line).hasMatch()) {
            s.kind = MdSegment::Aligned;
            s.alignment = divAlignRe().match(line).captured(1).toLower();
            int j = i + 1;
            while (j < n && !lines.at(j).trimmed().startsWith(QLatin1String("</div"), Qt::CaseInsensitive)) ++j;
            s.endLine = qMin(n, j + 1);
        }
        else if (headingRe().match(line).hasMatch()) {
            s.kind = MdSegment::Heading;
            s.endLine = i + 1;
        }
        else if (ruleRe().match(line).hasMatch()) {
            s.kind = MdSegment::Rule;
            s.endLine = i + 1;
        }
        else if (line.trimmed().startsWith(QLatin1Char('>'))) {
            s.kind = MdSegment::Quote;
            int j = i + 1;
            while (j < n && !blank(j) && !startsBlock(j)) ++j;
            s.endLine = j;
        }
        else if (listRe().match(line).hasMatch()) {
            s.kind = MdSegment::List;
            int j = i + 1;
            for (;;) {
                while (j < n && !blank(j) && !startsBlock(j)) ++j;
                // Linhas em branco dentro da lista: continua se o próximo item é da lista
                int k = j;
                while (k < n && blank(k)) ++k;
                if (k < n && k > j && (listRe().match(lines.at(k)).hasMatch() || lines.at(k).startsWith(QLatin1String("  ")) || lines.at(k).startsWith(QLatin1Char('\t')))
                        && !startsBlock(k)) {
                    j = k;
                    continue;
                }
                break;
            }
            s.endLine = j;
        }
        else if (line.trimmed().startsWith(QLatin1Char('<'))) {
            s.kind = MdSegment::Html;
            int j = i + 1;
            while (j < n && !blank(j)) ++j;
            s.endLine = j;
        }
        else {
            s.kind = MdSegment::Paragraph;
            int j = i + 1;
            while (j < n && !blank(j) && !startsBlock(j)) ++j;
            s.endLine = j;
        }
        // Remove linhas em branco no fim do trecho
        while (s.endLine > s.startLine + 1 && blank(s.endLine - 1)) --s.endLine;
        out << s;
        i = qMax(s.endLine, i + 1);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Markdown -> HTML (md4c)
// ---------------------------------------------------------------------------

static void appendHtml(const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    static_cast<QByteArray *>(userdata)->append(text, int(size));
}

QString MarkdownRenderer::markdownToHtml(const QString &markdown)
{
    const QByteArray in = markdown.toUtf8();
    QByteArray out;
    const unsigned parserFlags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS
            | MD_FLAG_PERMISSIVEURLAUTOLINKS | MD_FLAG_PERMISSIVEWWWAUTOLINKS | MD_FLAG_NOINDENTEDCODEBLOCKS
            | MD_FLAG_HARD_SOFT_BREAKS; // como o Obsidian: quebra de linha simples aparece
    md_html(in.constData(), MD_SIZE(in.size()), appendHtml, &out, parserFlags, 0);
    QString html = QString::fromUtf8(out);

    // Caixas de seleção: o Qt mostra a marca do item da lista
    static const QRegularExpression checked(QStringLiteral("<li class=\"task-list-item\">\\s*<input type=\"checkbox\" class=\"task-list-item-checkbox\" disabled checked>"));
    static const QRegularExpression unchecked(QStringLiteral("<li class=\"task-list-item\">\\s*<input type=\"checkbox\" class=\"task-list-item-checkbox\" disabled>"));
    html.replace(checked, QStringLiteral("<li class=\"checked\">"));
    html.replace(unchecked, QStringLiteral("<li class=\"unchecked\">"));
    // Quebras de linha entre tags viram espaços no Qt: remove
    static const QRegularExpression between(QStringLiteral(">[ \\t]*\\n\\s*<"));
    html.replace(between, QStringLiteral("><"));
    html = html.trimmed();
    html.replace(QLatin1String("<del>"), QLatin1String("<s>"));
    html.replace(QLatin1String("</del>"), QLatin1String("</s>"));
    return html;
}

// ---------------------------------------------------------------------------
// Sintaxe do Obsidian -> Markdown comum
// ---------------------------------------------------------------------------

static const QRegularExpression &imageExtRe() { static const QRegularExpression r(QStringLiteral("\\.(png|jpe?g|gif|svg|webp|bmp)$"), QRegularExpression::CaseInsensitiveOption); return r; }

static QString attr(const QString &s)
{
    return s.toHtmlEscaped();
}

QString MarkdownRenderer::preprocess(const QString &markdown, MdSegment *seg) const
{
    static const QRegularExpression embedRe(QStringLiteral(R"(!\[\[([^\]\|\n]+)(?:\|([^\]\n]*))?\]\])"));
    static const QRegularExpression mdImageRe(QStringLiteral(R"(!\[([^\]\n]*)\]\(([^)\n]+)\))"));
    static const QRegularExpression htmlImgRe(QStringLiteral(R"(<img\s[^>]*>)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression srcRe(QStringLiteral(R"(src\s*=\s*"([^"]*)\")"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression wikiRe(QStringLiteral(R"(\[\[([^\]\|\n]+)(?:\|([^\]\n]*))?\]\])"));
    static const QRegularExpression tagRe(QStringLiteral(R"((?<![\p{L}\p{N}_/#&"'=:\]\(])#([\p{L}\p{N}_\-/]*[\p{L}_\-][\p{L}\p{N}_\-/]*))"));
    static const QRegularExpression inlineCodeRe(QStringLiteral("`[^`\n]*`"));

    auto resolve = [this](const QString &name) {
        const QString decoded = QUrl::fromPercentEncoding(name.trimmed().toUtf8());
        if (decoded.startsWith(QLatin1String("http://")) || decoded.startsWith(QLatin1String("https://")))
            return decoded;
        if (resolveAttachment) {
            const QString r = resolveAttachment(decoded);
            if (!r.isEmpty()) return r;
        }
        return decoded;
    };

    QStringList lines = markdown.split(QLatin1Char('\n'));
    for (QString &line : lines) {
        // Trechos de código ficam como estão
        if (line.contains(inlineCodeRe))
            continue;

        // Imagens com tamanho e posição: <img src="..." width="300" align="left">
        {
            QString out;
            int last = 0;
            auto it = htmlImgRe.globalMatch(line);
            while (it.hasNext()) {
                const auto m = it.next();
                const auto sm = srcRe.match(m.captured(0));
                out += line.mid(last, m.capturedStart() - last);
                if (sm.hasMatch()) {
                    const QString resolved = resolve(sm.captured(1));
                    QString tag = m.captured(0);
                    tag.replace(sm.capturedStart(1), sm.capturedLength(1), attr(resolved));
                    out += tag;
                    if (seg) seg->images << qMakePair(resolved, m.captured(0));
                }
                else {
                    out += m.captured(0);
                }
                last = m.capturedEnd();
            }
            out += line.mid(last);
            line = out;
        }

        // ![[imagem.png|400]] (Obsidian) e ![](imagem.png)
        {
            QString out;
            int last = 0;
            auto it = embedRe.globalMatch(line);
            while (it.hasNext()) {
                const auto m = it.next();
                out += line.mid(last, m.capturedStart() - last);
                const QString target = m.captured(1).trimmed();
                if (imageExtRe().match(target).hasMatch()) {
                    const QString resolved = resolve(target);
                    QString width;
                    static const QRegularExpression sizeRe(QStringLiteral("^(\\d+)(x\\d+)?$"));
                    const auto sm = sizeRe.match(m.captured(2).trimmed());
                    if (sm.hasMatch()) width = QStringLiteral(" width=\"%1\"").arg(sm.captured(1));
                    out += QStringLiteral("<img src=\"%1\"%2>").arg(attr(resolved), width);
                    if (seg) seg->images << qMakePair(resolved, m.captured(0));
                }
                else {
                    // Nota incorporada: mostra como link
                    out += QStringLiteral("[[%1]]").arg(m.captured(1));
                }
                last = m.capturedEnd();
            }
            out += line.mid(last);
            line = out;
        }
        {
            QString out;
            int last = 0;
            auto it = mdImageRe.globalMatch(line);
            while (it.hasNext()) {
                const auto m = it.next();
                out += line.mid(last, m.capturedStart() - last);
                QString src = m.captured(2).trimmed();
                if (src.startsWith(QLatin1Char('<')) && src.endsWith(QLatin1Char('>'))) src = src.mid(1, src.size() - 2);
                src = src.section(QLatin1Char(' '), 0, 0); // remove "título"
                const QString resolved = resolve(src);
                out += QStringLiteral("<img src=\"%1\" alt=\"%2\">").arg(attr(resolved), attr(m.captured(1)));
                if (seg) seg->images << qMakePair(resolved, m.captured(0));
                last = m.capturedEnd();
            }
            out += line.mid(last);
            line = out;
        }

        // [[Nota|apelido]] -> link clicável
        {
            QString out;
            int last = 0;
            auto it = wikiRe.globalMatch(line);
            while (it.hasNext()) {
                const auto m = it.next();
                const QString target = m.captured(1).trimmed();
                const QString label = m.captured(2).isEmpty() ? target : m.captured(2).trimmed();
                out += line.mid(last, m.capturedStart() - last);
                out += QStringLiteral("[%1](craftpad:%2)").arg(label, QString::fromUtf8(QUrl::toPercentEncoding(target)));
                last = m.capturedEnd();
            }
            out += line.mid(last);
            line = out;
        }

        const bool heading = line.startsWith(QLatin1String("# ")) || line.startsWith(QLatin1String("##"));
        if (!heading)
            line.replace(tagRe, QStringLiteral("[#\\1](craftpad-tag:\\1)"));
    }
    return lines.join(QLatin1Char('\n'));
}

// ---------------------------------------------------------------------------
// HTML de cada trecho
// ---------------------------------------------------------------------------

QString MarkdownRenderer::inlineHtml(const QString &cellText, MdSegment *seg) const
{
    QString html = markdownToHtml(preprocess(cellText, seg)).trimmed();
    if (html.startsWith(QLatin1String("<p>")) && html.endsWith(QLatin1String("</p>")))
        html = html.mid(3, html.size() - 7);
    return html;
}

QString MarkdownRenderer::tableHtml(MdSegment &seg)
{
    const MarkdownTable &t = tbls.at(seg.tableIndex);
    const int cols = qMax(1, t.columnCount());

    FormulaEngine engine([this](const QString &name) -> const MarkdownTable * {
        for (const MarkdownTable &x : tbls)
            if (QString::compare(x.meta.name, name, Qt::CaseInsensitive) == 0)
                return &x;
        return externalLookup ? externalLookup(name) : nullptr;
    });

    QString html;
    if (t.meta.name.isEmpty()) {
        html += QStringLiteral("<p style=\"color:%1; font-size:small; margin:0\">%2</p>")
                .arg(style.muted.name(), QStringLiteral("⚠ Tabela sem nome — dê um nome em Formatar ▸ Tabela ▸ Propriedades"));
    }
    else {
        html += QStringLiteral("<p style=\"color:%1; font-size:small; margin:0\">▦ %2</p>").arg(style.muted.name(), t.meta.name.toHtmlEscaped());
    }

    const QColor border = t.meta.borderColor.isValid() ? t.meta.borderColor : style.tableBorder;
    const QString borderStyle = t.meta.borderStyle == QLatin1String("none") ? QStringLiteral("none") : t.meta.borderStyle;
    const int borderWidth = t.meta.borderStyle == QLatin1String("none") ? 0 : t.meta.borderWidth;
    html += QStringLiteral("<table cellspacing=\"0\" cellpadding=\"6\" border=\"%1\" style=\"border-color:%2; border-style:%3; border-collapse:collapse\">")
            .arg(borderWidth).arg(border.name(), borderStyle);

    seg.displayedCells.clear();
    for (int r = 0; r < t.rowCount(); ++r) {
        QStringList shown;
        html += QStringLiteral("<tr>");
        for (int c = 0; c < cols; ++c) {
            const QString raw = t.cell(r, c);
            QString content;
            bool error = false;
            if (FormulaEngine::isFormula(raw)) {
                const FormulaValue v = engine.cellValue(&t, r, c);
                error = v.isError();
                content = FormulaEngine::format(v).toHtmlEscaped();
            }
            else {
                content = inlineHtml(raw, &seg);
            }
            QColor bg = t.meta.shadeFor(r, c);
            // Texto legível sobre o sombreamento escolhido (temas claros e escuros)
            if (bg.isValid() && !error) {
                const bool lightBg = bg.lightnessF() > 0.55;
                const bool lightText = style.text.lightnessF() > 0.55;
                if (lightBg == lightText)
                    content = QStringLiteral("<span style=\"color:%1\">%2</span>").arg(lightBg ? QStringLiteral("#222228") : QStringLiteral("#F2F2F5"), content);
            }
            if (!bg.isValid() && r == 0) bg = style.tableHeader;
            const QString align = t.aligns.value(c);
            const QString tag = r == 0 ? QStringLiteral("th") : QStringLiteral("td");
            html += QStringLiteral("<%1%2%3>%4%5%6</%1>")
                    .arg(tag,
                         align.isEmpty() ? (r == 0 ? QStringLiteral(" align=\"left\"") : QString()) : QStringLiteral(" align=\"%1\"").arg(align),
                         bg.isValid() ? QStringLiteral(" bgcolor=\"%1\"").arg(bg.name()) : QString(),
                         error ? QStringLiteral("<span style=\"color:%1\">").arg(style.error.name()) : QString(),
                         content,
                         error ? QStringLiteral("</span>") : QString());
            shown << QTextDocumentFragment::fromHtml(content).toPlainText();
        }
        html += QStringLiteral("</tr>");
        seg.displayedCells << shown;
    }
    html += QStringLiteral("</table>");
    return html;
}

QString MarkdownRenderer::segmentHtml(MdSegment &seg)
{
    const QString text = srcLines.mid(seg.startLine, seg.endLine - seg.startLine).join(QLatin1Char('\n'));
    switch (seg.kind) {
    case MdSegment::Table:
        return tableHtml(seg);
    case MdSegment::Code:
        return markdownToHtml(text);
    case MdSegment::Aligned: {
        // O conteúdo entre <div align> e </div> é Markdown normal
        QStringList inner = srcLines.mid(seg.startLine + 1, qMax(0, seg.endLine - seg.startLine - 2));
        return QStringLiteral("<div align=\"%1\">%2</div>").arg(seg.alignment, markdownToHtml(preprocess(inner.join(QLatin1Char('\n')), &seg)));
    }
    default:
        return markdownToHtml(preprocess(text, &seg));
    }
}

QString MarkdownRenderer::styleSheet() const
{
    auto font = [](const QString &family) {
        return family.isEmpty() ? QString() : QStringLiteral("font-family:'%1';").arg(family);
    };
    const int h = style.headingSize;
    QString css;
    css += QStringLiteral("body { color:%1; }\n").arg(style.text.name());
    css += QStringLiteral("a { color:%1; text-decoration:none; }\n").arg(style.link.name());
    css += QStringLiteral("h1 { %1 font-size:%2pt; font-weight:600; }\n").arg(font(style.headingFont)).arg(h);
    css += QStringLiteral("h2 { %1 font-size:%2pt; font-weight:600; }\n").arg(font(style.headingFont)).arg(qRound(h * 0.8));
    css += QStringLiteral("h3 { %1 font-size:%2pt; font-weight:600; }\n").arg(font(style.headingFont)).arg(qRound(h * 0.67));
    css += QStringLiteral("h4, h5, h6 { %1 font-size:%2pt; font-weight:600; }\n").arg(font(style.headingFont)).arg(qMax(style.bodySize, qRound(h * 0.58)));
    css += QStringLiteral("code { %1 font-size:%2pt; background-color:%3; }\n").arg(font(style.codeFont)).arg(style.codeSize).arg(style.codeBackground.name());
    css += QStringLiteral("pre { %1 font-size:%2pt; background-color:%3; }\n").arg(font(style.codeFont)).arg(style.codeSize).arg(style.codeBackground.name());
    css += QStringLiteral("th { font-weight:600; }\n");
    css += QStringLiteral("blockquote { color:%1; }\n").arg(style.muted.name());
    return css;
}

void MarkdownRenderer::render(const QString &markdown, QTextDocument *doc)
{
    QString text = markdown;
    text.remove(QLatin1Char('\r'));
    srcLines = text.split(QLatin1Char('\n'));
    tbls = findMarkdownTables(srcLines);
    segs = segment(srcLines, tbls);

    QFont base = doc->defaultFont();
    if (!style.bodyFont.isEmpty()) base.setFamily(style.bodyFont);
    base.setPointSize(style.bodySize);
    doc->setDefaultFont(base);
    doc->setDefaultStyleSheet(styleSheet());

    doc->clear();
    QTextCursor cursor(doc);
    bool first = true;
    for (int i = 0; i < segs.size(); ++i) {
        MdSegment &seg = segs[i];
        if (seg.kind == MdSegment::Frontmatter)
            continue;

        const QString html = segmentHtml(seg);
        cursor.movePosition(QTextCursor::End);
        if (!first) {
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        }
        const int startPos = cursor.position();
        cursor.insertHtml(html);
        first = false;

        // Alinhamento de <div align> e <p align>: o insertHtml do Qt não aplica sozinho
        Qt::Alignment align = Qt::AlignLeft;
        QString alignName = seg.alignment;
        if (alignName.isEmpty() && seg.kind == MdSegment::Html) {
            static const QRegularExpression pAlign(QStringLiteral("^\\s*<(?:p|div)\\s+align=\"(left|center|right|justify)\""), QRegularExpression::CaseInsensitiveOption);
            alignName = pAlign.match(srcLines.value(seg.startLine)).captured(1).toLower();
        }
        if (alignName == QLatin1String("center")) align = Qt::AlignHCenter;
        else if (alignName == QLatin1String("right")) align = Qt::AlignRight;
        else if (alignName == QLatin1String("justify")) align = Qt::AlignJustify;

        // Marca os blocos com o número do trecho
        QTextBlock b = doc->findBlock(startPos);
        const int endPos = cursor.position();
        while (b.isValid() && b.position() <= endPos) {
            b.setUserState(i);
            if (!alignName.isEmpty() && !QTextCursor(b).currentTable()) {
                QTextCursor bc(b);
                QTextBlockFormat bf = b.blockFormat();
                bf.setAlignment(align);
                bc.setBlockFormat(bf);
            }
            b = b.next();
        }
    }
}

int MarkdownRenderer::segmentOfBlock(const QTextBlock &block)
{
    QTextBlock b = block;
    while (b.isValid()) {
        if (b.userState() >= 0)
            return b.userState();
        b = b.previous();
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Documento -> Markdown (edição na prévia)
// ---------------------------------------------------------------------------

namespace {

enum InlineFlag { Bold = 1, Italic = 2, Strike = 4, Underline = 8, Sup = 16, Sub = 32, Code = 64 };

int flagsFor(const QTextCharFormat &f)
{
    int flags = 0;
    if (f.fontFixedPitch() || f.fontFamilies().toStringList().join(QLatin1Char(',')).contains(QLatin1String("mono"), Qt::CaseInsensitive)
            || f.fontFamilies().toStringList().join(QLatin1Char(',')).contains(QLatin1String("courier"), Qt::CaseInsensitive))
        return Code;
    if (f.fontWeight() >= QFont::DemiBold) flags |= Bold;
    if (f.fontItalic()) flags |= Italic;
    if (f.fontStrikeOut()) flags |= Strike;
    if (f.fontUnderline() && !f.isAnchor()) flags |= Underline;
    if (f.verticalAlignment() == QTextCharFormat::AlignSuperScript) flags |= Sup;
    if (f.verticalAlignment() == QTextCharFormat::AlignSubScript) flags |= Sub;
    return flags;
}

QString openMarker(int flag)
{
    switch (flag) {
    case Bold: return QStringLiteral("**");
    case Italic: return QStringLiteral("*");
    case Strike: return QStringLiteral("~~");
    case Underline: return QStringLiteral("<u>");
    case Sup: return QStringLiteral("<sup>");
    case Sub: return QStringLiteral("<sub>");
    case Code: return QStringLiteral("`");
    }
    return QString();
}

QString closeMarker(int flag)
{
    switch (flag) {
    case Underline: return QStringLiteral("</u>");
    case Sup: return QStringLiteral("</sup>");
    case Sub: return QStringLiteral("</sub>");
    default: return openMarker(flag);
    }
}

QString escapeText(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('*') || ch == QLatin1Char('`') || ch == QLatin1Char('\\'))
            out += QLatin1Char('\\');
        else if (ch == QLatin1Char('_')) {
            const bool prevWord = i > 0 && text.at(i - 1).isLetterOrNumber();
            const bool nextWord = i + 1 < text.size() && text.at(i + 1).isLetterOrNumber();
            if (!(prevWord && nextWord)) out += QLatin1Char('\\');
        }
        if (ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator) { out += QLatin1Char('\n'); continue; }
        if (ch == QChar::Nbsp) { out += QLatin1Char(' '); continue; }
        out += ch;
    }
    return out;
}

// Texto de um bloco em Markdown inline
QString inlineMarkdown(const QTextBlock &block, const MdSegment &seg, QHash<QString, int> *imageUse)
{
    const bool heading = block.blockFormat().headingLevel() > 0;
    QString out;
    QList<int> open;           // marcadores abertos, na ordem
    QString linkHref, linkText;
    bool inLink = false;
    QString *target = &out;

    auto closeAll = [&](int keep) {
        // Fecha os marcadores que não continuam, do mais interno para fora
        int i = open.size() - 1;
        while (i >= 0) {
            bool closeIt = !(keep & open.at(i));
            if (!closeIt) {
                // Se algum marcador mais interno fecha, os de fora continuam
                --i;
                continue;
            }
            // Fecha este e todos os mais internos, reabrindo depois os que continuam
            QList<int> reopen;
            for (int j = open.size() - 1; j >= i; --j) {
                *target += closeMarker(open.at(j));
                if (j != i && (keep & open.at(j))) reopen.prepend(open.at(j));
            }
            open = open.mid(0, i);
            for (int f : reopen) { *target += openMarker(f); open << f; }
            i = open.size() - 1;
        }
    };

    auto finishLink = [&]() {
        if (!inLink) return;
        closeAll(0);
        target = &out;
        const QUrl url(linkHref);
        if (url.scheme() == QLatin1String("craftpad")) {
            const QString note = QUrl::fromPercentEncoding(linkHref.mid(9).toUtf8());
            out += linkText == note ? QStringLiteral("[[%1]]").arg(note) : QStringLiteral("[[%1|%2]]").arg(note, linkText);
        }
        else if (url.scheme() == QLatin1String("craftpad-tag")) {
            out += QLatin1Char('#') + linkHref.mid(13);
        }
        else if (linkText == linkHref || QStringLiteral("http://") + linkText == linkHref || QStringLiteral("https://") + linkText == linkHref) {
            out += linkHref;
        }
        else {
            out += QStringLiteral("[%1](%2)").arg(linkText, linkHref);
        }
        inLink = false;
        linkHref.clear();
        linkText.clear();
    };

    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        if (!frag.isValid()) continue;
        const QTextCharFormat fmt = frag.charFormat();

        // Links
        const QString href = fmt.isAnchor() ? fmt.anchorHref() : QString();
        if (inLink && href != linkHref) finishLink();
        if (!href.isEmpty() && !inLink) {
            closeAll(0);
            inLink = true;
            linkHref = href;
            target = &linkText;
        }

        if (fmt.isImageFormat()) {
            closeAll(0);
            const QString name = fmt.toImageFormat().name();
            const int used = imageUse->value(name);
            int seen = 0;
            QString markup = QStringLiteral("![](%1)").arg(name);
            for (const auto &p : seg.images) {
                if (p.first == name) {
                    if (seen == used) { markup = p.second; break; }
                    ++seen;
                }
            }
            imageUse->insert(name, used + 1);
            *target += markup;
            continue;
        }

        int flags = flagsFor(fmt);
        if (heading || QTextCursor(block).currentTable()) flags &= ~Bold; // títulos e cabeçalhos já são negrito

        // Espaços nas pontas ficam fora dos marcadores (** texto** não funciona)
        const QString text = frag.text();
        int a = 0, z = text.size();
        if (!(flags & Code)) {
            while (a < z && text.at(a).isSpace()) ++a;
            while (z > a && text.at(z - 1).isSpace()) --z;
        }
        if (a == z && !(flags & Code)) {
            *target += escapeText(text);
            continue;
        }
        if (a > 0) { closeAll(flags); *target += escapeText(text.left(a)); }
        closeAll(flags);
        for (int f : {Code, Bold, Italic, Strike, Underline, Sup, Sub}) {
            if ((flags & f) && !open.contains(f)) {
                *target += openMarker(f);
                open << f;
            }
        }
        const QString core = text.mid(a, z - a);
        *target += (flags & Code) ? core : escapeText(core);
        if (z < text.size()) { closeAll(0); *target += escapeText(text.mid(z)); }
    }
    closeAll(0);
    finishLink();
    while (out.endsWith(QLatin1Char(' ')) || out.endsWith(QLatin1Char('\t'))) out.chop(1);
    while (out.startsWith(QLatin1Char(' '))) out.remove(0, 1);

    // Marcadores de bloco no início do texto não podem virar sintaxe
    static const QRegularExpression leading(QStringLiteral("^(#{1,6} |[-+] |\\d+[.)] |> )"));
    if (leading.match(out).hasMatch() && seg.kind != MdSegment::Heading)
        out.prepend(QLatin1Char('\\'));
    return out;
}

QString alignmentName(Qt::Alignment a)
{
    if (a & Qt::AlignHCenter) return QStringLiteral("center");
    if (a & Qt::AlignRight) return QStringLiteral("right");
    if (a & Qt::AlignJustify) return QStringLiteral("justify");
    return QString();
}

} // namespace

QString MarkdownRenderer::blocksMarkdown(QTextDocument *doc, int segmentIndex, bool ignoreAlignment) const
{
    const MdSegment &seg = segs.at(segmentIndex);
    struct Item { QString text; QTextList *list; QString align; };
    QList<Item> items;
    QHash<QString, int> imageUse;

    int current = -1;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b.userState() >= 0) current = b.userState();
        if (current != segmentIndex) continue;
        if (QTextCursor(b).currentTable()) continue;

        const QString content = inlineMarkdown(b, seg, &imageUse);
        const QTextBlockFormat bf = b.blockFormat();
        QString line;
        QTextList *isList = nullptr;

        if (bf.headingLevel() > 0) {
            line = QString(bf.headingLevel(), QLatin1Char('#')) + QLatin1Char(' ') + content;
        }
        else if (QTextList *list = b.textList()) {
            isList = list;
            const QTextListFormat lf = list->format();
            const QString indent(qMax(0, lf.indent() - 1) * 4, QLatin1Char(' '));
            QString bullet;
            switch (lf.style()) {
            case QTextListFormat::ListDecimal:
            case QTextListFormat::ListLowerAlpha:
            case QTextListFormat::ListUpperAlpha:
            case QTextListFormat::ListLowerRoman:
            case QTextListFormat::ListUpperRoman:
                bullet = QStringLiteral("%1. ").arg(list->itemNumber(b) + 1);
                break;
            default:
                bullet = QStringLiteral("- ");
            }
            if (bf.marker() == QTextBlockFormat::MarkerType::Checked) bullet += QStringLiteral("[x] ");
            else if (bf.marker() == QTextBlockFormat::MarkerType::Unchecked) bullet += QStringLiteral("[ ] ");
            line = indent + bullet + content;
        }
        else {
            if (content.trimmed().isEmpty()) continue;
            line = content;
        }
        if (seg.kind == MdSegment::Quote) {
            QStringList ql = line.split(QLatin1Char('\n'));
            for (QString &q : ql) q.prepend(QStringLiteral("> "));
            line = ql.join(QLatin1Char('\n'));
        }
        items << Item{line, isList, ignoreAlignment ? QString() : alignmentName(bf.alignment())};
    }

    QString out;
    for (int i = 0; i < items.size(); ++i) {
        const Item &it = items.at(i);
        const bool groupStart = i == 0 || items.at(i - 1).align != it.align;
        const bool groupEnd = i + 1 == items.size() || items.at(i + 1).align != it.align;
        if (i > 0)
            out += (it.list && items.at(i - 1).list && (items.at(i - 1).list == it.list || items.at(i - 1).list->format().indent() != it.list->format().indent()) && !groupStart) ? QStringLiteral("\n")
                 : (seg.kind == MdSegment::Quote ? QStringLiteral("\n>\n") : QStringLiteral("\n\n"));
        if (groupStart && !it.align.isEmpty()) out += QStringLiteral("<div align=\"%1\">\n\n").arg(it.align);
        out += it.text;
        if (groupEnd && !it.align.isEmpty()) out += QStringLiteral("\n\n</div>");
    }
    return out;
}

QString MarkdownRenderer::tableMarkdown(QTextDocument *doc, int segmentIndex) const
{
    const MdSegment &seg = segs.at(segmentIndex);
    MarkdownTable t = tbls.at(seg.tableIndex);

    QTextTable *table = nullptr;
    int current = -1;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b.userState() >= 0) current = b.userState();
        if (current == segmentIndex) {
            if ((table = QTextCursor(b).currentTable()))
                break;
        }
        else if (current > segmentIndex) {
            break;
        }
    }
    if (!table)
        return QString(); // a tabela foi apagada na prévia

    QVector<QStringList> rows;
    QHash<QString, int> imageUse;
    for (int r = 0; r < table->rows(); ++r) {
        QStringList row;
        for (int c = 0; c < table->columns(); ++c) {
            const QTextTableCell cell = table->cellAt(r, c);
            QTextCursor cc = cell.firstCursorPosition();
            cc.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
            const QString shown = cc.selection().toPlainText();
            if (r < seg.displayedCells.size() && c < seg.displayedCells.at(r).size() && shown == seg.displayedCells.at(r).at(c)) {
                row << t.cell(r, c); // sem mudança: mantém o original (fórmulas incluídas)
                continue;
            }
            QStringList parts;
            for (QTextBlock b = cell.firstCursorPosition().block(); b.isValid() && b.position() <= cell.lastCursorPosition().position(); b = b.next())
                parts << inlineMarkdown(b, seg, &imageUse);
            row << parts.join(QStringLiteral("<br>")).replace(QLatin1Char('|'), QStringLiteral("\\|"));
        }
        rows << row;
    }
    t.rows = rows;
    return t.toMarkdown();
}

QString MarkdownRenderer::segmentMarkdown(QTextDocument *doc, int segmentIndex) const
{
    if (segmentIndex < 0 || segmentIndex >= segs.size())
        return QString();
    const MdSegment &seg = segs.at(segmentIndex);

    switch (seg.kind) {
    case MdSegment::Frontmatter:
    case MdSegment::Rule:
        return srcLines.mid(seg.startLine, seg.endLine - seg.startLine).join(QLatin1Char('\n'));
    case MdSegment::Table:
        return tableMarkdown(doc, segmentIndex);
    case MdSegment::Code: {
        QStringList body;
        int current = -1;
        bool any = false;
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            if (b.userState() >= 0) current = b.userState();
            if (current == segmentIndex) { body << b.text(); any = true; }
        }
        if (!any) return QString();
        while (!body.isEmpty() && body.last().trimmed().isEmpty()) body.removeLast();
        QString close = seg.fenceClose.isEmpty() ? seg.fenceOpen.trimmed().left(3) : seg.fenceClose;
        return seg.fenceOpen + QLatin1Char('\n') + body.join(QLatin1Char('\n')) + QLatin1Char('\n') + close;
    }
    case MdSegment::Aligned: {
        const QString inner = blocksMarkdown(doc, segmentIndex, true);
        if (inner.isEmpty()) return QString();
        return QStringLiteral("<div align=\"%1\">\n\n%2\n\n</div>").arg(seg.alignment, inner);
    }
    default:
        return blocksMarkdown(doc, segmentIndex, false);
    }
}
