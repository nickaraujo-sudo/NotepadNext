/*
 * NotepadNext - recursos próprios deste fork.
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#include "MeetingAssistantPage.h"

#include "NotepadNextHub.h"
#include "MainWindow.h"
#include "NoteSuggester.h"
#include "VaultIndex.h"

#include <QAction>
#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpacerItem>
#include <QSettings>
#include <QSplitter>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

static const char *kPalette[] = {"#9B6BE0", "#2E86DE", "#10AC84", "#E15554", "#E8A33D", "#5F27CD", "#01A3A4", "#C8507A"};

static QString clock(double seconds)
{
    const int s = qMax(0, int(seconds));
    return QStringLiteral("%1:%2:%3").arg(s / 3600, 2, 10, QLatin1Char('0'))
        .arg((s % 3600) / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

// Mistura duas cores (amount = peso da primeira); funciona nos temas claros e escuros
static QColor mix(const QColor &a, const QColor &b, double amount)
{
    return QColor::fromRgbF(a.redF() * amount + b.redF() * (1 - amount),
                            a.greenF() * amount + b.greenF() * (1 - amount),
                            a.blueF() * amount + b.blueF() * (1 - amount));
}

static QString meetingsRoot()
{
    return QDir::home().filePath(QStringLiteral("NotepadNext-reunioes"));
}

MeetingAssistantPage::MeetingAssistantPage(MainWindow *window)
    : QWidget(window), window(window)
{
    setObjectName(QStringLiteral("MeetingAssistantPage"));
    suggester = new NoteSuggester(NotepadNextHub::instance()->index(), this);
    buildUi();
    setState(State::Idle);
    renderTranscript();

    // Reabre a última reunião vista
    const QString last = QSettings().value(QStringLiteral("NotepadNext/meeting/lastFolder")).toString();
    if (!last.isEmpty() && loadResult(last))
        setState(State::Idle);

    // Quando o índice do vault muda, as conexões são recalculadas
    connect(NotepadNextHub::instance()->index(), &VaultIndex::indexChanged, this, [this]() {
        QTimer::singleShot(3500, this, [this]() {
            if (!turns.isEmpty()) {
                QStringList all;
                for (const Turn &t : turns) all << t.text;
                mentionedNotes = suggester->mentionedTitles(all.join(QLatin1Char(' ')));
                renderTranscript();
                updateSuggestions();
            }
        });
    });
}

void MeetingAssistantPage::buildUi()
{
    QWidget *root = this;
    QVBoxLayout *layout = new QVBoxLayout(root);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);

    // Linha 1: título da reunião e fonte do áudio (as ações ficam na barra e no menu "Reunião")
    QHBoxLayout *header = new QHBoxLayout;
    titleEdit = new QLineEdit(root);
    titleEdit->setPlaceholderText(tr("Meeting title (e.g. 26-11 Client: agents proposal)"));
    systemAudioCheck = new QCheckBox(tr("Computer audio (online meeting)"), root);
    systemAudioCheck->setChecked(QSettings().value(QStringLiteral("NotepadNext/meeting/systemAudio"), true).toBool());
    systemAudioCheck->setToolTip(tr("Uncheck for in-person meetings: only the microphone is recorded."));
    header->addWidget(titleEdit, 1);
    header->addSpacing(8);
    header->addWidget(systemAudioCheck);
    layout->addLayout(header);

    auto makeAction = [this](const QString &text, const QString &tip, void (MeetingAssistantPage::*slot)()) {
        QAction *a = new QAction(text, this);
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    recordAction = makeAction(tr("Record"), tr("Records the microphone and, if checked, the computer audio. No bot joins the meeting."), &MeetingAssistantPage::startRecording);
    stopAction = makeAction(tr("Stop"), tr("Stops recording and separates who said what"), &MeetingAssistantPage::stopRecording);
    openMeetingAction = makeAction(tr("Open meeting…"), tr("Opens a meeting that was already recorded"), &MeetingAssistantPage::openMeeting);
    openAudioAction = makeAction(tr("Open audio…"), tr("Analyzes a recording you already have (Plaud, m4a, wav, mp3)"), &MeetingAssistantPage::openAudioFile);
    reanalyzeAction = makeAction(tr("Reanalyze"), tr("Redoes the transcription with the current vault vocabulary and the known voices"), &MeetingAssistantPage::reanalyze);
    nameAction = makeAction(tr("Name voices…"), tr("Names each person; the voice is recognized automatically in future meetings"), &MeetingAssistantPage::nameSpeakers);
    saveAction = makeAction(tr("Save as note"), tr("Creates a note in the vault with the transcript and links to related notes"), &MeetingAssistantPage::saveAsNote);

    // Linha 2: status e níveis
    QHBoxLayout *status = new QHBoxLayout;
    statusLabel = new QLabel(root);
    statusLabel->setTextFormat(Qt::RichText);
    auto level = [root](const QString &label) {
        QProgressBar *bar = new QProgressBar(root);
        bar->setRange(0, 100);
        bar->setTextVisible(true);
        bar->setFormat(label);
        bar->setFixedWidth(90);
        bar->setFixedHeight(12);
        return bar;
    };
    micLevel = level(tr("mic"));
    sysLevel = level(tr("system"));
    status->addWidget(statusLabel, 1);
    status->addWidget(micLevel);
    status->addWidget(sysLevel);
    layout->addLayout(status);

    // Corpo: transcrição | conexões
    QSplitter *split = new QSplitter(Qt::Horizontal, root);
    transcriptView = new QTextBrowser(split);
    transcriptView->setOpenLinks(false);
    transcriptView->document()->setDocumentMargin(14);
    connect(transcriptView, &QTextBrowser::anchorClicked, this, &MeetingAssistantPage::handleLink);

    QWidget *side = new QWidget(split);
    QVBoxLayout *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(10, 0, 0, 0);
    contextLabel = new QLabel(side);
    contextLabel->setTextFormat(Qt::RichText);
    wholeMeetingButton = new QPushButton(tr("See the whole meeting"), side);
    wholeMeetingButton->setFlat(true);
    wholeMeetingButton->setVisible(false);
    connect(wholeMeetingButton, &QPushButton::clicked, this, [this]() {
        selectedTurn = -1;
        renderTranscript();
        updateSuggestions();
    });
    QLabel *topicsTitle = new QLabel(tr("<b>Being discussed</b>"), side);
    topicsLabel = new QLabel(side);
    topicsLabel->setWordWrap(true);
    topicsLabel->setTextFormat(Qt::RichText);
    QLabel *notesTitle = new QLabel(tr("<b>Related notes</b>"), side);
    suggestionList = new QListWidget(side);
    suggestionList->setFrameShape(QFrame::NoFrame);
    suggestionList->setSelectionMode(QAbstractItemView::SingleSelection);
    suggestionList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(suggestionList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) emit NotepadNextHub::instance()->openFileRequested(path, -1);
    });
    QLabel *peopleTitle = new QLabel(tr("<b>Participation</b>"), side);
    participationLabel = new QLabel(side);
    participationLabel->setTextFormat(Qt::RichText);
    participationLabel->setWordWrap(true);

    sideLayout->addWidget(contextLabel);
    sideLayout->addWidget(wholeMeetingButton, 0, Qt::AlignLeft);
    sideLayout->addSpacing(4);
    sideLayout->addWidget(topicsTitle);
    sideLayout->addWidget(topicsLabel);
    sideLayout->addSpacing(6);
    sideLayout->addWidget(notesTitle);
    sideLayout->addWidget(suggestionList, 1);
    sideLayout->addWidget(peopleTitle);
    sideLayout->addWidget(participationLabel);

    split->addWidget(transcriptView);
    split->addWidget(side);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    split->setSizes({640, 280});
    layout->addWidget(split, 1);

    connect(systemAudioCheck, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(QStringLiteral("NotepadNext/meeting/systemAudio"), on);
    });
}

void MeetingAssistantPage::setState(State s)
{
    state = s;
    const bool idle = s == State::Idle;
    const bool hasResult = !folder.isEmpty() && QFile::exists(QDir(folder).filePath(QStringLiteral("resultado.json")));
    recordAction->setEnabled(idle);
    stopAction->setEnabled(s == State::Recording);
    systemAudioCheck->setEnabled(idle);
    openMeetingAction->setEnabled(idle);
    openAudioAction->setEnabled(idle);
    reanalyzeAction->setEnabled(idle && !folder.isEmpty());
    nameAction->setEnabled(idle && hasResult);
    saveAction->setEnabled(idle && !turns.isEmpty());
    micLevel->setVisible(s == State::Recording);
    sysLevel->setVisible(s == State::Recording && systemAudioCheck->isChecked());

    switch (s) {
    case State::Idle:
        statusLabel->setText(folder.isEmpty()
            ? tr("Ready to record. Everything is processed on this Mac.")
            : tr("Meeting: <span style='color:gray'>%1</span>").arg(folder.toHtmlEscaped()));
        break;
    case State::Starting:
        statusLabel->setText(tr("Preparing the recording…"));
        break;
    case State::Recording:
        statusLabel->setText(tr("<span style='color:#E15554'>●</span> Recording"));
        break;
    case State::Analyzing:
        statusLabel->setText(tr("Analyzing the meeting…"));
        break;
    case State::Naming:
        statusLabel->setText(tr("Applying the names…"));
        break;
    }
}

QString MeetingAssistantPage::voicelabPath(bool ask)
{
    QSettings settings;
    QString path = settings.value(QStringLiteral("NotepadNext/voicelabPath")).toString();
    if (!path.isEmpty() && QFileInfo(path).isExecutable())
        return path;

    const QStringList candidates = {
        QDir::home().filePath(QStringLiteral("NotepadNext/tools/voicelab/.build/release/voicelab")),
        QStringLiteral("/usr/local/bin/voicelab"),
        QStringLiteral("/opt/homebrew/bin/voicelab"),
    };
    for (const QString &c : candidates) {
        if (QFileInfo(c).isExecutable()) {
            settings.setValue(QStringLiteral("NotepadNext/voicelabPath"), c);
            return c;
        }
    }
    if (!ask)
        return QString();

    QMessageBox::information(this, tr("Meeting Assistant"),
        tr("Could not find voicelab, which does the recording and transcription.\n\nBuild it with:\n  cd ~/NotepadNext/tools/voicelab\n  swift build -c release\n\nor show where it is in the next window."));
    path = QFileDialog::getOpenFileName(this, tr("Where is voicelab?"), QDir::homePath());
    if (!path.isEmpty() && QFileInfo(path).isExecutable()) {
        settings.setValue(QStringLiteral("NotepadNext/voicelabPath"), path);
        return path;
    }
    return QString();
}

QString MeetingAssistantPage::writeVocabulary()
{
    const QStringList terms = suggester->vocabularyTerms();
    if (terms.isEmpty())
        return QString();
    const QString dir = QDir::home().filePath(QStringLiteral("Library/Application Support/NotepadNext"));
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("vocabulario.txt"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return QString();
    f.write("# Gerado pelo NotepadNext a partir dos títulos e tags do vault\n");
    f.write(terms.join(QLatin1Char('\n')).toUtf8());
    return path;
}

void MeetingAssistantPage::runVoicelab(const QStringList &args, State newState)
{
    if (process)
        return;
    const QString program = voicelabPath(true);
    if (program.isEmpty())
        return;

    process = new QProcess(this);
    process->setProgram(program);
    process->setArguments(args + QStringList{QStringLiteral("--json")});
    stdoutBuffer.clear();
    auto stderrTail = std::make_shared<QStringList>();
    auto reportedError = std::make_shared<bool>(false);

    connect(process, &QProcess::readyReadStandardOutput, this, [this, reportedError]() {
        stdoutBuffer += process->readAllStandardOutput();
        int nl;
        while ((nl = stdoutBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = stdoutBuffer.left(nl).trimmed();
            stdoutBuffer.remove(0, nl + 1);
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isObject()) continue;
            if (doc.object().value(QStringLiteral("evento")).toString() == QLatin1String("erro"))
                *reportedError = true;
            handleEvent(doc.object());
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, stderrTail]() {
        const QString text = QString::fromUtf8(process->readAllStandardError());
        for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            if (line.startsWith(QLatin1String("[Profiling]"))) continue;
            *stderrTail << line.trimmed();
            if (stderrTail->size() > 25) stderrTail->removeFirst();
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            QMessageBox::warning(this, tr("Meeting Assistant"), tr("Could not start voicelab (%1).").arg(process->program()));
            process->deleteLater();
            process = nullptr;
            setState(State::Idle);
        }
    });
    connect(process, &QProcess::finished, this, [this, stderrTail, reportedError](int exitCode, QProcess::ExitStatus) {
        const State finishedState = state;
        process->deleteLater();
        process = nullptr;
        if (exitCode != 0 && !*reportedError) {
            QMessageBox::warning(this, tr("Meeting Assistant"),
                                 tr("voicelab finished with an error:\n\n%1").arg(stderrTail->join(QLatin1Char('\n'))));
        }
        if (finishedState != State::Idle && !folder.isEmpty())
            loadResult(folder);
        setState(State::Idle);
    });

    setState(newState);
    process->start();
}

void MeetingAssistantPage::handleEvent(const QJsonObject &e)
{
    const QString type = e.value(QStringLiteral("evento")).toString();
    if (type == QLatin1String("gravando")) {
        setState(State::Recording);
    }
    else if (type == QLatin1String("nivel")) {
        statusLabel->setText(tr("<span style='color:#E15554'>●</span> Recording %1 — the transcript appears every ~10 s")
                             .arg(clock(e.value(QStringLiteral("tempo")).toDouble())));
        micLevel->setValue(qMin(100, int(e.value(QStringLiteral("microfone")).toDouble() * 250)));
        sysLevel->setValue(qMin(100, int(e.value(QStringLiteral("sistema")).toDouble() * 250)));
    }
    else if (type == QLatin1String("trecho")) {
        Turn t;
        t.speakerKey = QStringLiteral("ao-vivo:") + e.value(QStringLiteral("canal")).toString();
        t.start = e.value(QStringLiteral("inicio")).toDouble();
        t.end = e.value(QStringLiteral("fim")).toDouble();
        t.text = e.value(QStringLiteral("texto")).toString();
        t.draft = true;
        turns << t;
        std::sort(turns.begin(), turns.end(), [](const Turn &a, const Turn &b) { return a.start < b.start; });
        mentionedNotes.insert(suggester->mentionedTitles(t.text));
        renderTranscript();
        updateSuggestions();
    }
    else if (type == QLatin1String("parado")) {
        setState(State::Analyzing);
        statusLabel->setText(tr("Recording finished. Separating who said what…"));
    }
    else if (type == QLatin1String("etapa")) {
        statusLabel->setText(e.value(QStringLiteral("texto")).toString().toHtmlEscaped());
    }
    else if (type == QLatin1String("pronto")) {
        folder = e.value(QStringLiteral("pasta")).toString();
    }
    else if (type == QLatin1String("erro")) {
        QMessageBox::warning(this, tr("Meeting Assistant"), e.value(QStringLiteral("texto")).toString());
    }
}

void MeetingAssistantPage::startRecording()
{
    QDir().mkpath(meetingsRoot());
    QString base = QDir(meetingsRoot()).filePath(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH'h'mm")));
    folder = base;
    for (int i = 2; QFileInfo::exists(folder); ++i)
        folder = base + QStringLiteral("-%1").arg(i);

    // Título da reunião anterior não passa para a nova
    if (!loadedTitle.isEmpty() && titleEdit->text() == loadedTitle)
        titleEdit->clear();
    loadedTitle.clear();
    turns.clear();
    speakers.clear();
    selectedTurn = -1;
    mentionedNotes.clear();
    engineTitle.clear();
    renderTranscript();
    updateSuggestions();

    QStringList args{QStringLiteral("gravar"), folder};
    if (!systemAudioCheck->isChecked()) args << QStringLiteral("--sem-sistema");
    const QString vocab = writeVocabulary();
    if (!vocab.isEmpty()) args << QStringLiteral("--vocabulario") << vocab;
    if (!titleEdit->text().trimmed().isEmpty()) args << QStringLiteral("--titulo") << titleEdit->text().trimmed();
    runVoicelab(args, State::Starting);
}

void MeetingAssistantPage::stopRecording()
{
    if (process && state == State::Recording) {
        process->write("\n");
        setState(State::Analyzing);
        statusLabel->setText(tr("Stopping the recording…"));
    }
}

void MeetingAssistantPage::openMeeting()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open meeting"), meetingsRoot());
    if (dir.isEmpty()) return;
    folder = dir;
    if (QFile::exists(QDir(dir).filePath(QStringLiteral("resultado.json")))) {
        loadResult(dir);
        setState(State::Idle);
    }
    else if (QMessageBox::question(this, tr("Meeting Assistant"), tr("This folder has not been analyzed yet. Analyze it now?")) == QMessageBox::Yes) {
        reanalyze();
    }
}

void MeetingAssistantPage::openAudioFile()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Analyze recording"), QDir::homePath(),
                                                      tr("Audio (*.m4a *.mp3 *.wav *.caf *.aac *.flac *.ogg *.opus *.mp4)"));
    if (file.isEmpty()) return;
    const QFileInfo fi(file);
    folder = fi.dir().filePath(fi.completeBaseName() + QStringLiteral("-voicelab"));
    turns.clear();
    speakers.clear();
    renderTranscript();
    QStringList args{QStringLiteral("analisar"), file};
    const QString vocab = writeVocabulary();
    if (!vocab.isEmpty()) args << QStringLiteral("--vocabulario") << vocab;
    if (!titleEdit->text().trimmed().isEmpty()) args << QStringLiteral("--titulo") << titleEdit->text().trimmed();
    runVoicelab(args, State::Analyzing);
}

void MeetingAssistantPage::reanalyze()
{
    if (folder.isEmpty()) return;
    QStringList args{QStringLiteral("analisar"), folder};
    const QString vocab = writeVocabulary();
    if (!vocab.isEmpty()) args << QStringLiteral("--vocabulario") << vocab;
    if (!titleEdit->text().trimmed().isEmpty()) args << QStringLiteral("--titulo") << titleEdit->text().trimmed();
    runVoicelab(args, State::Analyzing);
}

void MeetingAssistantPage::nameSpeakers()
{
    if (speakers.isEmpty()) return;

    QDialog d(this);
    d.setWindowTitle(tr("Name voices"));
    d.resize(560, 100);
    QVBoxLayout *outer = new QVBoxLayout(&d);
    QLabel *intro = new QLabel(tr("Enter each person's name. The voice is stored (only on this Mac) and recognized automatically in future meetings. Leave blank to not store it."), &d);
    intro->setWordWrap(true);
    outer->addWidget(intro);
    QFormLayout *form = new QFormLayout;
    outer->addLayout(form);

    QHash<QString, QLineEdit *> edits;
    for (const Speaker &s : speakers) {
        if (s.seconds < 1) continue;
        // Trecho mais longo dessa pessoa, para reconhecer quem é
        QString sample;
        for (const Turn &t : turns)
            if (t.speakerKey == s.key && t.text.size() > sample.size()) sample = t.text;
        if (sample.size() > 150) sample = sample.left(150) + QStringLiteral("…");
        QLabel *who = new QLabel(QStringLiteral("<span style='color:%1'>●</span> <b>%2</b> <span style='color:gray'>(%3, %4 s)</span><br>"
                                                "<i style='color:gray'>“%5”</i>")
                                 .arg(colorFor(s.key), s.label.toHtmlEscaped(), channelLabel(s.channel))
                                 .arg(int(s.seconds)).arg(sample.toHtmlEscaped()), &d);
        who->setWordWrap(true);
        who->setMinimumHeight(who->heightForWidth(520));
        QLineEdit *edit = new QLineEdit(s.name, &d);
        edit->setPlaceholderText(tr("Name of %1").arg(s.label));
        form->addRow(who);
        form->addRow(edit);
        form->addItem(new QSpacerItem(0, 8));
        edits.insert(s.key, edit);
    }
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return;

    QStringList args{QStringLiteral("nomear"), folder};
    for (const Speaker &s : speakers) {
        QLineEdit *edit = edits.value(s.key);
        if (!edit) continue;
        const QString name = edit->text().simplified();
        if (name.isEmpty() && s.name.isEmpty()) continue;
        args << QStringLiteral("--definir") << QStringLiteral("%1=%2").arg(s.key, name.isEmpty() ? QStringLiteral("-") : name);
    }
    if (args.size() > 2)
        runVoicelab(args, State::Naming);
}

bool MeetingAssistantPage::loadResult(const QString &dir)
{
    QFile f(QDir(dir).filePath(QStringLiteral("resultado.json")));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.isEmpty()) return false;
    folder = dir;
    QSettings().setValue(QStringLiteral("NotepadNext/meeting/lastFolder"), dir);

    speakers.clear();
    for (const QJsonValue &v : root.value(QStringLiteral("speakers")).toArray()) {
        const QJsonObject o = v.toObject();
        Speaker s;
        s.key = o.value(QStringLiteral("key")).toString();
        s.channel = o.value(QStringLiteral("channel")).toString();
        s.label = o.value(QStringLiteral("label")).toString();
        s.name = o.value(QStringLiteral("name")).toString();
        s.seconds = o.value(QStringLiteral("seconds")).toDouble();
        speakers << s;
    }

    // Prefere o Parakeet (foi o melhor no teste em português)
    const QJsonArray engines = root.value(QStringLiteral("engines")).toArray();
    QJsonObject engine;
    for (const QJsonValue &v : engines)
        if (v.toObject().value(QStringLiteral("engine")).toString() == QLatin1String("parakeet")) engine = v.toObject();
    if (engine.isEmpty() && !engines.isEmpty()) engine = engines.first().toObject();
    engineTitle = engine.value(QStringLiteral("title")).toString();

    turns.clear();
    for (const QJsonValue &v : engine.value(QStringLiteral("turns")).toArray()) {
        const QJsonObject o = v.toObject();
        Turn t;
        t.speakerKey = o.value(QStringLiteral("speakerKey")).toString();
        t.start = o.value(QStringLiteral("start")).toDouble();
        t.end = o.value(QStringLiteral("end")).toDouble();
        t.text = o.value(QStringLiteral("text")).toString();
        turns << t;
    }
    loadedTitle = root.value(QStringLiteral("title")).toString();
    if (!loadedTitle.isEmpty())
        titleEdit->setText(loadedTitle);

    QStringList all;
    for (const Turn &t : turns) all << t.text;
    mentionedNotes = suggester->mentionedTitles(all.join(QLatin1Char(' ')));
    selectedTurn = -1;
    renderTranscript();
    updateSuggestions();
    return true;
}

QString MeetingAssistantPage::colorFor(const QString &key) const
{
    if (key == QLatin1String("ao-vivo:presencial")) return QStringLiteral("#7A8699");
    if (key == QLatin1String("ao-vivo:remoto")) return QStringLiteral("#8E7AA8");
    for (int i = 0; i < speakers.size(); ++i)
        if (speakers[i].key == key) return QString::fromLatin1(kPalette[i % 8]);
    return QStringLiteral("#888888");
}

// Recording channel shown to the user: "presencial" = microphone in the room, "remoto" = computer audio
QString MeetingAssistantPage::channelLabel(const QString &channel) const
{
    if (channel == QLatin1String("presencial")) return tr("Room");
    if (channel == QLatin1String("remoto")) return tr("Remote");
    return channel;
}

QString MeetingAssistantPage::speakerName(const QString &key) const
{
    if (key == QLatin1String("ao-vivo:presencial")) return tr("Room");
    if (key == QLatin1String("ao-vivo:remoto")) return tr("Remote");
    for (const Speaker &s : speakers)
        if (s.key == key) return s.displayName();
    return QStringLiteral("?");
}

void MeetingAssistantPage::renderTranscript()
{
    const QColor muted = mix(palette().color(QPalette::Text), palette().color(QPalette::Base), 0.55);
    const QColor highlight = palette().color(QPalette::Highlight);

    if (turns.isEmpty()) {
        transcriptView->setHtml(tr(
            "<div style='color:%1; margin:30px'><h2>Meeting Assistant</h2><p>Click <b>Record</b> (in the toolbar above or in the Meeting menu) when the meeting starts. The conversation shows up here, split by person, and the notes in your vault related to what is being discussed appear on the side.</p><p>• <b>Online meeting</b> (Zoom, Teams, Meet, Slack): keep <i>Computer audio</i> checked. No bot joins the meeting.<br>• <b>In-person meeting</b>: uncheck <i>Computer audio</i>; only the microphone is recorded.<br>• <b>A recording you already have</b> (Plaud etc.): use <i>Open audio</i>.</p><p>Then use <b>Name voices</b> once: in future meetings people are recognized automatically.</p></div>").arg(muted.name()));
        return;
    }

    // Títulos de notas citados viram links para as notas
    QStringList titles = mentionedNotes.keys();
    std::sort(titles.begin(), titles.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    QRegularExpression titleRe;
    if (!titles.isEmpty()) {
        QStringList parts;
        for (const QString &t : titles) parts << QRegularExpression::escape(t.toHtmlEscaped());
        titleRe = QRegularExpression(QStringLiteral("(?<![\\p{L}\\p{N}])(%1)(?![\\p{L}\\p{N}])").arg(parts.join(QLatin1Char('|'))),
                                     QRegularExpression::CaseInsensitiveOption);
    }
    auto linkify = [&](const QString &escaped) {
        if (titles.isEmpty()) return escaped;
        QString out;
        int last = 0;
        auto it = titleRe.globalMatch(escaped);
        while (it.hasNext()) {
            const auto m = it.next();
            QString path;
            for (const QString &t : titles)
                if (t.toHtmlEscaped().compare(m.captured(1), Qt::CaseInsensitive) == 0) path = mentionedNotes.value(t);
            out += escaped.mid(last, m.capturedStart() - last);
            out += QStringLiteral("<a href=\"note:%1\" style=\"color:%2; text-decoration:none; border-bottom:1px dotted\">%3</a>")
                       .arg(QString::fromUtf8(QUrl::toPercentEncoding(path)), highlight.name(), m.captured(1));
            last = m.capturedEnd();
        }
        out += escaped.mid(last);
        return out;
    };

    const bool twoChannels = std::any_of(speakers.begin(), speakers.end(), [](const Speaker &s) { return s.channel == QLatin1String("remoto"); })
                             && std::any_of(speakers.begin(), speakers.end(), [](const Speaker &s) { return s.channel == QLatin1String("presencial"); });

    QString html;
    if (!engineTitle.isEmpty() || state == State::Recording) {
        html += QStringLiteral("<p style='color:%1; font-size:small'>%2</p>").arg(muted.name(),
            state == State::Recording ? tr("Live draft — the final version, with each person separated, appears when you stop.")
                                      : tr("Transcript: %1 · click the time of a line to see the connections for that part").arg(engineTitle.toHtmlEscaped()));
    }
    for (int i = 0; i < turns.size(); ++i) {
        const Turn &t = turns[i];
        const QString color = colorFor(t.speakerKey);
        const QString channel = t.draft ? tr("live")
                              : (twoChannels ? channelLabel(t.speakerKey.section(QLatin1Char(':'), 0, 0)) : QString());
        QString text = linkify(t.text.toHtmlEscaped());
        if (t.draft) text = QStringLiteral("<span style='color:%1'><i>%2</i></span>").arg(muted.name(), text);
        QString bg;
        if (i == selectedTurn) {
            const QColor base = palette().color(QPalette::Base);
            const QColor soft = QColor::fromRgbF(base.redF() * 0.85 + highlight.redF() * 0.15,
                                                 base.greenF() * 0.85 + highlight.greenF() * 0.15,
                                                 base.blueF() * 0.85 + highlight.blueF() * 0.15);
            bg = QStringLiteral(" bgcolor=\"%1\"").arg(soft.name());
        }
        html += QStringLiteral(
            "<table width='100%' cellspacing='0' cellpadding='5'%1><tr>"
            "<td width='70' valign='top'><a href='turn:%2' style='color:%3; text-decoration:none'>%4</a></td>"
            "<td valign='top'><span style='color:%5; font-weight:600'>●&nbsp;%6</span>%7<br>%8</td>"
            "</tr></table>")
            .arg(bg).arg(i).arg(muted.name(), clock(t.start), color, speakerName(t.speakerKey).toHtmlEscaped(),
                 channel.isEmpty() ? QString() : QStringLiteral(" <span style='color:%1; font-size:small'>%2</span>").arg(muted.name(), channel),
                 text);
    }

    const bool atBottom = transcriptView->verticalScrollBar()->value() >= transcriptView->verticalScrollBar()->maximum() - 20;
    const int scroll = transcriptView->verticalScrollBar()->value();
    transcriptView->setHtml(html);
    if (state == State::Recording && atBottom)
        transcriptView->verticalScrollBar()->setValue(transcriptView->verticalScrollBar()->maximum());
    else
        transcriptView->verticalScrollBar()->setValue(scroll);
}

QString MeetingAssistantPage::contextText() const
{
    QStringList parts;
    if (selectedTurn >= 0 && selectedTurn < turns.size()) {
        for (int i = qMax(0, selectedTurn - 1); i <= qMin(int(turns.size()) - 1, selectedTurn + 1); ++i)
            parts << turns[i].text;
    }
    else if (state == State::Recording && !turns.isEmpty()) {
        const double until = turns.last().end;
        for (const Turn &t : turns)
            if (t.end >= until - 180) parts << t.text;
    }
    else {
        for (const Turn &t : turns) parts << t.text;
    }
    return parts.join(QLatin1Char(' '));
}

void MeetingAssistantPage::updateSuggestions()
{
    const QColor muted = mix(palette().color(QPalette::Text), palette().color(QPalette::Base), 0.55);
    QString scope;
    if (selectedTurn >= 0 && selectedTurn < turns.size())
        scope = tr("part at %1").arg(clock(turns[selectedTurn].start));
    else if (state == State::Recording)
        scope = tr("last 3 minutes");
    else
        scope = tr("whole meeting");
    contextLabel->setText(tr("<span style='font-size:large'><b>Connections to your notes</b></span><br><span style='color:%1'>%2</span>").arg(muted.name(), scope));
    wholeMeetingButton->setVisible(selectedTurn >= 0);

    suggestionList->clear();
    suggester->setCurrentRecording(folder);
    const QString text = contextText();
    if (!NotepadNextHub::instance()->index()->hasRoot()) {
        topicsLabel->setText(tr("<span style='color:%1'>Open a vault (sidebar) to see connections.</span>").arg(muted.name()));
        participationLabel->clear();
        return;
    }
    if (text.trimmed().isEmpty()) {
        topicsLabel->setText(tr("<span style='color:%1'>—</span>").arg(muted.name()));
        participationLabel->clear();
        return;
    }

    QStringList chips;
    for (const QString &t : suggester->keyTerms(text, 8))
        chips << QStringLiteral("<span style='background-color:rgba(155,107,224,0.25)'>&nbsp;%1&nbsp;</span>").arg(t.toHtmlEscaped());
    topicsLabel->setText(chips.join(QStringLiteral(" &nbsp;")));

    const QList<NoteSuggestion> list = suggester->suggest(text, 10);
    if (list.isEmpty()) {
        QListWidgetItem *item = new QListWidgetItem(tr("No related notes yet."), suggestionList);
        item->setFlags(Qt::NoItemFlags);
    }
    for (const NoteSuggestion &s : list) {
        QStringList why;
        if (s.mentioned) why << tr("mentioned in the conversation");
        if (!s.sharedTags.isEmpty()) why << s.sharedTags.join(QStringLiteral(" "));
        if (!s.sharedTerms.isEmpty()) why << tr("topics: %1").arg(s.sharedTerms.join(QStringLiteral(", ")));
        QListWidgetItem *item = new QListWidgetItem(suggestionList);
        item->setData(Qt::UserRole, s.path);
        item->setToolTip(s.path);
        QLabel *card = new QLabel(QStringLiteral("<b>%1</b>%2<br><span style='color:%3; font-size:small'>%4</span>")
                                  .arg(s.title.toHtmlEscaped(),
                                       s.mentioned ? QStringLiteral(" <span style='color:%1'>●</span>").arg(palette().color(QPalette::Highlight).name()) : QString(),
                                       muted.name(), why.join(QStringLiteral(" · ")).toHtmlEscaped()));
        card->setWordWrap(true);
        card->setContentsMargins(4, 4, 4, 4);
        card->setAttribute(Qt::WA_TransparentForMouseEvents);
        const int w = qMax(160, suggestionList->viewport()->width() - 8);
        item->setSizeHint(QSize(w, card->heightForWidth(w) + 6));
        suggestionList->setItemWidget(item, card);
    }

    // Participação de cada pessoa (tempo de fala nas falas finais)
    QHash<QString, double> talk;
    double total = 0;
    for (const Turn &t : turns) {
        if (t.draft) continue;
        talk[t.speakerKey] += t.end - t.start;
        total += t.end - t.start;
    }
    QStringList rows;
    for (const Speaker &s : speakers) {
        if (total <= 0 || talk.value(s.key) <= 0) continue;
        const int pct = qRound(100.0 * talk.value(s.key) / total);
        rows << QStringLiteral("<span style='color:%1'>●</span> %2 &nbsp;<span style='color:%3'>%4%</span>")
                    .arg(colorFor(s.key), s.displayName().toHtmlEscaped(), muted.name()).arg(pct);
    }
    if (rows.isEmpty() && state == State::Recording)
        rows << tr("<span style='color:%1'>Appears when the recording ends.</span>").arg(muted.name());
    participationLabel->setText(rows.join(QStringLiteral("<br>")));
}

void MeetingAssistantPage::handleLink(const QUrl &url)
{
    const QString s = url.toString();
    if (s.startsWith(QLatin1String("turn:"))) {
        const int i = s.mid(5).toInt();
        selectedTurn = selectedTurn == i ? -1 : i;
        renderTranscript();
        updateSuggestions();
    }
    else if (s.startsWith(QLatin1String("note:"))) {
        const QString path = QUrl::fromPercentEncoding(s.mid(5).toUtf8());
        if (!path.isEmpty()) emit NotepadNextHub::instance()->openFileRequested(path, -1);
    }
}

void MeetingAssistantPage::saveAsNote()
{
    if (turns.isEmpty()) return;
    VaultIndex *index = NotepadNextHub::instance()->index();
    QString dir = index->hasRoot() ? QDir(index->root()).filePath(QStringLiteral("Reuniões"))
                                   : QFileDialog::getExistingDirectory(this, tr("Save the note in"), QDir::homePath());
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);

    QString title = titleEdit->text().simplified();
    if (title.isEmpty()) title = QDateTime::currentDateTime().toString(QStringLiteral("dd-MM")) + tr(" Meeting");
    QString fileName = title;
    fileName.remove(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")));
    QString path = QDir(dir).filePath(fileName + QStringLiteral(".md"));
    for (int i = 2; QFile::exists(path); ++i)
        path = QDir(dir).filePath(QStringLiteral("%1 (%2).md").arg(fileName).arg(i));

    // Participantes e tempo de fala
    QHash<QString, double> talk;
    double total = 0;
    for (const Turn &t : turns) { talk[t.speakerKey] += t.end - t.start; total += t.end - t.start; }
    QStringList names;
    QString people;
    for (const Speaker &s : speakers) {
        if (talk.value(s.key) <= 0) continue;
        names << QStringLiteral("\"%1\"").arg(s.displayName());
        people += QStringLiteral("- %1 — %2%\n").arg(s.displayName()).arg(total > 0 ? qRound(100 * talk.value(s.key) / total) : 0);
    }

    // Citações de notas viram [[links]] (primeira vez em cada fala)
    QStringList titles = mentionedNotes.keys();
    std::sort(titles.begin(), titles.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });

    QString md;
    md += QStringLiteral("---\ntipo: reuniao\ndata: %1\nparticipantes: [%2]\ngravacao: \"%3\"\n---\n\n")
              .arg(QDate::currentDate().toString(Qt::ISODate), names.join(QStringLiteral(", ")), folder);
    md += QStringLiteral("# %1\n\n").arg(title);
    if (!people.isEmpty()) md += tr("## Participants\n\n") + people + QLatin1Char('\n');

    const QList<NoteSuggestion> related = suggester->suggest([this] {
        QStringList all; for (const Turn &t : turns) all << t.text; return all.join(QLatin1Char(' '));
    }(), 8);
    if (!related.isEmpty()) {
        md += tr("## Related notes\n\n");
        for (const NoteSuggestion &s : related) md += QStringLiteral("- [[%1]]\n").arg(s.title);
        md += QLatin1Char('\n');
    }

    md += tr("## Transcript\n\n");
    for (const Turn &t : turns) {
        if (t.draft) continue;
        QString text = t.text;
        for (const QString &ti : titles) {
            const QRegularExpression re(QStringLiteral("(?<![\\p{L}\\p{N}\\[])(%1)(?![\\p{L}\\p{N}\\]])").arg(QRegularExpression::escape(ti)),
                                        QRegularExpression::CaseInsensitiveOption);
            const auto m = re.match(text);
            if (m.hasMatch())
                text.replace(m.capturedStart(1), m.capturedLength(1), QStringLiteral("[[%1|%2]]").arg(ti, m.captured(1)));
        }
        md += QStringLiteral("**%1 · %2** — %3\n\n").arg(clock(t.start), speakerName(t.speakerKey), text);
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Meeting Assistant"), tr("Could not save %1").arg(path));
        return;
    }
    f.write(md.toUtf8());
    f.close();
    emit NotepadNextHub::instance()->openFileRequested(path, -1);
}
