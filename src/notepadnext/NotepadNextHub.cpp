/*
 * NotepadNext - recursos de notas no estilo Obsidian (vault, links, tags, grafo).
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#include "NotepadNextHub.h"
#include "VaultIndex.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>

NotepadNextHub::NotepadNextHub(QObject *parent) :
    QObject(parent),
    vaultIndex(new VaultIndex(this))
{
}

NotepadNextHub *NotepadNextHub::instance()
{
    static NotepadNextHub *hub = new NotepadNextHub(QCoreApplication::instance());
    return hub;
}

void NotepadNextHub::migrateLegacyNames()
{
    const QString oldMeetings = QStringLiteral("craftpad-reunioes");
    const QString newMeetings = QStringLiteral("NotepadNext-reunioes");

    // Configurações: CraftPad/... e craftpad/... passam a NotepadNext/...
    QSettings settings;
    for (const QString &key : settings.allKeys()) {
        QString suffix;
        if (key.startsWith(QLatin1String("CraftPad/")) || key.startsWith(QLatin1String("craftpad/")))
            suffix = key.mid(9);
        else
            continue;
        const QString newKey = QStringLiteral("NotepadNext/") + suffix;
        QVariant value = settings.value(key);
        if (value.typeId() == QMetaType::QString)
            value = value.toString().replace(oldMeetings, newMeetings);
        if (!settings.contains(newKey))
            settings.setValue(newKey, value);
        settings.remove(key);
    }

    // Pasta das reuniões gravadas
    QDir home = QDir::home();
    if (home.exists(oldMeetings) && !home.exists(newMeetings))
        home.rename(oldMeetings, newMeetings);

    // Banco de vozes e vocabulário (macOS)
    const QString support = home.filePath(QStringLiteral("Library/Application Support"));
    const QDir oldDir(QDir(support).filePath(QStringLiteral("craftpad")));
    if (oldDir.exists()) {
        const QDir newDir(QDir(support).filePath(QStringLiteral("NotepadNext")));
        QDir().mkpath(newDir.path());
        for (const QString &name : oldDir.entryList(QDir::Files)) {
            if (!newDir.exists(name))
                QFile::rename(oldDir.filePath(name), newDir.filePath(name));
        }
        QDir().rmdir(oldDir.path()); // só apaga se ficou vazia
    }
}

QStringList NotepadNextHub::availableSidebarFormats()
{
    return {QStringLiteral("md"), QStringLiteral("txt"), QStringLiteral("csv")};
}

QStringList NotepadNextHub::sidebarFormats()
{
    QStringList formats = QSettings().value(QStringLiteral("NotepadNext/sidebar/formats"), QStringList{QStringLiteral("md")}).toStringList();
    if (!formats.contains(QStringLiteral("md")))
        formats.prepend(QStringLiteral("md"));
    return formats;
}

void NotepadNextHub::setSidebarFormats(const QStringList &formats)
{
    QStringList clean{QStringLiteral("md")};
    for (const QString &f : formats)
        if (availableSidebarFormats().contains(f) && !clean.contains(f))
            clean << f;
    if (clean == sidebarFormats())
        return;
    QSettings().setValue(QStringLiteral("NotepadNext/sidebar/formats"), clean);
    emit sidebarFormatsChanged(clean);
}
