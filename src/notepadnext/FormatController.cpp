/*
 * NotepadNext - recursos próprios deste fork.
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#include "FormatController.h"

#include "EditorManager.h"
#include "ImageSupport.h"
#include "MainWindow.h"
#include "MarkdownCommands.h"
#include "MarkdownPreviewDock.h"
#include "NotepadNextApplication.h"
#include "ScintillaNext.h"
#include "ThemeManager.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

using namespace MarkdownCommands;

FormatController::FormatController(MainWindow *window, MarkdownPreviewDock *preview, QMenuBar *menuBar, QAction *menuBefore, QMenu *viewMenu)
    : QObject(window), window(window), preview(preview)
{
    buildMenusAndToolbar(menuBar, menuBefore, viewMenu);

    // Arrastar imagens para o editor de Markdown
    ScintillaNext::canDropImage = [](ScintillaNext *e, const QMimeData *mime) {
        return isMarkdown(e) && ImageSupport::canHandle(mime);
    };
    ScintillaNext::dropImage = [](ScintillaNext *e, const QMimeData *mime, int position) {
        e->beginUndoAction();
        const bool ok = ImageSupport::insertFromMime(e, mime, position);
        e->endUndoAction();
        return ok;
    };

    // Tema: aplica nos editores novos e sempre que a linguagem muda
    ThemeManager *themes = ThemeManager::instance();
    NotepadNextApplication *app = qobject_cast<NotepadNextApplication *>(qApp);
    if (app) {
        connect(app->getEditorManager(), &EditorManager::editorCreated, this, [themes](ScintillaNext *e) {
            connect(e, &ScintillaNext::lexerChanged, e, [themes, e]() { themes->applyToEditor(e); }, Qt::QueuedConnection);
            QTimer::singleShot(0, e, [themes, e]() { themes->applyToEditor(e); });
        });
    }
    connect(themes, &ThemeManager::themeChanged, this, [this]() {
        refreshIcons();
        applyThemeToAllEditors();
    });
    if (themes->currentId() != QLatin1String("sistema"))
        QTimer::singleShot(0, this, [themes]() { themes->setTheme(themes->currentId()); });
}

bool FormatController::isMarkdown(ScintillaNext *editor)
{
    if (!editor) return false;
    if (editor->languageName.compare(QLatin1String("markdown"), Qt::CaseInsensitive) == 0) return true;
    const QString suffix = editor->getFileInfo().suffix().toLower();
    return suffix == QLatin1String("md") || suffix == QLatin1String("markdown") || suffix == QLatin1String("mdown");
}

bool FormatController::inPreview() const
{
    return preview && preview->previewHasFocus() && !preview->browser()->isReadOnly();
}

ScintillaNext *FormatController::editor() const
{
    return window->currentEditor();
}

QAction *FormatController::makeAction(const QString &id, const QString &text, const QString &icon, const QKeySequence &shortcut, std::function<void()> handler)
{
    QAction *a = new QAction(text, window);
    a->setObjectName(QStringLiteral("actionFormat_") + id);
    if (!shortcut.isEmpty()) {
        a->setShortcut(shortcut);
        a->setToolTip(QStringLiteral("%1 (%2)").arg(QString(text).remove(QLatin1Char('&')).remove(QStringLiteral("…")), shortcut.toString(QKeySequence::NativeText)));
    }
    if (!icon.isEmpty()) {
        iconNames.insert(a, icon);
        a->setIcon(ThemeManager::instance()->icon(icon));
    }
    connect(a, &QAction::triggered, this, [this, handler]() {
        if (!editor()) return;
        handler();
        if (preview) preview->syncToSource();
    });
    window->addAction(a);
    return a;
}

void FormatController::previewMergeFormat(const std::function<bool(const QTextCharFormat &)> &isOn, const std::function<void(QTextCharFormat &, bool)> &apply)
{
    QTextCursor c = preview->browser()->textCursor();
    if (!c.hasSelection())
        c.select(QTextCursor::WordUnderCursor);
    const bool on = isOn(c.charFormat());
    QTextCharFormat f;
    apply(f, !on);
    c.mergeCharFormat(f);
    preview->browser()->mergeCurrentCharFormat(f);
}

void FormatController::tableCommand(const std::function<void(ScintillaNext *)> &command)
{
    ScintillaNext *e = editor();
    if (inPreview()) {
        preview->syncToSource();
        int headerLine, row, col;
        if (preview->sourceTableCell(&headerLine, &row, &col))
            selectTableCell(e, headerLine, row, col);
        else {
            const int line = preview->sourceLineAtCursor();
            if (line >= 0) e->setEmptySelection(e->lineEndPosition(line));
        }
    }
    command(e);
}

void FormatController::buildMenusAndToolbar(QMenuBar *menuBar, QAction *menuBefore, QMenu *viewMenu)
{
    QMenu *menu = new QMenu(tr("For&mat"), window);
    menu->setObjectName(QStringLiteral("menuFormatting"));
    menuBar->insertMenu(menuBefore, menu);

    // --- Texto ---
    auto charToggle = [this](const QString &open, const QString &close,
                             std::function<bool(const QTextCharFormat &)> isOn,
                             std::function<void(QTextCharFormat &, bool)> apply) {
        return [this, open, close, isOn, apply]() {
            if (inPreview()) previewMergeFormat(isOn, apply);
            else toggleWrap(editor(), open, close);
        };
    };

    QAction *bold = makeAction(QStringLiteral("Bold"), tr("&Bold"), QStringLiteral("bold"), QKeySequence(Qt::CTRL | Qt::Key_B),
        charToggle(QStringLiteral("**"), QStringLiteral("**"),
                   [](const QTextCharFormat &f) { return f.fontWeight() >= QFont::DemiBold; },
                   [](QTextCharFormat &f, bool on) { f.setFontWeight(on ? QFont::Bold : QFont::Normal); }));
    QAction *italic = makeAction(QStringLiteral("Italic"), tr("&Italic"), QStringLiteral("italic"), QKeySequence(Qt::CTRL | Qt::Key_I),
        charToggle(QStringLiteral("*"), QStringLiteral("*"),
                   [](const QTextCharFormat &f) { return f.fontItalic(); },
                   [](QTextCharFormat &f, bool on) { f.setFontItalic(on); }));
    QAction *underline = makeAction(QStringLiteral("Underline"), tr("&Underline"), QStringLiteral("underline"), QKeySequence(Qt::CTRL | Qt::Key_U),
        charToggle(QStringLiteral("<u>"), QStringLiteral("</u>"),
                   [](const QTextCharFormat &f) { return f.fontUnderline() && !f.isAnchor(); },
                   [](QTextCharFormat &f, bool on) { f.setFontUnderline(on); }));
    QAction *strike = makeAction(QStringLiteral("Strike"), tr("&Strikethrough"), QStringLiteral("strike"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X),
        charToggle(QStringLiteral("~~"), QStringLiteral("~~"),
                   [](const QTextCharFormat &f) { return f.fontStrikeOut(); },
                   [](QTextCharFormat &f, bool on) { f.setFontStrikeOut(on); }));
    QAction *sup = makeAction(QStringLiteral("Superscript"), tr("Superscript"), QStringLiteral("superscript"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Period),
        charToggle(QStringLiteral("<sup>"), QStringLiteral("</sup>"),
                   [](const QTextCharFormat &f) { return f.verticalAlignment() == QTextCharFormat::AlignSuperScript; },
                   [](QTextCharFormat &f, bool on) { f.setVerticalAlignment(on ? QTextCharFormat::AlignSuperScript : QTextCharFormat::AlignNormal); }));
    QAction *sub = makeAction(QStringLiteral("Subscript"), tr("Subscript"), QStringLiteral("subscript"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Comma),
        charToggle(QStringLiteral("<sub>"), QStringLiteral("</sub>"),
                   [](const QTextCharFormat &f) { return f.verticalAlignment() == QTextCharFormat::AlignSubScript; },
                   [](QTextCharFormat &f, bool on) { f.setVerticalAlignment(on ? QTextCharFormat::AlignSubScript : QTextCharFormat::AlignNormal); }));
    menu->addActions({bold, italic, underline, strike, sup, sub});

    // --- Títulos ---
    QMenu *headings = menu->addMenu(tr("&Heading"));
    headings->setIcon(ThemeManager::instance()->icon(QStringLiteral("heading")));
    QList<QAction *> headingActions;
    for (int level = 0; level <= 3; ++level) {
        const QString text = level == 0 ? tr("Normal text") : tr("Heading %1").arg(level);
        QAction *h = makeAction(QStringLiteral("Heading%1").arg(level), text, QString(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key(Qt::Key_0 + level)), [this, level]() {
            if (inPreview()) {
                QTextCursor c = preview->browser()->textCursor();
                QTextBlockFormat bf = c.blockFormat();
                bf.setHeadingLevel(level);
                c.setBlockFormat(bf);
            }
            else setHeading(editor(), level);
        });
        headings->addAction(h);
        headingActions << h;
    }
    menu->addSeparator();

    // --- Alinhamento ---
    auto alignAction = [this](const QString &id, const QString &text, const QString &icon, const QString &align, Qt::Alignment qa) {
        return makeAction(id, text, icon, QKeySequence(), [this, align, qa]() {
            if (inPreview()) {
                QTextCursor c = preview->browser()->textCursor();
                QTextBlockFormat bf = c.blockFormat();
                bf.setAlignment(qa);
                c.setBlockFormat(bf);
            }
            else setAlignment(editor(), align);
        });
    };
    QAction *alignLeft = alignAction(QStringLiteral("AlignLeft"), tr("Align left"), QStringLiteral("align-left"), QStringLiteral("left"), Qt::AlignLeft);
    QAction *alignCenter = alignAction(QStringLiteral("AlignCenter"), tr("Center"), QStringLiteral("align-center"), QStringLiteral("center"), Qt::AlignHCenter);
    QAction *alignRight = alignAction(QStringLiteral("AlignRight"), tr("Align right"), QStringLiteral("align-right"), QStringLiteral("right"), Qt::AlignRight);
    menu->addActions({alignLeft, alignCenter, alignRight});
    menu->addSeparator();

    // --- Listas ---
    auto previewList = [this](QTextListFormat::Style style, bool task) {
        QTextCursor c = preview->browser()->textCursor();
        QTextList *list = c.currentList();
        QTextBlockFormat bf = c.blockFormat();
        const bool hasTask = bf.marker() != QTextBlockFormat::MarkerType::NoMarker;
        if (list && list->format().style() == style && hasTask == task) {
            // Já é desse tipo: volta a ser parágrafo
            list->remove(c.block());
            bf = c.blockFormat();
            bf.setIndent(0);
            bf.setMarker(QTextBlockFormat::MarkerType::NoMarker);
            c.setBlockFormat(bf);
            return;
        }
        if (list) {
            QTextListFormat lf = list->format();
            lf.setStyle(style);
            list->setFormat(lf);
        }
        else {
            c.createList(style);
        }
        bf = c.blockFormat();
        bf.setMarker(task ? QTextBlockFormat::MarkerType::Unchecked : QTextBlockFormat::MarkerType::NoMarker);
        c.setBlockFormat(bf);
    };
    QAction *bullet = makeAction(QStringLiteral("ListBullet"), tr("Bulleted list"), QStringLiteral("list-bullet"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_8), [this, previewList]() {
        if (inPreview()) previewList(QTextListFormat::ListDisc, false); else toggleList(editor(), ListKind::Bullet);
    });
    QAction *numbered = makeAction(QStringLiteral("ListNumber"), tr("Numbered list"), QStringLiteral("list-number"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_7), [this, previewList]() {
        if (inPreview()) previewList(QTextListFormat::ListDecimal, false); else toggleList(editor(), ListKind::Numbered);
    });
    QAction *task = makeAction(QStringLiteral("ListTask"), tr("Task list (checkbox)"), QStringLiteral("list-task"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_9), [this, previewList]() {
        if (inPreview()) previewList(QTextListFormat::ListDisc, true); else toggleList(editor(), ListKind::Task);
    });
    QAction *done = makeAction(QStringLiteral("TaskDone"), tr("Check/uncheck task"), QStringLiteral("task-done"), QKeySequence(Qt::CTRL | Qt::Key_Return), [this]() {
        if (inPreview()) {
            QTextCursor c = preview->browser()->textCursor();
            QTextBlockFormat bf = c.blockFormat();
            bf.setMarker(bf.marker() == QTextBlockFormat::MarkerType::Checked ? QTextBlockFormat::MarkerType::Unchecked : QTextBlockFormat::MarkerType::Checked);
            c.setBlockFormat(bf);
        }
        else toggleTaskDone(editor());
    });
    auto previewIndent = [this](int delta) {
        QTextCursor c = preview->browser()->textCursor();
        if (QTextList *list = c.currentList()) {
            QTextListFormat lf = list->format();
            lf.setIndent(qMax(1, lf.indent() + delta));
            c.createList(lf);
        }
    };
    QAction *indent = makeAction(QStringLiteral("Indent"), tr("Increase indent"), QStringLiteral("indent"), QKeySequence(), [this, previewIndent]() {
        if (inPreview()) previewIndent(1); else indentLines(editor(), true);
    });
    QAction *outdent = makeAction(QStringLiteral("Outdent"), tr("Decrease indent"), QStringLiteral("outdent"), QKeySequence(), [this, previewIndent]() {
        if (inPreview()) previewIndent(-1); else indentLines(editor(), false);
    });
    menu->addActions({bullet, numbered, task, done, indent, outdent});
    menu->addSeparator();

    // --- Tabelas ---
    QMenu *tableMenu = menu->addMenu(tr("Ta&ble"));
    tableMenu->setIcon(ThemeManager::instance()->icon(QStringLiteral("table")));
    iconNames.insert(tableMenu->menuAction(), QStringLiteral("table"));
    QAction *tInsert = makeAction(QStringLiteral("TableInsert"), tr("Insert table…"), QStringLiteral("table-insert"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T),
                                  [this]() { tableCommand([this](ScintillaNext *e) { insertTable(e, window); }); });
    QAction *rowAbove = makeAction(QStringLiteral("TableRowAbove"), tr("Insert row above"), QStringLiteral("table-row-above"), QKeySequence(),
                                   [this]() { tableCommand([](ScintillaNext *e) { insertRow(e, false); }); });
    QAction *rowBelow = makeAction(QStringLiteral("TableRowBelow"), tr("Insert row below"), QStringLiteral("table-row-below"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Return),
                                   [this]() { tableCommand([](ScintillaNext *e) { insertRow(e, true); }); });
    QAction *rowDelete = makeAction(QStringLiteral("TableRowDelete"), tr("Delete row"), QStringLiteral("table-row-delete"), QKeySequence(),
                                    [this]() { tableCommand([](ScintillaNext *e) { deleteRow(e); }); });
    QAction *colLeft = makeAction(QStringLiteral("TableColLeft"), tr("Insert column left"), QStringLiteral("table-col-left"), QKeySequence(),
                                  [this]() { tableCommand([](ScintillaNext *e) { insertColumn(e, false); }); });
    QAction *colRight = makeAction(QStringLiteral("TableColRight"), tr("Insert column right"), QStringLiteral("table-col-right"), QKeySequence(),
                                   [this]() { tableCommand([](ScintillaNext *e) { insertColumn(e, true); }); });
    QAction *colDelete = makeAction(QStringLiteral("TableColDelete"), tr("Delete column"), QStringLiteral("table-col-delete"), QKeySequence(),
                                    [this]() { tableCommand([](ScintillaNext *e) { deleteColumn(e); }); });
    QAction *tShade = makeAction(QStringLiteral("TableShade"), tr("Cell shading…"), QStringLiteral("table-shade"), QKeySequence(),
                                 [this]() { tableCommand([this](ScintillaNext *e) { shadeCells(e, window); }); });
    QAction *tProps = makeAction(QStringLiteral("TableProps"), tr("Table name, lines and colors…"), QStringLiteral("table-border"), QKeySequence(),
                                 [this]() { tableCommand([this](ScintillaNext *e) { tableProperties(e, window); }); });
    QAction *tAlign = makeAction(QStringLiteral("TableAlign"), tr("Align table columns"), QStringLiteral("table-align"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_L),
                                 [this]() { tableCommand([](ScintillaNext *e) { alignTableColumns(e); }); });
    tAlign->setToolTip(tr("Lines up the table columns in the editor so the text is easier to read (%1)").arg(tAlign->shortcut().toString(QKeySequence::NativeText)));
    QAction *tDelete = makeAction(QStringLiteral("TableDelete"), tr("Delete table"), QStringLiteral("table-delete"), QKeySequence(),
                                  [this]() { tableCommand([](ScintillaNext *e) { deleteTable(e); }); });
    QAction *formulas = makeAction(QStringLiteral("FormulaHelp"), tr("Formulas: how to use…"), QStringLiteral("formula"), QKeySequence(),
                                   [this]() { showFormulaHelp(window); });
    tableMenu->addAction(tInsert);
    tableMenu->addAction(tAlign);
    tableMenu->addSeparator();
    tableMenu->addActions({rowAbove, rowBelow, rowDelete});
    tableMenu->addSeparator();
    tableMenu->addActions({colLeft, colRight, colDelete});
    tableMenu->addSeparator();
    tableMenu->addActions({tShade, tProps});
    tableMenu->addSeparator();
    tableMenu->addAction(tDelete);
    tableMenu->addSeparator();
    tableMenu->addAction(formulas);

    // --- Imagens ---
    QAction *image = makeAction(QStringLiteral("ImageInsert"), tr("Insert image…"), QStringLiteral("image"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), [this]() {
        ScintillaNext *e = editor();
        if (inPreview()) {
            const int line = preview->sourceLineAtCursor();
            if (line >= 0) {
                e->setEmptySelection(e->lineEndPosition(line));
                insertTextAt(e, e->currentPos(), QStringLiteral("\n\n"));
                e->setEmptySelection(e->lineEndPosition(line + 2));
            }
        }
        ImageSupport::insertFromFile(e, window);
    });
    QAction *imageProps = makeAction(QStringLiteral("ImageProps"), tr("Image size and position…"), QStringLiteral("image-props"), QKeySequence(), [this]() {
        ScintillaNext *e = editor();
        if (inPreview()) {
            const int line = preview->sourceLineAtCursor();
            if (line >= 0) e->setEmptySelection(e->positionFromLine(line));
        }
        ImageSupport::editProperties(e, window);
    });
    menu->addSeparator();
    menu->addActions({image, imageProps});

    // --- Barra de formatação (embaixo da barra principal) ---
    window->addToolBarBreak();
    toolbar = new QToolBar(tr("Formatting"), window);
    toolbar->setObjectName(QStringLiteral("formatToolBar"));
    toolbar->setIconSize(QSize(18, 18));
    toolbar->setMovable(false);
    window->addToolBar(Qt::TopToolBarArea, toolbar);
    toolbar->addActions({bold, italic, underline, strike, sup, sub});

    QToolButton *headingButton = new QToolButton(toolbar);
    headingButton->setMenu(headings);
    headingButton->setPopupMode(QToolButton::InstantPopup);
    headingButton->setIcon(ThemeManager::instance()->icon(QStringLiteral("heading")));
    headingButton->setToolTip(tr("Heading"));
    headingButton->setProperty("appIcon", QStringLiteral("heading"));
    toolbar->addWidget(headingButton);
    toolbar->addSeparator();
    toolbar->addActions({alignLeft, alignCenter, alignRight});
    toolbar->addSeparator();
    toolbar->addActions({bullet, numbered, task, done, outdent, indent});
    toolbar->addSeparator();
    toolbar->addAction(tInsert);

    QToolButton *tableButton = new QToolButton(toolbar);
    tableButton->setMenu(tableMenu);
    tableButton->setPopupMode(QToolButton::InstantPopup);
    tableButton->setIcon(ThemeManager::instance()->icon(QStringLiteral("table")));
    tableButton->setToolTip(tr("Table rows, columns and appearance"));
    tableButton->setProperty("appIcon", QStringLiteral("table"));
    toolbar->addWidget(tableButton);
    toolbar->addAction(tAlign);
    toolbar->addActions({tShade, tProps});
    toolbar->addSeparator();
    toolbar->addActions({image, imageProps});

    QAction *toolbarToggle = toolbar->toggleViewAction();
    toolbarToggle->setText(tr("Formatting toolbar"));
    viewMenu->addAction(toolbarToggle);

    // --- Tema e fonte da prévia (menu Visualizar) ---
    QMenu *themeMenu = viewMenu->addMenu(tr("Theme"));
    QActionGroup *group = new QActionGroup(themeMenu);
    for (const AppTheme &t : ThemeManager::instance()->themes()) {
        QAction *a = themeMenu->addAction(t.name);
        a->setCheckable(true);
        a->setChecked(t.id == ThemeManager::instance()->currentId());
        group->addAction(a);
        const QString id = t.id;
        connect(a, &QAction::triggered, this, [id]() { ThemeManager::instance()->setTheme(id); });
    }
    QAction *fonts = viewMenu->addAction(tr("Preview font…"));
    connect(fonts, &QAction::triggered, preview, &MarkdownPreviewDock::chooseFonts);
}

void FormatController::refreshIcons()
{
    ThemeManager *tm = ThemeManager::instance();
    for (auto it = iconNames.constBegin(); it != iconNames.constEnd(); ++it)
        it.key()->setIcon(tm->icon(it.value()));
    if (toolbar) {
        for (QToolButton *b : toolbar->findChildren<QToolButton *>()) {
            const QString name = b->property("appIcon").toString();
            if (!name.isEmpty()) b->setIcon(tm->icon(name));
        }
    }
}

void FormatController::applyThemeToAllEditors()
{
    NotepadNextApplication *app = qobject_cast<NotepadNextApplication *>(qApp);
    ThemeManager *tm = ThemeManager::instance();
    for (QWidget *w : QApplication::allWidgets()) {
        ScintillaNext *e = qobject_cast<ScintillaNext *>(w);
        if (!e) continue;
        if (tm->currentId() == QLatin1String("sistema")) {
            // Volta às cores originais da linguagem
            if (app && !e->languageName.isEmpty()) app->setEditorLanguage(e, e->languageName);
        }
        else {
            tm->applyToEditor(e);
        }
    }
}
