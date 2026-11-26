/*
 * craftpad++ - extensão do Notepad Next.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#ifndef MEETINGASSISTANTDOCK_H
#define MEETINGASSISTANTDOCK_H

#include <QDockWidget>
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QVector>

class MainWindow;
class NoteSuggester;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProcess;
class QProgressBar;
class QPushButton;
class QTextBrowser;
class QToolButton;
class QUrl;

// Aba "Meeting Assistant": grava a reunião (sem bot), mostra a transcrição com quem
// falou e, ao lado, sugere ligações com as notas do vault.
// O áudio é processado pelo voicelab (tools/voicelab), que roda no próprio Mac.
class MeetingAssistantDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit MeetingAssistantDock(MainWindow *window);

private:
    struct Speaker {
        QString key;
        QString channel;
        QString label;
        QString name;
        double seconds = 0;
        QString displayName() const { return name.isEmpty() ? label : name; }
    };
    struct Turn {
        QString speakerKey;
        double start = 0;
        double end = 0;
        QString text;
        bool draft = false;
    };

    enum class State { Idle, Starting, Recording, Analyzing, Naming };

    void buildUi();
    void setState(State s);
    QString voicelabPath(bool ask);
    QString writeVocabulary();
    void runVoicelab(const QStringList &args, State state);
    void handleEvent(const QJsonObject &event);

    void startRecording();
    void stopRecording();
    void openMeeting();
    void openAudioFile();
    void reanalyze();
    void nameSpeakers();
    void saveAsNote();

    bool loadResult(const QString &folder);
    void renderTranscript();
    void updateSuggestions();
    void handleLink(const QUrl &url);
    QString colorFor(const QString &speakerKey) const;
    QString speakerName(const QString &key) const;
    QString contextText() const;

    MainWindow *window;
    NoteSuggester *suggester;

    QLineEdit *titleEdit;
    QPushButton *recordButton;
    QPushButton *stopButton;
    QCheckBox *systemAudioCheck;
    QToolButton *openButton;
    QPushButton *reanalyzeButton;
    QPushButton *nameButton;
    QPushButton *saveButton;
    QLabel *statusLabel;
    QProgressBar *micLevel;
    QProgressBar *sysLevel;
    QTextBrowser *transcriptView;
    QLabel *contextLabel;
    QLabel *topicsLabel;
    QListWidget *suggestionList;
    QLabel *participationLabel;
    QPushButton *wholeMeetingButton;

    QPointer<QProcess> process;
    QByteArray stdoutBuffer;
    State state = State::Idle;
    QString folder;
    QString engineTitle;
    QString loadedTitle;
    QVector<Speaker> speakers;
    QVector<Turn> turns;
    int selectedTurn = -1;
    QHash<QString, QString> mentionedNotes; // título -> caminho
};

#endif // MEETINGASSISTANTDOCK_H
