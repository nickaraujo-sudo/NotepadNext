/*
 * CraftPad - extensão do Notepad Next no estilo Obsidian.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#include "WikiLinkDecorator.h"
#include "CraftPadHub.h"
#include "VaultIndex.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>

static constexpr int LinkListType = 7201;
static constexpr int TagListType = 7202;

// Scintilla usa cores no formato BGR
static int toBgr(unsigned int rgb)
{
    return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}

WikiLinkDecorator::WikiLinkDecorator(ScintillaNext *editor) :
    EditorDecorator(editor),
    highlightTimer(new QTimer(this)),
    indexTimer(new QTimer(this))
{
    linkIndicator = editor->allocateIndicator(QStringLiteral("craftpad_link"));
    editor->indicSetStyle(linkIndicator, INDIC_TEXTFORE);
    editor->indicSetFore(linkIndicator, toBgr(CraftPadHub::LinkColor));
    editor->indicSetHoverStyle(linkIndicator, INDIC_PLAIN);
    editor->indicSetHoverFore(linkIndicator, toBgr(CraftPadHub::LinkColor));

    tagIndicator = editor->allocateIndicator(QStringLiteral("craftpad_tag"));
    editor->indicSetStyle(tagIndicator, INDIC_ROUNDBOX);
    editor->indicSetFore(tagIndicator, toBgr(CraftPadHub::TagColor));
    editor->indicSetAlpha(tagIndicator, 45);
    editor->indicSetOutlineAlpha(tagIndicator, 140);
    editor->indicSetUnder(tagIndicator, true);

    highlightTimer->setSingleShot(true);
    highlightTimer->setInterval(150);
    connect(highlightTimer, &QTimer::timeout, this, &WikiLinkDecorator::highlight);

    // Atualiza o índice do vault ao vivo (para backlinks e grafo)
    indexTimer->setSingleShot(true);
    indexTimer->setInterval(900);
    connect(indexTimer, &QTimer::timeout, this, &WikiLinkDecorator::pushToIndex);

    connect(this, &EditorDecorator::stateChanged, this, [this](bool on) {
        if (on) {
            connect(this->editor, &ScintillaNext::resized, highlightTimer, qOverload<>(&QTimer::start));
            connect(this->editor, &ScintillaNext::lexerChanged, highlightTimer, qOverload<>(&QTimer::start));
            connect(this->editor, &ScintillaNext::reloaded, highlightTimer, qOverload<>(&QTimer::start));
            connect(this->editor, &ScintillaNext::renamed, highlightTimer, qOverload<>(&QTimer::start));
            highlightTimer->start();
        }
        else {
            disconnect(this->editor, nullptr, highlightTimer, nullptr);
            clearAll();
        }
    });
}

bool WikiLinkDecorator::isMarkdown() const
{
    if (editor->languageName.compare(QLatin1String("Markdown"), Qt::CaseInsensitive) == 0)
        return true;
    const QString suffix = editor->getFileInfo().suffix().toLower();
    return suffix == QLatin1String("md") || suffix == QLatin1String("markdown");
}

void WikiLinkDecorator::clearAll()
{
    editor->setIndicatorCurrent(linkIndicator);
    editor->indicatorClearRange(0, editor->length());
    editor->setIndicatorCurrent(tagIndicator);
    editor->indicatorClearRange(0, editor->length());
}

void WikiLinkDecorator::highlight()
{
    clearAll();
    if (!isMarkdown())
        return;

    static const QRegularExpression linkRe(QStringLiteral(R"(!?\[\[[^\]\n]+\]\])"));
    static const QRegularExpression tagRe(QStringLiteral(R"((?<![\p{L}\p{N}_/#&"'=:\]])#[\p{L}\p{N}_\-/]*[\p{L}_\-][\p{L}\p{N}_\-/]*)"));

    // Só processa as linhas visíveis, como o destaque de URLs
    int line = editor->docLineFromVisible(editor->firstVisibleLine());
    int remaining = editor->linesOnScreen() + 1;

    while (remaining >= 0 && line < editor->lineCount()) {
        if (!editor->lineVisible(line)) {
            ++line;
            continue;
        }

        const int lineStart = editor->positionFromLine(line);
        const QByteArray bytes = editor->get_text_range(lineStart, editor->lineEndPosition(line));
        const QString text = QString::fromUtf8(bytes);

        // Converte posição em caracteres para posição em bytes (UTF-8)
        auto byteOffset = [&](int charPos) { return static_cast<int>(text.left(charPos).toUtf8().size()); };
        auto fill = [&](int indicator, const QRegularExpressionMatch &m) {
            const int start = byteOffset(m.capturedStart());
            const int end = byteOffset(m.capturedEnd());
            editor->setIndicatorCurrent(indicator);
            editor->indicatorFillRange(lineStart + start, end - start);
        };

        // Linhas de código (4 espaços ou crase) e títulos não têm tags
        const bool isHeading = text.startsWith(QLatin1String("# ")) || text.startsWith(QLatin1String("##"));

        auto it = linkRe.globalMatch(text);
        while (it.hasNext())
            fill(linkIndicator, it.next());

        if (!isHeading) {
            // Trechos entre crases são código: ali não há tags
            static const QRegularExpression codeRe(QStringLiteral("`[^`]*`"));
            QVector<QPair<int, int>> code;
            auto cit = codeRe.globalMatch(text);
            while (cit.hasNext()) {
                const auto m = cit.next();
                code.append({m.capturedStart(), m.capturedEnd()});
            }

            it = tagRe.globalMatch(text);
            while (it.hasNext()) {
                const auto m = it.next();
                const bool inCode = std::any_of(code.cbegin(), code.cend(), [&](const auto &r) {
                    return m.capturedStart() >= r.first && m.capturedStart() < r.second;
                });
                if (!inCode)
                    fill(tagIndicator, m);
            }
        }

        remaining -= editor->wrapCount(line);
        ++line;
    }
}

void WikiLinkDecorator::pushToIndex()
{
    VaultIndex *index = CraftPadHub::instance()->index();
    if (!editor->isFile() || !index->contains(editor->getFilePath()))
        return;

    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(editor->characterPointer()), editor->textLength());
    index->updateNote(editor->getFilePath(), text);
}

void WikiLinkDecorator::showLinkList()
{
    const QStringList names = CraftPadHub::instance()->index()->noteNames();
    if (names.isEmpty())
        return;

    listAnchor = editor->currentPos();
    editor->autoCSetSeparator('\n');
    editor->autoCSetIgnoreCase(true);
    editor->autoCSetOrder(SC_ORDER_PERFORMSORT);
    editor->autoCStops("]|#\n");
    editor->userListShow(LinkListType, names.join(QLatin1Char('\n')).toUtf8().constData());
}

void WikiLinkDecorator::showTagList()
{
    const QStringList tags = CraftPadHub::instance()->index()->tags().keys();
    if (tags.isEmpty())
        return;

    listAnchor = editor->currentPos();
    editor->autoCSetSeparator('\n');
    editor->autoCSetIgnoreCase(true);
    editor->autoCSetOrder(SC_ORDER_PERFORMSORT);
    editor->autoCStops(" \t\n#");
    editor->userListShow(TagListType, tags.join(QLatin1Char('\n')).toUtf8().constData());
}

void WikiLinkDecorator::notify(const Scintilla::NotificationData *pscn)
{
    using Scintilla::Notification;

    switch (pscn->nmhdr.code) {
    case Notification::UpdateUI:
        if (FlagSet(pscn->updated, Scintilla::Update::VScroll))
            highlightTimer->start();
        break;

    case Notification::Modified:
        if (FlagSet(pscn->modificationType, Scintilla::ModificationFlags::InsertText)
                || FlagSet(pscn->modificationType, Scintilla::ModificationFlags::DeleteText)) {
            highlightTimer->start();
            indexTimer->start();
        }
        break;

    case Notification::Zoom:
        highlightTimer->start();
        break;

    case Notification::CharAdded: {
        if (!isMarkdown())
            break;
        const int pos = editor->currentPos();
        if (pscn->ch == '[' && pos >= 2 && editor->charAt(pos - 2) == '[') {
            showLinkList();
        }
        else if (pscn->ch == '#') {
            // Só no início de palavra, e não em títulos (# no começo da linha)
            const int lineStart = editor->positionFromLine(editor->lineFromPosition(pos));
            const char prev = pos >= 2 ? static_cast<char>(editor->charAt(pos - 2)) : ' ';
            if (pos - 1 > lineStart && (prev == ' ' || prev == '\t'))
                showTagList();
        }
        break;
    }

    case Notification::UserListSelection: {
        if ((pscn->listType != LinkListType && pscn->listType != TagListType) || !pscn->text || listAnchor < 0)
            break;

        QByteArray insert(pscn->text);
        const int pos = editor->currentPos();
        if (pscn->listType == LinkListType && editor->get_text_range(pos, pos + 2) != "]]")
            insert += "]]";

        editor->setTargetRange(listAnchor, pos);
        editor->replaceTarget(insert.size(), insert.constData());
        int end = listAnchor + static_cast<int>(insert.size());
        if (pscn->listType == LinkListType && !insert.endsWith("]]"))
            end += 2; // pula o "]]" que já existia
        editor->gotoPos(end);
        listAnchor = -1;
        editor->autoCStops("");
        break;
    }

    case Notification::AutoCCancelled:
        listAnchor = -1;
        editor->autoCStops("");
        break;

    case Notification::IndicatorClick: {
        // Cmd+clique no Mac (Ctrl no Windows/Linux)
        if (!FlagSet(pscn->modifiers, Scintilla::KeyMod::Ctrl))
            break;

        const int indicators = editor->indicatorAllOnFor(pscn->position);
        if (indicators & (1 << linkIndicator)) {
            const int start = editor->indicatorStart(linkIndicator, pscn->position);
            const int end = editor->indicatorEnd(linkIndicator, pscn->position);
            QString link = QString::fromUtf8(editor->get_text_range(start, end));
            if (link.startsWith(QLatin1Char('!')))
                link.remove(0, 1);
            link = link.mid(2, link.size() - 4);         // tira [[ ]]
            link = link.section(QLatin1Char('|'), 0, 0); // tira o apelido
            link = link.section(QLatin1Char('#'), 0, 0); // tira a seção
            link = link.section(QLatin1Char('^'), 0, 0);
            if (!link.trimmed().isEmpty()) {
                // Adia para não abrir outro arquivo no meio do evento de clique
                const QString target = link.trimmed();
                const QString from = editor->getFilePath();
                QTimer::singleShot(0, CraftPadHub::instance(), [target, from]() {
                    emit CraftPadHub::instance()->openNoteRequested(target, from);
                });
            }
        }
        else if (indicators & (1 << tagIndicator)) {
            const int start = editor->indicatorStart(tagIndicator, pscn->position);
            const int end = editor->indicatorEnd(tagIndicator, pscn->position);
            const QString tag = QString::fromUtf8(editor->get_text_range(start + 1, end));
            emit CraftPadHub::instance()->tagRequested(tag);
        }
        break;
    }

    default:
        break;
    }
}
