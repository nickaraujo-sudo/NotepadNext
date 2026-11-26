/*
 * craftpad++ - extensão do Notepad Next.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#ifndef NOTESUGGESTER_H
#define NOTESUGGESTER_H

#include <QHash>
#include <QSet>
#include <QObject>
#include <QStringList>
#include <QVector>

class VaultIndex;
class QTimer;

struct NoteSuggestion {
    QString path;
    QString title;
    double score = 0;
    bool mentioned = false;     // o título da nota foi falado na reunião
    QStringList sharedTerms;    // assuntos em comum
    QStringList sharedTags;
};

// Relaciona um trecho de conversa com as notas do vault:
// títulos citados, tags e assuntos em comum (TF-IDF simples, tudo local).
class NoteSuggester : public QObject
{
    Q_OBJECT

public:
    explicit NoteSuggester(VaultIndex *index, QObject *parent = nullptr);

    QList<NoteSuggestion> suggest(const QString &text, int max = 8);
    // Termos mais relevantes do trecho (para "Em discussão")
    QStringList keyTerms(const QString &text, int max = 8);
    // Títulos de notas citados no texto: título -> caminho
    QHash<QString, QString> mentionedTitles(const QString &text);
    // Lista de termos para o reconhecimento de fala (títulos e tags do vault)
    QStringList vocabularyTerms();

    // Não sugere a nota salva desta mesma reunião
    void setCurrentRecording(const QString &folder) { excludedRecording = folder; }

    static QString fold(const QString &s);             // minúsculas e sem acentos
    static QStringList tokenize(const QString &text);  // palavras relevantes, sem stopwords

private:
    struct NoteData {
        QString path;
        QString title;
        QString foldedTitle;
        QStringList tags;
        QHash<QString, int> tf;
        int tokenCount = 0;
        bool meeting = false;     // nota de reunião (tipo: reuniao)
        QString recording;        // pasta da gravação, nas notas de reunião
    };

    void ensureBuilt();
    void rebuild();

    VaultIndex *index;
    QTimer *rebuildTimer;
    bool dirty = true;
    QVector<NoteData> notes;
    QHash<QString, double> idf;
    QSet<QString> topicTerms;            // termos das notas que não são reuniões
    QString excludedRecording;
    QHash<QString, QString> displayForm; // termo dobrado -> como apareceu
};

#endif // NOTESUGGESTER_H
