/*
 * NotepadNext - recursos próprios deste fork.
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#include "WorkspaceController.h"

#include "EditorInfoStatusBar.h"
#include "GraphPage.h"
#include "MainWindow.h"
#include "MeetingAssistantPage.h"
#include "NotepadNextHub.h"
#include "ScintillaNext.h"
#include "ThemeManager.h"

#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QSettings>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>

static const char *kModeKey = "NotepadNext/workspace/mode";

static void collectActions(QMenu *menu, QSet<QAction *> &out)
{
    for (QAction *a : menu->actions()) {
        out.insert(a);
        if (a->menu())
            collectActions(a->menu(), out);
    }
}

WorkspaceController::WorkspaceController(MainWindow *window, QMenuBar *menuBar, QMenu *viewMenu, QMenu *helpMenu,
                                         QToolBar *firstToolBar, QAction *openVaultAction)
    : QObject(window), window(window)
{
    // Menus do editor (os que existem antes das abas): ficam só em Anotações
    originalMenus = menuBar->actions();
    helpMenuAction = helpMenu->menuAction();

    // Área central: editor | grafo | reunião
    graphPage = new GraphPage(window);
    meetingPage = new MeetingAssistantPage(window);
    stack = new QStackedWidget(window);
    stack->setObjectName(QStringLiteral("workspaceStack"));
    QWidget *editorArea = window->takeCentralWidget();
    stack->addWidget(editorArea);
    stack->addWidget(graphPage);
    stack->addWidget(meetingPage);
    window->setCentralWidget(stack);

    buildTabs(firstToolBar);
    buildModeActions(viewMenu);
    buildGraphUi(menuBar, helpMenu, openVaultAction);
    buildMeetingUi(menuBar, helpMenu);
    refreshIcons();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &WorkspaceController::refreshIcons);

    // Pedidos que levam de volta às anotações (clique numa nota, tag ou link)
    NotepadNextHub *hub = NotepadNextHub::instance();
    connect(hub, &NotepadNextHub::tagRequested, this, &WorkspaceController::showNotes);
    connect(hub, &NotepadNextHub::vaultRequested, this, &WorkspaceController::showNotes);

    // Editor ativo de verdade (o "atual" do painel pode atrasar quando a área estava escondida)
    connect(window, &MainWindow::editorActivated, this, [this](ScintillaNext *editor) { lastActivated = editor; });

    applyMenus();
    graphBar->hide();
    meetingBar->hide();
}

QToolBar *WorkspaceController::makeToolBar(const QString &title, const QString &objectName)
{
    QToolBar *bar = new QToolBar(title, window);
    bar->setObjectName(objectName);
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setIconSize(QSize(18, 18));
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->toggleViewAction()->setVisible(false);
    window->addToolBar(Qt::TopToolBarArea, bar);
    return bar;
}

void WorkspaceController::setIcon(QAction *action, const QString &icon)
{
    iconNames.insert(action, icon);
}

void WorkspaceController::refreshIcons()
{
    ThemeManager *tm = ThemeManager::instance();
    for (auto it = iconNames.constBegin(); it != iconNames.constEnd(); ++it)
        it.key()->setIcon(tm->icon(it.value()));
    const QStringList tabIcons = {QStringLiteral("notes"), QStringLiteral("graph"), QStringLiteral("meeting")};
    for (int i = 0; i < tabs->count() && i < tabIcons.size(); ++i)
        tabs->setTabIcon(i, tm->icon(tabIcons[i]));
}

void WorkspaceController::buildTabs(QToolBar *firstToolBar)
{
    tabsBar = new QToolBar(tr("Abas"), window);
    tabsBar->setObjectName(QStringLiteral("workspaceTabsToolBar"));
    tabsBar->setMovable(false);
    tabsBar->setFloatable(false);
    tabsBar->toggleViewAction()->setVisible(false);
    tabsBar->setStyleSheet(QStringLiteral("QToolBar#workspaceTabsToolBar { border: none; padding: 4px 6px 0 6px; spacing: 0; }"));

    tabs = new QTabBar(tabsBar);
    tabs->setObjectName(QStringLiteral("workspaceTabs"));
    tabs->setDocumentMode(true);
    tabs->setExpanding(false);
    tabs->setDrawBase(false);
    tabs->setUsesScrollButtons(false);
    tabs->setIconSize(QSize(16, 16));
    tabs->setStyleSheet(QStringLiteral(
        "QTabBar#workspaceTabs::tab { padding: 6px 16px; margin-right: 2px; border: none;"
        " border-bottom: 3px solid transparent; background: transparent; }"
        "QTabBar#workspaceTabs::tab:selected { border-bottom: 3px solid #9B6BE0; font-weight: 600; }"
        "QTabBar#workspaceTabs::tab:hover:!selected { border-bottom: 3px solid rgba(155,107,224,0.35); }"));
    tabs->addTab(tr("Anotações"));
    tabs->addTab(tr("Grafo"));
    tabs->addTab(tr("Meeting Assistant"));
    tabs->setTabToolTip(0, tr("Notas em Markdown, prévia, vault e backlinks (%1)").arg(QKeySequence(Qt::CTRL | Qt::Key_1).toString(QKeySequence::NativeText)));
    tabs->setTabToolTip(1, tr("Grafo do vault (%1)").arg(QKeySequence(Qt::CTRL | Qt::Key_2).toString(QKeySequence::NativeText)));
    tabs->setTabToolTip(2, tr("Gravação e transcrição de reuniões (%1)").arg(QKeySequence(Qt::CTRL | Qt::Key_3).toString(QKeySequence::NativeText)));
    QFont f = tabs->font();
    f.setPointSizeF(f.pointSizeF() + 1);
    tabs->setFont(f);
    tabsBar->addWidget(tabs);
    connect(tabs, &QTabBar::currentChanged, this, [this](int index) { setMode(Mode(index)); });

    window->insertToolBar(firstToolBar, tabsBar);
    window->insertToolBarBreak(firstToolBar);
}

void WorkspaceController::buildModeActions(QMenu *viewMenu)
{
    modeGroup = new QActionGroup(this);
    const QStringList names = {tr("Anotações"), tr("Grafo"), tr("Meeting Assistant")};
    const QStringList icons = {QStringLiteral("notes"), QStringLiteral("graph"), QStringLiteral("meeting")};
    const int keys[] = {Qt::Key_1, Qt::Key_2, Qt::Key_3};
    for (int i = 0; i < 3; ++i) {
        QAction *a = new QAction(names[i], this);
        a->setObjectName(QStringLiteral("actionWorkspace%1").arg(i));
        a->setCheckable(true);
        a->setShortcut(QKeySequence(Qt::CTRL | keys[i]));
        a->setChecked(i == 0);
        setIcon(a, icons[i]);
        modeGroup->addAction(a);
        window->addAction(a); // atalho funciona em qualquer aba
        connect(a, &QAction::triggered, this, [this, i]() { setMode(Mode(i)); });
        modeActions << a;
    }

    // No menu View das anotações, as abas ficam no topo
    QAction *first = viewMenu->actions().value(0);
    viewMenu->insertActions(first, modeActions);
    viewMenu->insertSeparator(first);
}

QMenu *WorkspaceController::buildCompactViewMenu(QMenuBar *menuBar, QMenu *helpMenu)
{
    // "Exibir" das abas Grafo e Meeting Assistant: só o que vale fora do editor
    QMenu *menu = new QMenu(tr("&Exibir"), window);
    menu->addActions(modeActions);
    menu->addSeparator();
    QMenu *themeMenu = menu->addMenu(tr("Tema"));
    connect(themeMenu, &QMenu::aboutToShow, this, [themeMenu]() {
        themeMenu->clear();
        QActionGroup *group = new QActionGroup(themeMenu);
        ThemeManager *tm = ThemeManager::instance();
        for (const AppTheme &t : tm->themes()) {
            QAction *a = themeMenu->addAction(t.name);
            a->setCheckable(true);
            a->setChecked(t.id == tm->currentId());
            group->addAction(a);
            const QString id = t.id;
            connect(a, &QAction::triggered, tm, [id]() { ThemeManager::instance()->setTheme(id); });
        }
    });
    // macOS: o menu precisa de ao menos um item para aparecer na primeira vez
    themeMenu->addAction(QStringLiteral("…"));
    // Sair (no macOS ele vai para o menu do app, como sempre)
    if (QAction *exit = window->findChild<QAction *>(QStringLiteral("actionExit"))) {
        menu->addSeparator();
        menu->addAction(exit);
    }
    menuBar->insertMenu(helpMenu->menuAction(), menu);
    return menu;
}

void WorkspaceController::buildGraphUi(QMenuBar *menuBar, QMenu *helpMenu, QAction *openVaultAction)
{
    QAction *tags = graphPage->showTagsAction();
    QAction *orphans = graphPage->showOrphansAction();
    QAction *spin = graphPage->spinAction();
    QAction *fit = graphPage->fitAction();
    setIcon(tags, QStringLiteral("graph-tags"));
    setIcon(orphans, QStringLiteral("graph-orphans"));
    setIcon(spin, QStringLiteral("graph-spin"));
    setIcon(fit, QStringLiteral("graph-fit"));
    fit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    fit->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    graphPage->addAction(fit);

    QAction *vault = new QAction(tr("Abrir pasta do vault…"), this);
    setIcon(vault, QStringLiteral("vault"));
    connect(vault, &QAction::triggered, openVaultAction, &QAction::trigger);

    graphBar = makeToolBar(tr("Grafo"), QStringLiteral("graphToolBar"));
    graphBar->addAction(vault);
    graphBar->addSeparator();
    graphBar->addActions({tags, orphans, spin});
    graphBar->addSeparator();
    graphBar->addAction(fit);
    QWidget *spacer = new QWidget(graphBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    graphBar->addWidget(spacer);
    graphBar->addWidget(graphPage->statsLabel());

    graphMenu = new QMenu(tr("&Grafo"), window);
    graphMenu->addAction(vault);
    graphMenu->addSeparator();
    graphMenu->addActions({tags, orphans, spin});
    graphMenu->addSeparator();
    graphMenu->addAction(fit);
    menuBar->insertMenu(helpMenu->menuAction(), graphMenu);
    graphViewMenu = buildCompactViewMenu(menuBar, helpMenu);
}

void WorkspaceController::buildMeetingUi(QMenuBar *menuBar, QMenu *helpMenu)
{
    MeetingAssistantPage *m = meetingPage;
    setIcon(m->recordAction, QStringLiteral("record"));
    setIcon(m->stopAction, QStringLiteral("stop"));
    setIcon(m->openMeetingAction, QStringLiteral("folder-open"));
    setIcon(m->openAudioAction, QStringLiteral("audio-file"));
    setIcon(m->reanalyzeAction, QStringLiteral("refresh"));
    setIcon(m->nameAction, QStringLiteral("voices"));
    setIcon(m->saveAction, QStringLiteral("note-save"));
    m->recordAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    m->stopAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Period));
    m->saveAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
    for (QAction *a : {m->recordAction, m->stopAction, m->saveAction}) {
        // Atalhos valem só dentro da aba (Ctrl+S continua salvando arquivos no editor)
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        m->addAction(a);
    }

    meetingBar = makeToolBar(tr("Reunião"), QStringLiteral("meetingToolBar"));
    meetingBar->addActions({m->recordAction, m->stopAction});
    meetingBar->addSeparator();
    meetingBar->addActions({m->openMeetingAction, m->openAudioAction});
    meetingBar->addSeparator();
    meetingBar->addActions({m->reanalyzeAction, m->nameAction, m->saveAction});

    meetingMenu = new QMenu(tr("&Reunião"), window);
    meetingMenu->addActions({m->recordAction, m->stopAction});
    meetingMenu->addSeparator();
    meetingMenu->addActions({m->openMeetingAction, m->openAudioAction});
    meetingMenu->addSeparator();
    meetingMenu->addActions({m->reanalyzeAction, m->nameAction, m->saveAction});
    menuBar->insertMenu(helpMenu->menuAction(), meetingMenu);
    meetingViewMenu = buildCompactViewMenu(menuBar, helpMenu);
}

void WorkspaceController::applyMenus()
{
    graphMenu->menuAction()->setVisible(current == Graph);
    graphViewMenu->menuAction()->setVisible(current == Graph);
    meetingMenu->menuAction()->setVisible(current == Meeting);
    meetingViewMenu->menuAction()->setVisible(current == Meeting);
}

void WorkspaceController::suspendNotes()
{
    // Menus do editor (menos Ajuda)
    savedMenuVisible.clear();
    QSet<QAction *> actions;
    for (QAction *menuAction : originalMenus) {
        savedMenuVisible.insert(menuAction, menuAction->isVisible());
        if (menuAction == helpMenuAction)
            continue;
        menuAction->setVisible(false);
        if (menuAction->menu())
            collectActions(menuAction->menu(), actions);
    }

    // Barras de ícones, painéis e barra de status do editor
    hiddenToolBars.clear();
    for (QToolBar *bar : window->findChildren<QToolBar *>()) {
        if (bar->parentWidget() != window || bar == tabsBar || bar == graphBar || bar == meetingBar)
            continue;
        for (QAction *a : bar->actions()) actions.insert(a);
        if (!bar->isHidden()) {
            hiddenToolBars << bar;
            bar->hide();
        }
    }
    hiddenDocks.clear();
    for (QDockWidget *dock : window->findChildren<QDockWidget *>()) {
        if (dock->parentWidget() != window || dock->isHidden())
            continue;
        hiddenDocks << dock;
        dock->hide();
    }
    statusBarWasVisible = !window->statusBar()->isHidden();
    window->statusBar()->hide();

    // Atalhos do editor ficam desligados (ex.: ⌘W não fecha um arquivo escondido)
    for (QAction *a : window->actions()) actions.insert(a);
    QSet<QAction *> keep(modeActions.begin(), modeActions.end());
    if (helpMenuAction->menu()) collectActions(helpMenuAction->menu(), keep);
    savedEnabled.clear();
    for (QAction *a : std::as_const(actions)) {
        if (keep.contains(a) || a->isSeparator())
            continue;
        const QAction::MenuRole role = a->menuRole();
        if (role == QAction::QuitRole || role == QAction::PreferencesRole || role == QAction::AboutRole
            || role == QAction::AboutQtRole || a->objectName() == QLatin1String("actionExit")
            || a->objectName() == QLatin1String("actionPreferences"))
            continue;
        savedEnabled << qMakePair(QPointer<QAction>(a), a->isEnabled());
        a->setEnabled(false);
    }
}

void WorkspaceController::resumeNotes()
{
    for (const auto &entry : std::as_const(savedEnabled))
        if (entry.first) entry.first->setEnabled(entry.second);
    savedEnabled.clear();

    for (auto it = savedMenuVisible.constBegin(); it != savedMenuVisible.constEnd(); ++it)
        it.key()->setVisible(it.value());
    savedMenuVisible.clear();

    for (const QPointer<QToolBar> &bar : std::as_const(hiddenToolBars))
        if (bar) bar->show();
    hiddenToolBars.clear();
    for (const QPointer<QDockWidget> &dock : std::as_const(hiddenDocks))
        if (dock) dock->show();
    hiddenDocks.clear();
    window->statusBar()->setVisible(statusBarWasVisible);

    // Depois de abrir o arquivo pedido (se houver), atualiza menus e barra de status do editor atual
    QTimer::singleShot(0, this, [this]() {
        if (current != Notes) return;
        // Editor visível (o recém-aberto, se houver); com vários lado a lado, o último usado
        ScintillaNext *editor = nullptr;
        for (ScintillaNext *e : window->findChildren<ScintillaNext *>()) {
            if (!e->isVisible()) continue;
            if (!editor || e == lastActivated) editor = e;
        }
        if (!editor) editor = window->currentEditor();
        if (!editor) return;
        window->updateGui(editor);
        if (auto *status = qobject_cast<EditorInfoStatusBar *>(window->statusBar()))
            status->refresh(editor);
        static_cast<QWidget *>(editor)->setFocus();
    });
}

void WorkspaceController::setMode(Mode mode)
{
    if (mode == current) {
        // Só sincroniza aba e menu (ex.: clique na aba que já está aberta)
        modeActions.value(mode)->setChecked(true);
        return;
    }
    const Mode old = current;
    if (old == Notes)
        suspendNotes();

    current = mode;
    stack->setCurrentIndex(mode);
    graphBar->setVisible(mode == Graph);
    meetingBar->setVisible(mode == Meeting);
    applyMenus();

    if (mode == Notes)
        resumeNotes();
    else if (mode == Graph)
        graphPage->setFocus();
    else
        meetingPage->setFocus();

    {
        const QSignalBlocker block(tabs);
        tabs->setCurrentIndex(mode);
    }
    modeActions.value(mode)->setChecked(true);
}

void WorkspaceController::prepareForSave()
{
    QSettings().setValue(QLatin1String(kModeKey), int(current));
    setMode(Notes);
}

void WorkspaceController::restoreMode()
{
    const int saved = QSettings().value(QLatin1String(kModeKey), 0).toInt();
    // Garante que as barras das outras abas comecem escondidas, seja qual for o layout salvo
    graphBar->setVisible(current == Graph);
    meetingBar->setVisible(current == Meeting);
    if (saved >= Notes && saved <= Meeting)
        setMode(Mode(saved));
}
