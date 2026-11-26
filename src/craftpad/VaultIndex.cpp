/*
 * CraftPad - extensão do Notepad Next no estilo Obsidian.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#include "VaultIndex.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

static bool isMarkdownFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("md") || suffix == QLatin1String("markdown");
}

// Remove ".md" e barras invertidas do alvo de um link
static QString normalizeTarget(QString target)
{
    target = target.trimmed();
    target.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (target.endsWith(QLatin1String(".md"), Qt::CaseInsensitive))
        target.chop(3);
    return target;
}

VaultIndex::VaultIndex(QObject *parent) :
    QObject(parent),
    watcher(new QFileSystemWatcher(this)),
    rescanTimer(new QTimer(this))
{
    // Junta várias mudanças de arquivo numa única releitura
    rescanTimer->setSingleShot(true);
    rescanTimer->setInterval(400);
    connect(rescanTimer, &QTimer::timeout, this, &VaultIndex::rescan);

    connect(watcher, &QFileSystemWatcher::directoryChanged, rescanTimer, qOverload<>(&QTimer::start));
    connect(watcher, &QFileSystemWatcher::fileChanged, rescanTimer, qOverload<>(&QTimer::start));
}

void VaultIndex::setRoot(const QString &path)
{
    const QString clean = path.isEmpty() ? QString() : QDir(path).absolutePath();
    if (clean == rootPath)
        return;

    rootPath = clean;
    rescan();
}

bool VaultIndex::contains(const QString &path) const
{
    if (rootPath.isEmpty() || path.isEmpty())
        return false;
    const QString abs = QFileInfo(path).absoluteFilePath();
    return abs.startsWith(rootPath + QLatin1Char('/'));
}

NoteInfo VaultIndex::parse(const QString &path, const QString &text)
{
    NoteInfo info;
    info.path = path;
    info.name = QFileInfo(path).completeBaseName();

    // Tags no estilo Obsidian: #tag, #tag/subtag, precisa ter ao menos uma letra
    static const QRegularExpression tagRe(QStringLiteral(R"((?<![\p{L}\p{N}_/#&\]])#([\p{L}\p{N}_\-/]*[\p{L}_\-][\p{L}\p{N}_\-/]*))"));
    // Links internos: [[Nota]], [[Nota|apelido]], [[Nota#Seção]], ![[imagem.png]]
    static const QRegularExpression wikiRe(QStringLiteral(R"(\[\[([^\]\|#\^\n]+)(?:[#\^][^\]\|\n]*)?(?:\|[^\]\n]*)?\]\])"));
    // Links Markdown para outras notas: [texto](nota.md)
    static const QRegularExpression mdLinkRe(QStringLiteral(R"(\[[^\]\n]*\]\((?!https?:|mailto:)([^)\s]+\.md)\))"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression inlineCodeRe(QStringLiteral("`[^`\n]*`"));

    const QStringList lines = text.split(QLatin1Char('\n'));
    QSet<QString> tagSet;
    bool inFence = false;
    bool inFrontmatter = false;
    bool inFrontTags = false;

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QString trimmed = line.trimmed();

        // Frontmatter YAML no topo do arquivo: tags: [a, b] ou lista
        if (i == 0 && trimmed == QLatin1String("---")) {
            inFrontmatter = true;
            continue;
        }
        if (inFrontmatter) {
            if (trimmed == QLatin1String("---") || trimmed == QLatin1String("...")) {
                inFrontmatter = false;
                continue;
            }
            if (trimmed.startsWith(QLatin1String("tags:")) || trimmed.startsWith(QLatin1String("tag:"))) {
                QString rest = trimmed.mid(trimmed.indexOf(QLatin1Char(':')) + 1).trimmed();
                rest.remove(QLatin1Char('[')).remove(QLatin1Char(']'));
                inFrontTags = rest.isEmpty();
                for (QString t : rest.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts)) {
                    t.remove(QLatin1Char('"')).remove(QLatin1Char('\''));
                    if (t.startsWith(QLatin1Char('#'))) t.remove(0, 1);
                    if (!t.isEmpty()) tagSet.insert(t);
                }
            }
            else if (inFrontTags && trimmed.startsWith(QLatin1String("- "))) {
                QString t = trimmed.mid(2).trimmed();
                t.remove(QLatin1Char('"')).remove(QLatin1Char('\''));
                if (t.startsWith(QLatin1Char('#'))) t.remove(0, 1);
                if (!t.isEmpty()) tagSet.insert(t);
            }
            else {
                inFrontTags = false;
            }
            continue;
        }

        // Ignora blocos de código
        if (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"))) {
            inFence = !inFence;
            continue;
        }
        if (inFence)
            continue;

        line.remove(inlineCodeRe);

        auto it = tagRe.globalMatch(line);
        while (it.hasNext())
            tagSet.insert(it.next().captured(1));

        it = wikiRe.globalMatch(line);
        while (it.hasNext()) {
            const QString target = normalizeTarget(it.next().captured(1));
            if (!target.isEmpty())
                info.links.append({target, i});
        }

        it = mdLinkRe.globalMatch(line);
        while (it.hasNext()) {
            const QString raw = QUrl::fromPercentEncoding(it.next().captured(1).toUtf8());
            // Caminho relativo à própria nota
            const QString abs = QFileInfo(QFileInfo(path).dir(), raw).absoluteFilePath();
            info.links.append({QStringLiteral("\x01") + abs, i});
        }
    }

    info.tags = tagSet.values();
    info.tags.sort(Qt::CaseInsensitive);
    return info;
}

void VaultIndex::rescan()
{
    noteMap.clear();
    nameLookup.clear();

    if (!rootPath.isEmpty() && QDir(rootPath).exists()) {
        QDirIterator it(rootPath, {QStringLiteral("*.md"), QStringLiteral("*.markdown")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            // Pula pastas ocultas como .obsidian e .git
            if (path.mid(rootPath.size()).contains(QLatin1String("/.")))
                continue;

            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            noteMap.insert(path, parse(path, QString::fromUtf8(f.readAll())));
        }
    }

    // Tabela para resolver [[Nome]] e [[Pasta/Nome]]
    for (const NoteInfo &note : std::as_const(noteMap)) {
        QString rel = QDir(rootPath).relativeFilePath(note.path);
        rel = normalizeTarget(rel).toLower();
        nameLookup.insert(rel, note.path);
        const QString shortName = note.name.toLower();
        if (!nameLookup.contains(shortName))
            nameLookup.insert(shortName, note.path);
    }

    watchDirectories();
    emit indexChanged();
}

void VaultIndex::updateNote(const QString &path, const QString &text)
{
    if (!contains(path) || !isMarkdownFile(path))
        return;

    const bool isNew = !noteMap.contains(path);
    noteMap.insert(path, parse(path, text));

    if (isNew) {
        rescanTimer->start();
        return;
    }
    emit indexChanged();
}

void VaultIndex::watchDirectories()
{
    if (!watcher->directories().isEmpty())
        watcher->removePaths(watcher->directories());
    if (!watcher->files().isEmpty())
        watcher->removePaths(watcher->files());

    if (rootPath.isEmpty())
        return;

    QStringList dirs{rootPath};
    QDirIterator it(rootPath, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString d = it.next();
        if (!d.mid(rootPath.size()).contains(QLatin1String("/.")))
            dirs << d;
    }
    watcher->addPaths(dirs);
    if (!noteMap.isEmpty())
        watcher->addPaths(noteMap.keys());
}

QStringList VaultIndex::noteNames() const
{
    QStringList names;
    for (const NoteInfo &note : noteMap)
        names << note.name;
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

QMap<QString, QStringList> VaultIndex::tags() const
{
    QMap<QString, QStringList> result;
    for (const NoteInfo &note : noteMap) {
        for (const QString &tag : note.tags) {
            result[tag].append(note.path);
        }
    }
    for (QStringList &list : result) {
        list.removeDuplicates();
        list.sort(Qt::CaseInsensitive);
    }
    return result;
}

QString VaultIndex::resolveLink(const QString &target) const
{
    if (target.startsWith(QLatin1Char('\x01'))) {
        const QString abs = target.mid(1);
        return noteMap.contains(abs) ? abs : QString();
    }

    const QString key = normalizeTarget(target).toLower();
    if (nameLookup.contains(key))
        return nameLookup.value(key);

    // [[Pasta/Nome]] quando só o nome bate
    const QString last = key.section(QLatin1Char('/'), -1);
    return nameLookup.value(last);
}

QList<QPair<QString, int>> VaultIndex::backlinks(const QString &path) const
{
    QList<QPair<QString, int>> result;
    for (const NoteInfo &note : noteMap) {
        if (note.path == path)
            continue;
        for (const NoteLink &link : note.links) {
            if (resolveLink(link.target) == path)
                result.append({note.path, link.line});
        }
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.first.compare(b.first, Qt::CaseInsensitive) < 0 || (a.first == b.first && a.second < b.second);
    });
    return result;
}

QString VaultIndex::createNote(const QString &target)
{
    if (rootPath.isEmpty())
        return QString();

    QString rel = normalizeTarget(target);
    // Evita nomes inválidos no macOS/Windows
    rel.replace(QRegularExpression(QStringLiteral(R"([:*?"<>|])")), QStringLiteral("-"));
    const QString path = QDir(rootPath).absoluteFilePath(rel + QStringLiteral(".md"));

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.exists() && f.open(QIODevice::WriteOnly)) {
        f.write(QStringLiteral("# %1\n\n").arg(QFileInfo(path).completeBaseName()).toUtf8());
        f.close();
    }

    noteMap.insert(path, parse(path, QString()));
    nameLookup.insert(rel.toLower(), path);
    nameLookup.insert(QFileInfo(path).completeBaseName().toLower(), path);
    rescanTimer->start();
    return path;
}
