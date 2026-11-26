/*
 * craftpad++ - extensão do Notepad Next.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#ifndef FORMATCONTROLLER_H
#define FORMATCONTROLLER_H

#include <QHash>
#include <QObject>
#include <QPointer>

#include <functional>

class MainWindow;
class MarkdownPreviewDock;
class QAction;
class QMenu;
class QMenuBar;
class QToolBar;
class QTextCharFormat;
class ScintillaNext;

// Barra e menu "Formatar": texto, listas, alinhamento, tabelas e imagens.
// Funciona no editor (altera o Markdown) e na prévia editável.
class FormatController : public QObject
{
    Q_OBJECT

public:
    FormatController(MainWindow *window, MarkdownPreviewDock *preview, QMenuBar *menuBar, QAction *menuBefore, QMenu *viewMenu);

    static bool isMarkdown(ScintillaNext *editor);

private:
    QAction *makeAction(const QString &id, const QString &text, const QString &icon, const QKeySequence &shortcut, std::function<void()> handler);
    void buildMenusAndToolbar(QMenuBar *menuBar, QAction *menuBefore, QMenu *viewMenu);
    void refreshIcons();
    void applyThemeToAllEditors();

    // Onde a ação deve agir: na prévia (se está com o foco) ou no editor
    bool inPreview() const;
    ScintillaNext *editor() const;
    void previewMergeFormat(const std::function<bool(const QTextCharFormat &)> &isOn, const std::function<void(QTextCharFormat &, bool)> &apply);
    // Executa um comando de tabela: na prévia, leva o cursor do editor à célula correspondente
    void tableCommand(const std::function<void(ScintillaNext *)> &command);

    MainWindow *window;
    MarkdownPreviewDock *preview;
    QToolBar *toolbar = nullptr;
    QHash<QAction *, QString> iconNames;
};

#endif // FORMATCONTROLLER_H
