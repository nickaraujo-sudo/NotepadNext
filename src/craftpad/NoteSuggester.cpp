/*
 * craftpad++ - extensão do Notepad Next.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#include "NoteSuggester.h"
#include "VaultIndex.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <cmath>

static const QSet<QString> &stopwords()
{
    static const QSet<QString> words = [] {
        const char *list =
            "a o e as os um uma uns umas de do da dos das em no na nos nas por pelo pela pelos pelas para pra pro "
            "com sem sob sobre entre ate apos ante que se nao sim mas ou ja so tambem muito muita muitos muitas "
            "mais menos bem mal como quando onde porque pois entao assim isso isto aquilo esse essa esses essas "
            "este esta estes estas aquele aquela aqueles aquelas ele ela eles elas eu tu voce voces nos vos "
            "me te lhe lhes meu minha meus minhas seu sua seus suas nosso nossa nossos nossas dele dela deles delas "
            "ser estar ter haver ir fazer dar ver vir poder dizer falar ficar querer saber achar "
            "sou es somos sao era eram foi foram sera seria estou estamos estao estava estavam esteve "
            "tenho tem temos tinha tinham teve vai vou vamos iam fazendo feito faz fiz pode podem posso "
            "acho acha achei sei sabe gente cara tipo coisa coisas negocio ai la aqui ali agora hoje "
            "ne ta tah ok beleza pois entao tudo todo toda todos todas nada algo alguem alguma algum "
            "outro outra outros outras mesmo mesma cada qual quais quem qualquer tanto tanta tao "
            "vez vezes dia ano anos mes semana hora horas parte forma jeito caso exemplo verdade realmente "
            "sempre nunca ainda depois antes logo bom boa melhor certo certa claro exatamente "
            "the and for that this with you are have not but from they will what about there "
            "dessa desse disso nisso naquele numa num pela pelo pois porem porque quer queria "
            "certeza duvida duvidas sentido entendeu entendi perfeito legal show minimamente basicamente sinceramente";
        QSet<QString> s;
        for (const QString &w : QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            s.insert(w);
        return s;
    }();
    return words;
}

QString NoteSuggester::fold(const QString &s)
{
    const QString decomposed = s.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    for (QChar ch : decomposed) {
        if (ch.category() == QChar::Mark_NonSpacing)
            continue;
        out += ch.toLower();
    }
    return out;
}

QStringList NoteSuggester::tokenize(const QString &text)
{
    static const QRegularExpression splitter(QStringLiteral("[^\\p{L}\\p{N}]+"));
    QStringList out;
    for (const QString &w : fold(text).split(splitter, Qt::SkipEmptyParts)) {
        if (w.size() < 3 || stopwords().contains(w))
            continue;
        bool digits = true;
        for (QChar c : w) if (!c.isDigit()) { digits = false; break; }
        if (digits && w.size() < 4)
            continue;
        out << w;
    }
    return out;
}

NoteSuggester::NoteSuggester(VaultIndex *index, QObject *parent)
    : QObject(parent), index(index)
{
    rebuildTimer = new QTimer(this);
    rebuildTimer->setSingleShot(true);
    rebuildTimer->setInterval(3000);
    connect(rebuildTimer, &QTimer::timeout, this, [this]() { dirty = true; });
    connect(index, &VaultIndex::indexChanged, rebuildTimer, qOverload<>(&QTimer::start));
}

void NoteSuggester::ensureBuilt()
{
    if (dirty)
        rebuild();
}

void NoteSuggester::rebuild()
{
    dirty = false;
    notes.clear();
    idf.clear();
    topicTerms.clear();
    QHash<QString, int> df;

    for (auto it = index->notes().constBegin(); it != index->notes().constEnd(); ++it) {
        NoteData n;
        n.path = it.key();
        n.title = it->name;
        n.foldedTitle = fold(it->name).simplified();
        n.tags = it->tags;

        QFile f(n.path);
        if (f.open(QIODevice::ReadOnly)) {
            const QString text = QString::fromUtf8(f.read(256 * 1024));
            if (text.startsWith(QLatin1String("---"))) {
                const int end = text.indexOf(QLatin1String("\n---"), 3);
                const QString front = end > 0 ? text.left(end) : QString();
                static const QRegularExpression typeRe(QStringLiteral("(?m)^tipo:\\s*reuni[aã]o\\s*$"));
                static const QRegularExpression recRe(QStringLiteral("(?m)^gravacao:\\s*\"?([^\"\\n]+)\"?\\s*$"));
                n.meeting = typeRe.match(front).hasMatch();
                const auto m = recRe.match(front);
                if (m.hasMatch()) n.recording = m.captured(1).trimmed();
            }
            const QStringList tokens = tokenize(n.title + QLatin1Char(' ') + n.title + QLatin1Char(' ') + text);
            n.tokenCount = tokens.size();
            for (const QString &t : tokens)
                n.tf[t]++;
        }
        for (auto t = n.tf.constBegin(); t != n.tf.constEnd(); ++t) {
            df[t.key()]++;
            if (!n.meeting) topicTerms.insert(t.key());
        }
        notes << n;
    }

    const double total = std::max<qsizetype>(1, notes.size());
    for (auto it = df.constBegin(); it != df.constEnd(); ++it)
        idf.insert(it.key(), std::log((total + 1.0) / (it.value() + 0.5)));
}

QHash<QString, QString> NoteSuggester::mentionedTitles(const QString &text)
{
    ensureBuilt();
    QHash<QString, QString> found;
    const QString folded = QLatin1Char(' ') + fold(text).replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" ")) + QLatin1Char(' ');
    for (const NoteData &n : notes) {
        if (n.foldedTitle.size() < 3 || stopwords().contains(n.foldedTitle))
            continue;
        const QString needle = QLatin1Char(' ') + QString(n.foldedTitle).replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" ")) + QLatin1Char(' ');
        if (needle.trimmed().isEmpty())
            continue;
        if (folded.contains(needle))
            found.insert(n.title, n.path);
    }
    return found;
}

QStringList NoteSuggester::keyTerms(const QString &text, int max)
{
    ensureBuilt();
    static const QRegularExpression splitter(QStringLiteral("[^\\p{L}\\p{N}]+"));
    static const QRegularExpression sentenceEnd(QStringLiteral("[.!?…]\\s*$"));
    QHash<QString, int> tf;
    QSet<QString> properNoun; // apareceu com maiúscula no meio da frase
    // Guarda como a palavra apareceu (com acento e maiúscula) para mostrar
    int pos = 0;
    QString previous;
    const QStringList words = text.split(splitter, Qt::SkipEmptyParts);
    for (const QString &raw : words) {
        const int at = text.indexOf(raw, pos);
        const bool sentenceStart = at <= 0 || sentenceEnd.match(text.mid(qMax(0, at - 3), qMin(at, 3))).hasMatch();
        if (at >= 0) pos = at + raw.size();
        const QString f = fold(raw);
        if (f.size() < 3 || stopwords().contains(f))
            continue;
        tf[f]++;
        if (raw.at(0).isUpper() && !sentenceStart)
            properNoun.insert(f);
        if (!displayForm.contains(f) || raw.at(0).isUpper())
            displayForm.insert(f, raw);
    }
    QList<QPair<double, QString>> ranked;
    const double unknownIdf = std::log((notes.size() + 1.0) / 0.5);
    for (auto it = tf.constBegin(); it != tf.constEnd(); ++it) {
        if (it.value() < 2 && tf.size() > 30)
            continue; // em textos longos, ignora o que apareceu uma vez só
        double w = (1 + std::log(double(it.value())));
        if (topicTerms.contains(it.key()))
            w *= idf.value(it.key()) * 1.5;          // assunto que existe no vault
        else if (properNoun.contains(it.key()))
            w *= unknownIdf;                          // nome próprio (empresa, pessoa, produto)
        else
            w *= unknownIdf * 0.15;                   // palavra comum da conversa
        ranked << qMakePair(w, it.key());
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList out;
    for (const auto &r : ranked) {
        out << displayForm.value(r.second, r.second);
        if (out.size() >= max) break;
    }
    return out;
}

QList<NoteSuggestion> NoteSuggester::suggest(const QString &text, int max)
{
    ensureBuilt();
    QList<NoteSuggestion> out;
    if (notes.isEmpty() || text.trimmed().isEmpty())
        return out;

    QHash<QString, int> tf;
    for (const QString &t : tokenize(text))
        tf[t]++;
    keyTerms(text, 1); // atualiza as formas de exibição

    // Só os termos mais informativos do trecho
    QList<QPair<double, QString>> weighted;
    for (auto it = tf.constBegin(); it != tf.constEnd(); ++it) {
        if (!idf.contains(it.key())) continue;
        weighted << qMakePair((1 + std::log(double(it.value()))) * idf.value(it.key()), it.key());
    }
    std::sort(weighted.begin(), weighted.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    if (weighted.size() > 60) weighted = weighted.mid(0, 60);

    // Trechos curtos (uma fala) têm menos palavras em comum: exige menos
    const double minScore = tf.size() < 40 ? 0.8 : 1.5;
    const QHash<QString, QString> mentioned = mentionedTitles(text);
    const QSet<QString> tokenSet(tf.keyBegin(), tf.keyEnd());

    for (const NoteData &n : notes) {
        if (!excludedRecording.isEmpty() && QDir::cleanPath(n.recording) == QDir::cleanPath(excludedRecording))
            continue;
        NoteSuggestion s;
        s.path = n.path;
        s.title = n.title;

        if (mentioned.contains(n.title)) {
            s.mentioned = true;
            s.score += 8;
        }
        for (const QString &tag : n.tags) {
            const QString leaf = fold(tag.section(QLatin1Char('/'), -1));
            if (leaf.size() >= 3 && tokenSet.contains(leaf)) {
                s.score += 2.5;
                s.sharedTags << QLatin1Char('#') + tag;
            }
        }
        QList<QPair<double, QString>> shared;
        for (const auto &w : weighted) {
            const int c = n.tf.value(w.second);
            if (c == 0) continue;
            const double contribution = w.first * (1 + std::log(double(c))) / std::sqrt(1.0 + n.tokenCount / 400.0);
            s.score += contribution * 0.35;
            shared << qMakePair(contribution, w.second);
        }
        std::sort(shared.begin(), shared.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        for (int i = 0; i < shared.size() && s.sharedTerms.size() < 4; ++i)
            s.sharedTerms << displayForm.value(shared[i].second, shared[i].second);

        if (s.score >= minScore)
            out << s;
    }
    std::sort(out.begin(), out.end(), [](const NoteSuggestion &a, const NoteSuggestion &b) { return a.score > b.score; });
    if (out.size() > max) out = out.mid(0, max);
    return out;
}

QStringList NoteSuggester::vocabularyTerms()
{
    QStringList out;
    QSet<QString> seen;
    auto add = [&](const QString &t) {
        const QString s = t.simplified();
        const QString key = fold(s);
        if (s.size() < 3 || s.size() > 40 || seen.contains(key)) return;
        if (s.split(QLatin1Char(' ')).size() > 3) return;
        seen.insert(key);
        out << s;
    };
    for (auto it = index->notes().constBegin(); it != index->notes().constEnd(); ++it) {
        // Títulos curtos costumam ser nomes (clientes, pessoas, produtos, siglas)
        add(it->name);
        for (const QString &tag : it->tags)
            add(tag.section(QLatin1Char('/'), -1).replace(QLatin1Char('-'), QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' ')));
    }
    return out;
}
