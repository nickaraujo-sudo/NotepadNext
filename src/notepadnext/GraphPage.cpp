/*
 * NotepadNext - recursos de notas no estilo Obsidian (vault, links, tags, grafo).
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#include "GraphPage.h"
#include "GraphView.h"
#include "NotepadNextHub.h"

#include "MainWindow.h"
#include "ScintillaNext.h"

#include <QAction>
#include <QLabel>
#include <QSettings>
#include <QVBoxLayout>

GraphPage::GraphPage(MainWindow *window) :
    QWidget(window)
{
    setObjectName(QStringLiteral("NotepadNextGraphPage"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    view = new GraphView(NotepadNextHub::instance()->index(), this);
    layout->addWidget(view, 1);

    QSettings settings;
    auto makeToggle = [this](const QString &text, const QString &tip, const QString &key) {
        QAction *a = new QAction(text, this);
        a->setToolTip(tip);
        a->setCheckable(true);
        a->setChecked(QSettings().value(key, true).toBool());
        connect(a, &QAction::toggled, this, [key](bool on) { QSettings().setValue(key, on); });
        return a;
    };
    tagsAction = makeToggle(tr("Tags"), tr("Mostrar as tags como pontos no grafo"), QStringLiteral("NotepadNext/GraphTags"));
    orphansAction = makeToggle(tr("Notas sem links"), tr("Mostrar notas que não têm links"), QStringLiteral("NotepadNext/GraphOrphans"));
    spinActionPtr = makeToggle(tr("Girar"), tr("Girar o grafo devagar quando ninguém está mexendo"), QStringLiteral("NotepadNext/GraphSpin"));
    fitActionPtr = new QAction(tr("Centralizar"), this);
    fitActionPtr->setToolTip(tr("Enquadrar o grafo inteiro na tela"));

    view->setShowTags(tagsAction->isChecked());
    view->setShowOrphans(orphansAction->isChecked());
    view->setIdleRotation(spinActionPtr->isChecked());
    connect(tagsAction, &QAction::toggled, view, &GraphView::setShowTags);
    connect(orphansAction, &QAction::toggled, view, &GraphView::setShowOrphans);
    connect(spinActionPtr, &QAction::toggled, view, &GraphView::setIdleRotation);
    connect(fitActionPtr, &QAction::triggered, view, &GraphView::fitToView);

    stats = new QLabel(this);
    stats->setContentsMargins(8, 0, 8, 0);
    auto updateStats = [this]() {
        stats->setText(tr("%1 notas · %2 tags").arg(view->noteCount()).arg(view->tagCount()));
    };
    connect(view, &GraphView::statsChanged, this, updateStats);
    updateStats();

    connect(window, &MainWindow::editorActivated, this, &GraphPage::editorActivated);
}

void GraphPage::editorActivated(ScintillaNext *editor)
{
    view->setCurrentFile(editor && editor->isFile() ? editor->getFilePath() : QString());
}
