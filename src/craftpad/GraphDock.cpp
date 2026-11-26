/*
 * CraftPad - extensão do Notepad Next no estilo Obsidian.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#include "GraphDock.h"
#include "CraftPadHub.h"
#include "GraphView.h"

#include "MainWindow.h"
#include "ScintillaNext.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

GraphDock::GraphDock(MainWindow *parent) :
    QDockWidget(tr("Grafo"), parent)
{
    setObjectName(QStringLiteral("CraftPadGraphDock"));

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Barra de opções
    auto *bar = new QWidget(container);
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(8, 4, 8, 4);
    QSettings settings;
    auto *tagsBox = new QCheckBox(tr("Tags"), bar);
    tagsBox->setChecked(settings.value(QStringLiteral("CraftPad/GraphTags"), true).toBool());
    auto *orphansBox = new QCheckBox(tr("Notas sem links"), bar);
    orphansBox->setChecked(settings.value(QStringLiteral("CraftPad/GraphOrphans"), true).toBool());
    auto *spinBox = new QCheckBox(tr("Girar quando parado"), bar);
    spinBox->setChecked(settings.value(QStringLiteral("CraftPad/GraphSpin"), true).toBool());
    auto *fitBtn = new QToolButton(bar);
    fitBtn->setText(tr("Centralizar"));
    auto *stats = new QLabel(bar);
    barLayout->addWidget(tagsBox);
    barLayout->addWidget(orphansBox);
    barLayout->addWidget(spinBox);
    barLayout->addStretch(1);
    barLayout->addWidget(stats);
    barLayout->addWidget(fitBtn);
    layout->addWidget(bar);

    view = new GraphView(CraftPadHub::instance()->index(), container);
    view->setShowTags(tagsBox->isChecked());
    view->setShowOrphans(orphansBox->isChecked());
    view->setIdleRotation(spinBox->isChecked());
    layout->addWidget(view, 1);
    setWidget(container);

    auto updateStats = [this, stats]() {
        stats->setText(tr("%1 notas · %2 tags").arg(view->noteCount()).arg(view->tagCount()));
    };
    connect(view, &GraphView::statsChanged, this, updateStats);
    updateStats();

    connect(tagsBox, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("CraftPad/GraphTags"), on);
        view->setShowTags(on);
    });
    connect(orphansBox, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("CraftPad/GraphOrphans"), on);
        view->setShowOrphans(on);
    });
    connect(spinBox, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("CraftPad/GraphSpin"), on);
        view->setIdleRotation(on);
    });
    connect(fitBtn, &QToolButton::clicked, view, &GraphView::fitToView);

    connect(parent, &MainWindow::editorActivated, this, &GraphDock::editorActivated);
}

void GraphDock::editorActivated(ScintillaNext *editor)
{
    view->setCurrentFile(editor && editor->isFile() ? editor->getFilePath() : QString());
}
