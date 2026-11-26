/*
 * NotepadNext - recursos próprios deste fork.
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#ifndef WORKSPACECONTROLLER_H
#define WORKSPACECONTROLLER_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>

class GraphPage;
class MainWindow;
class MeetingAssistantPage;
class QAction;
class QActionGroup;
class QDockWidget;
class QMenu;
class QMenuBar;
class QStackedWidget;
class QTabBar;
class QToolBar;
class ScintillaNext;

// Abas do topo da janela: Anotações | Grafo | Meeting Assistant.
// Cada aba tem a sua área central, os seus menus e a sua barra de ícones.
// Ao sair de Anotações, menus, barras e painéis do editor ficam escondidos e os
// atalhos do editor são desligados; ao voltar, tudo é restaurado como estava.
class WorkspaceController : public QObject
{
    Q_OBJECT

public:
    enum Mode { Notes = 0, Graph = 1, Meeting = 2 };

    WorkspaceController(MainWindow *window, QMenuBar *menuBar, QMenu *viewMenu, QMenu *helpMenu,
                        QToolBar *firstToolBar, QAction *openVaultAction);

    Mode mode() const { return current; }
    void setMode(Mode mode);
    void showNotes() { setMode(Notes); }

    // Antes de salvar o layout da janela: volta para Anotações (o layout salvo é sempre o do editor)
    void prepareForSave();
    // Na abertura: volta para a aba usada por último
    void restoreMode();

private:
    void buildTabs(QToolBar *firstToolBar);
    void buildModeActions(QMenu *viewMenu);
    void buildGraphUi(QMenuBar *menuBar, QMenu *helpMenu, QAction *openVaultAction);
    void buildMeetingUi(QMenuBar *menuBar, QMenu *helpMenu);
    QMenu *buildCompactViewMenu(QMenuBar *menuBar, QMenu *helpMenu);
    QToolBar *makeToolBar(const QString &title, const QString &objectName);
    void setIcon(QAction *action, const QString &icon);
    void refreshIcons();

    void suspendNotes();
    void resumeNotes();
    void applyMenus();

    MainWindow *window;
    QStackedWidget *stack;
    QTabBar *tabs;
    QToolBar *tabsBar;
    GraphPage *graphPage;
    MeetingAssistantPage *meetingPage;

    QActionGroup *modeGroup;
    QList<QAction *> modeActions;

    QToolBar *graphBar;
    QToolBar *meetingBar;
    QMenu *graphMenu;
    QMenu *meetingMenu;
    QMenu *graphViewMenu;
    QMenu *meetingViewMenu;
    QAction *helpMenuAction;
    QList<QAction *> originalMenus;
    QHash<QAction *, QString> iconNames;

    Mode current = Notes;
    QPointer<ScintillaNext> lastActivated;

    // Estado do editor guardado enquanto outra aba está aberta
    QList<QPair<QPointer<QAction>, bool>> savedEnabled;
    QHash<QAction *, bool> savedMenuVisible;
    QList<QPointer<QDockWidget>> hiddenDocks;
    QList<QPointer<QToolBar>> hiddenToolBars;
    bool statusBarWasVisible = true;
};

#endif // WORKSPACECONTROLLER_H
