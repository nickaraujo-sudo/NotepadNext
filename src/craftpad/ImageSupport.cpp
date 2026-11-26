/*
 * craftpad++ - extensão do Notepad Next.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#include "ImageSupport.h"
#include "CraftPadHub.h"
#include "MarkdownCommands.h"
#include "ScintillaNext.h"
#include "VaultIndex.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>

namespace ImageSupport
{

static const QStringList kImageExt = {
    QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"),
    QStringLiteral("svg"), QStringLiteral("webp"), QStringLiteral("bmp")};

static bool isImagePath(const QString &path)
{
    return kImageExt.contains(QFileInfo(QUrl(path).path()).suffix().toLower());
}

bool canHandle(const QMimeData *mime)
{
    if (!mime) return false;
    if (mime->hasUrls()) {
        for (const QUrl &u : mime->urls()) {
            if (u.isLocalFile() ? isImagePath(u.toLocalFile()) : (u.scheme().startsWith(QLatin1String("http")) && (isImagePath(u.toString()) || mime->hasImage())))
                return true;
        }
    }
    return mime->hasImage();
}

static QString noteDirFor(ScintillaNext *editor)
{
    if (editor->isFile())
        return editor->getFileInfo().absolutePath();
    VaultIndex *index = CraftPadHub::instance()->index();
    return index->hasRoot() ? index->root() : QString();
}

static QString uniquePath(const QString &dir, const QString &fileName)
{
    QDir().mkpath(dir);
    const QFileInfo fi(fileName);
    QString base = fi.completeBaseName().isEmpty() ? QStringLiteral("imagem") : fi.completeBaseName();
    const QString ext = fi.suffix().isEmpty() ? QStringLiteral("png") : fi.suffix().toLower();
    QString path = QDir(dir).filePath(base + QLatin1Char('.') + ext);
    for (int i = 2; QFileInfo::exists(path); ++i)
        path = QDir(dir).filePath(QStringLiteral("%1-%2.%3").arg(base).arg(i).arg(ext));
    return path;
}

static QString timestampName(const QString &ext)
{
    return QStringLiteral("imagem-%1.%2").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")), ext);
}

static QString markupFor(const QString &relative, const QString &absolute)
{
    QImageReader reader(absolute);
    const QSize size = reader.size();
    // Imagens grandes entram com 480 px de largura; pode ser ajustado nas propriedades
    if (size.isValid() && size.width() > 480)
        return QStringLiteral("![[%1|480]]").arg(relative);
    return QStringLiteral("![[%1]]").arg(relative);
}

static QByteArray download(const QUrl &url, QString *ext)
{
    QNetworkAccessManager nam;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = nam.get(request);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(20000);
    loop.exec();
    QByteArray data;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        data = reply->readAll();
        const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (type.contains(QLatin1String("jpeg"))) *ext = QStringLiteral("jpg");
        else if (type.contains(QLatin1String("gif"))) *ext = QStringLiteral("gif");
        else if (type.contains(QLatin1String("webp"))) *ext = QStringLiteral("webp");
        else if (type.contains(QLatin1String("svg"))) *ext = QStringLiteral("svg");
        else if (!isImagePath(url.toString())) *ext = QStringLiteral("png");
        else *ext = QFileInfo(url.path()).suffix().toLower();
    }
    reply->abort();
    reply->deleteLater();
    return data;
}

bool insertFromMime(ScintillaNext *editor, const QMimeData *mime, int position)
{
    const QString noteDir = noteDirFor(editor);
    if (noteDir.isEmpty()) {
        QMessageBox::information(editor, QObject::tr("Inserir imagem"), QObject::tr("Salve a nota primeiro: a imagem é guardada na pasta \"anexos\" ao lado dela."));
        return true;
    }
    const QString attachDir = QDir(noteDir).filePath(QStringLiteral("anexos"));
    VaultIndex *index = CraftPadHub::instance()->index();
    QStringList markups;

    auto addFile = [&](const QString &absolute) {
        markups << markupFor(QDir(noteDir).relativeFilePath(absolute), absolute);
    };

    bool handledUrl = false;
    if (mime->hasUrls()) {
        for (const QUrl &u : mime->urls()) {
            if (u.isLocalFile() && isImagePath(u.toLocalFile())) {
                const QString src = QFileInfo(u.toLocalFile()).absoluteFilePath();
                const bool inside = src.startsWith(noteDir + QLatin1Char('/')) || (index->hasRoot() && src.startsWith(index->root() + QLatin1Char('/')));
                if (inside) {
                    addFile(src);
                }
                else {
                    const QString dest = uniquePath(attachDir, QFileInfo(src).fileName());
                    if (QFile::copy(src, dest)) addFile(dest);
                }
                handledUrl = true;
            }
            else if (u.scheme().startsWith(QLatin1String("http")) && !mime->hasImage()) {
                QString ext;
                const QByteArray data = download(u, &ext);
                if (!data.isEmpty()) {
                    QString name = QFileInfo(u.path()).fileName();
                    if (!isImagePath(name)) name = timestampName(ext);
                    const QString dest = uniquePath(attachDir, name);
                    QFile f(dest);
                    if (f.open(QIODevice::WriteOnly)) { f.write(data); f.close(); addFile(dest); }
                }
                handledUrl = true;
            }
        }
    }
    if (!handledUrl && mime->hasImage()) {
        // Imagem arrastada do navegador (ou colada)
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull()) {
            QString name = timestampName(QStringLiteral("png"));
            if (mime->hasUrls() && !mime->urls().isEmpty()) {
                const QString fromUrl = QFileInfo(mime->urls().first().path()).completeBaseName();
                if (!fromUrl.isEmpty()) name = fromUrl + QStringLiteral(".png");
            }
            const QString dest = uniquePath(attachDir, name);
            if (image.save(dest)) addFile(dest);
        }
    }

    if (markups.isEmpty())
        return false;

    MarkdownCommands::insertTextAt(editor, position, markups.join(QLatin1Char('\n')));
    editor->setEmptySelection(position);
    return true;
}

void insertFromFile(ScintillaNext *editor, QWidget *parent)
{
    const QStringList files = QFileDialog::getOpenFileNames(parent, QObject::tr("Inserir imagem"), QString(),
                                                            QObject::tr("Imagens (*.png *.jpg *.jpeg *.gif *.svg *.webp *.bmp)"));
    if (files.isEmpty())
        return;
    QMimeData mime;
    QList<QUrl> urls;
    for (const QString &f : files) urls << QUrl::fromLocalFile(f);
    mime.setUrls(urls);
    insertFromMime(editor, &mime, editor->currentPos());
}

// ---------------------------------------------------------------------------
// Propriedades: tamanho e posição
// ---------------------------------------------------------------------------

namespace {

enum Position { Inline, Centered, WrapLeft, WrapRight };

struct ImageMarkup {
    int start = -1;      // em caracteres, na linha
    int length = 0;
    QString src;
    int width = 0;
    Position position = Inline;
};

QList<ImageMarkup> findImages(const QString &line)
{
    static const QRegularExpression centered(QStringLiteral(R"(<p\s+align="center"\s*>\s*(<img\s[^>]*>)\s*</p>)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression img(QStringLiteral(R"(<img\s[^>]*>)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression embed(QStringLiteral(R"(!\[\[([^\]\|\n]+)(?:\|([^\]\n]*))?\]\])"));
    static const QRegularExpression md(QStringLiteral(R"(!\[([^\]\n]*)\]\(([^)\n]+)\))"));
    static const QRegularExpression srcRe(QStringLiteral(R"(src\s*=\s*"([^"]*)\")"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression widthRe(QStringLiteral(R"(width\s*=\s*"?(\d+))"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression alignRe(QStringLiteral(R"(align\s*=\s*"?(left|right))"), QRegularExpression::CaseInsensitiveOption);

    QList<ImageMarkup> out;
    QVector<bool> used(line.size() + 1, false);
    auto overlaps = [&](int s, int l) { for (int i = s; i < s + l; ++i) if (used[i]) return true; return false; };
    auto mark = [&](int s, int l) { for (int i = s; i < s + l; ++i) used[i] = true; };

    auto parseTag = [&](const QString &tag, ImageMarkup *m) {
        m->src = srcRe.match(tag).captured(1);
        m->width = widthRe.match(tag).captured(1).toInt();
        const QString a = alignRe.match(tag).captured(1).toLower();
        if (a == QLatin1String("left")) m->position = WrapLeft;
        else if (a == QLatin1String("right")) m->position = WrapRight;
    };

    for (auto it = centered.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        ImageMarkup x;
        x.start = m.capturedStart();
        x.length = m.capturedLength();
        parseTag(m.captured(1), &x);
        x.position = Centered;
        out << x;
        mark(x.start, x.length);
    }
    for (auto it = img.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        if (overlaps(m.capturedStart(), m.capturedLength())) continue;
        ImageMarkup x;
        x.start = m.capturedStart();
        x.length = m.capturedLength();
        parseTag(m.captured(0), &x);
        out << x;
        mark(x.start, x.length);
    }
    for (auto it = embed.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        if (overlaps(m.capturedStart(), m.capturedLength())) continue;
        ImageMarkup x;
        x.start = m.capturedStart();
        x.length = m.capturedLength();
        x.src = m.captured(1).trimmed();
        x.width = m.captured(2).section(QLatin1Char('x'), 0, 0).toInt();
        if (!isImagePath(x.src)) continue;
        out << x;
        mark(x.start, x.length);
    }
    for (auto it = md.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        if (overlaps(m.capturedStart(), m.capturedLength())) continue;
        ImageMarkup x;
        x.start = m.capturedStart();
        x.length = m.capturedLength();
        x.src = m.captured(2).trimmed().section(QLatin1Char(' '), 0, 0);
        out << x;
    }
    std::sort(out.begin(), out.end(), [](const ImageMarkup &a, const ImageMarkup &b) { return a.start < b.start; });
    return out;
}

QString buildMarkup(const QString &src, int width, Position pos)
{
    const bool remote = src.startsWith(QLatin1String("http"));
    const QString w = width > 0 ? QStringLiteral(" width=\"%1\"").arg(width) : QString();
    switch (pos) {
    case Centered: return QStringLiteral("<p align=\"center\"><img src=\"%1\"%2></p>").arg(src, w);
    case WrapLeft: return QStringLiteral("<img src=\"%1\"%2 align=\"left\">").arg(src, w);
    case WrapRight: return QStringLiteral("<img src=\"%1\"%2 align=\"right\">").arg(src, w);
    case Inline:
        if (remote) return width > 0 ? QStringLiteral("<img src=\"%1\"%2>").arg(src, w) : QStringLiteral("![](%1)").arg(src);
        return width > 0 ? QStringLiteral("![[%1|%2]]").arg(src).arg(width) : QStringLiteral("![[%1]]").arg(src);
    }
    return QString();
}

} // namespace

static void editMarkup(ScintillaNext *editor, QWidget *parent, int line, const ImageMarkup &image)
{
    const QString noteDir = noteDirFor(editor);
    const QString path = resolveAttachment(image.src, noteDir);
    QImageReader reader(path);
    const QSize natural = reader.size();

    QDialog d(parent);
    d.setWindowTitle(QObject::tr("Propriedades da imagem"));
    auto *form = new QFormLayout(&d);
    auto *info = new QLabel(natural.isValid() ? QObject::tr("%1 — %2 × %3 px").arg(QFileInfo(image.src).fileName()).arg(natural.width()).arg(natural.height())
                                              : QFileInfo(image.src).fileName(), &d);
    auto *width = new QSpinBox(&d);
    width->setRange(0, 4000);
    width->setSpecialValueText(QObject::tr("Original"));
    width->setSuffix(QStringLiteral(" px"));
    width->setSingleStep(20);
    width->setValue(image.width);
    auto *pos = new QComboBox(&d);
    pos->addItem(QObject::tr("Na linha do texto"), Inline);
    pos->addItem(QObject::tr("Centralizada, em linha própria"), Centered);
    pos->addItem(QObject::tr("Texto ao redor — imagem à esquerda"), WrapLeft);
    pos->addItem(QObject::tr("Texto ao redor — imagem à direita"), WrapRight);
    pos->setCurrentIndex(pos->findData(image.position));
    form->addRow(info);
    form->addRow(QObject::tr("Largura na nota:"), width);
    form->addRow(QObject::tr("Posição:"), pos);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return;

    const Position newPos = Position(pos->currentData().toInt());
    // Em HTML (<img>) o caminho precisa ser relativo à nota
    QString src = image.src;
    if (newPos != Inline && !src.startsWith(QLatin1String("http")) && !path.isEmpty() && !noteDir.isEmpty())
        src = QDir(noteDir).relativeFilePath(path);
    const QString markup = buildMarkup(src, width->value(), newPos);
    QString text = MarkdownCommands::lineText(editor, line);
    const QString rest = (text.left(image.start) + text.mid(image.start + image.length)).trimmed();

    QString newText;
    if (newPos == Centered && !rest.isEmpty()) {
        // Imagem centralizada fica num parágrafo próprio, antes do texto
        newText = markup + QStringLiteral("\n\n") + text.left(image.start) + text.mid(image.start + image.length);
    }
    else if ((newPos == WrapLeft || newPos == WrapRight) && image.start > 0) {
        // Para o texto contornar, a imagem vai para o início do parágrafo
        newText = markup + text.left(image.start) + text.mid(image.start + image.length);
    }
    else {
        newText = text.left(image.start) + markup + text.mid(image.start + image.length);
    }
    editor->beginUndoAction();
    MarkdownCommands::replaceLines(editor, line, line, newText);
    editor->endUndoAction();
}

void editProperties(ScintillaNext *editor, QWidget *parent)
{
    const int line = editor->lineFromPosition(editor->currentPos());
    const QString text = MarkdownCommands::lineText(editor, line);
    const int col = QString::fromUtf8(editor->textRange(editor->positionFromLine(line), editor->currentPos())).size();
    const QList<ImageMarkup> images = findImages(text);
    if (images.isEmpty()) {
        QMessageBox::information(parent, QObject::tr("Imagem"), QObject::tr("Coloque o cursor na linha de uma imagem."));
        return;
    }
    ImageMarkup chosen = images.first();
    for (const ImageMarkup &m : images)
        if (col >= m.start && col <= m.start + m.length) chosen = m;
    editMarkup(editor, parent, line, chosen);
}

void editImageAt(ScintillaNext *editor, QWidget *parent, int line, const QString &markup)
{
    const QString text = MarkdownCommands::lineText(editor, line);
    for (const ImageMarkup &m : findImages(text)) {
        if (text.mid(m.start, m.length) == markup || markup.contains(text.mid(m.start, m.length))) {
            editMarkup(editor, parent, line, m);
            return;
        }
    }
    const QList<ImageMarkup> images = findImages(text);
    if (!images.isEmpty())
        editMarkup(editor, parent, line, images.first());
}

QString resolveAttachment(const QString &name, const QString &noteDir)
{
    if (name.isEmpty()) return QString();
    if (QFileInfo(name).isAbsolute() && QFileInfo::exists(name)) return name;
    if (!noteDir.isEmpty()) {
        const QString p = QDir(noteDir).filePath(name);
        if (QFileInfo::exists(p)) return QFileInfo(p).absoluteFilePath();
    }
    VaultIndex *index = CraftPadHub::instance()->index();
    if (!index->hasRoot()) return QString();
    const QString atRoot = QDir(index->root()).filePath(name);
    if (QFileInfo::exists(atRoot)) return atRoot;

    // Como o Obsidian: procura pelo nome do arquivo em todo o vault
    static QHash<QString, QString> byName;
    static QString builtFor;
    static QElapsedTimer age;
    const QString fileName = QFileInfo(name).fileName().toLower();
    if (builtFor != index->root() || !age.isValid() || (age.elapsed() > 15000 && !byName.contains(fileName))) {
        byName.clear();
        builtFor = index->root();
        age.start();
        QDirIterator it(index->root(), QDir::Files, QDirIterator::Subdirectories);
        int n = 0;
        while (it.hasNext() && n < 50000) {
            const QString p = it.next();
            ++n;
            if (isImagePath(p)) byName.insert(QFileInfo(p).fileName().toLower(), p);
        }
    }
    return byName.value(fileName);
}

} // namespace ImageSupport
