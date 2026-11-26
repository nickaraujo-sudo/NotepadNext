/*
 * craftpad++ - extensão do Notepad Next.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#ifndef MARKDOWNRENDERER_H
#define MARKDOWNRENDERER_H

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

#include "MarkdownTable.h"

class QTextDocument;
class QTextBlock;
class QTextTable;

// Um trecho do Markdown que vira um bloco na prévia. A prévia guarda o
// número do trecho em cada parágrafo, para devolver edições ao arquivo.
struct MdSegment {
    enum Kind { Frontmatter, Paragraph, Heading, List, Quote, Code, Table, Aligned, Html, Rule };
    Kind kind = Paragraph;
    int startLine = 0;   // primeira linha no arquivo
    int endLine = 0;     // linha depois da última
    int tableIndex = -1;
    QString alignment;   // trechos <div align="...">
    QString fenceOpen, fenceClose;
    // Para comparar edições em tabelas: texto mostrado em cada célula
    QVector<QStringList> displayedCells;
    // Imagens: caminho resolvido -> marcação original (na ordem)
    QList<QPair<QString, QString>> images;
};

struct PreviewStyle {
    QColor text = QColor(0x22, 0x22, 0x28);
    QColor muted = QColor(0x80, 0x80, 0x8C);
    QColor link = QColor(0x9B, 0x6B, 0xE0);
    QColor tag = QColor(0xFF, 0x2E, 0x97);
    QColor codeBackground = QColor(0xF3, 0xF1, 0xF6);
    QColor tableBorder = QColor(0xC8, 0xC4, 0xD4);
    QColor tableHeader = QColor(0xF1, 0xEC, 0xFA);
    QColor error = QColor(0xC6, 0x28, 0x28);
    QString bodyFont;       // vazio = padrão do sistema
    int bodySize = 13;
    QString headingFont;
    int headingSize = 24;   // H1; H2..H6 proporcionais
    QString codeFont;
    int codeSize = 12;
};

class MarkdownRenderer
{
public:
    using TableLookup = std::function<const MarkdownTable *(const QString &name)>;
    using AttachmentResolver = std::function<QString(const QString &name)>;

    void setTableLookup(TableLookup f) { externalLookup = std::move(f); }
    void setAttachmentResolver(AttachmentResolver f) { resolveAttachment = std::move(f); }
    void setStyle(const PreviewStyle &s) { style = s; }

    // Monta a prévia no documento
    void render(const QString &markdown, QTextDocument *doc);

    const QList<MdSegment> &segments() const { return segs; }
    const QList<MarkdownTable> &tables() const { return tbls; }
    const QStringList &lines() const { return srcLines; }

    // Markdown de um trecho, a partir do que está no documento agora
    QString segmentMarkdown(QTextDocument *doc, int segmentIndex) const;
    // Trecho ao qual o bloco pertence (blocos novos herdam o anterior)
    static int segmentOfBlock(const QTextBlock &block);

    // Partes reutilizáveis (também usadas nos testes)
    static QList<MdSegment> segment(const QStringList &lines, const QList<MarkdownTable> &tables);
    static QString markdownToHtml(const QString &markdown);
    QString preprocess(const QString &markdown, MdSegment *seg) const;
    QString styleSheet() const;

private:
    QString segmentHtml(MdSegment &seg);
    QString tableHtml(MdSegment &seg);
    QString inlineHtml(const QString &cellText, MdSegment *seg) const;
    QString blocksMarkdown(QTextDocument *doc, int segmentIndex, bool ignoreAlignment) const;
    QString tableMarkdown(QTextDocument *doc, int segmentIndex) const;

    QStringList srcLines;
    QList<MdSegment> segs;
    QList<MarkdownTable> tbls;
    TableLookup externalLookup;
    AttachmentResolver resolveAttachment;
    PreviewStyle style;
};

#endif // MARKDOWNRENDERER_H
