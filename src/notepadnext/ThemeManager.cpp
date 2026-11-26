/*
 * NotepadNext - recursos próprios deste fork.
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#include "ThemeManager.h"
#include "ScintillaNext.h"

#include "Scintilla.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

static sptr_t bgr(const QColor &c)
{
    return c.red() | (c.green() << 8) | (c.blue() << 16);
}

static sptr_t bgra(const QColor &c)
{
    return sptr_t(c.red() | (c.green() << 8) | (c.blue() << 16)) | (sptr_t(c.alpha()) << 24);
}

static QColor fromBgr(sptr_t v)
{
    return QColor(int(v & 0xFF), int((v >> 8) & 0xFF), int((v >> 16) & 0xFF));
}

ThemeManager *ThemeManager::instance()
{
    static ThemeManager *manager = new ThemeManager(qApp);
    return manager;
}

ThemeManager::ThemeManager(QObject *parent) : QObject(parent)
{
    originalPalette = QApplication::palette();
    originalStyle = QApplication::style() ? QApplication::style()->name() : QString();

    auto make = [](const char *id, const QString &name, bool dark, const char *window, const char *base, const char *text,
                   const char *muted, const char *accent, const char *border, const char *selection, const char *code, const char *header) {
        AppTheme t;
        t.id = QLatin1String(id);
        t.name = name;
        t.dark = dark;
        t.window = QColor(QLatin1String(window));
        t.base = QColor(QLatin1String(base));
        t.text = QColor(QLatin1String(text));
        t.muted = QColor(QLatin1String(muted));
        t.accent = QColor(QLatin1String(accent));
        t.border = QColor(QLatin1String(border));
        t.selection = QColor(QLatin1String(selection));
        t.codeBackground = QColor(QLatin1String(code));
        t.tableHeader = QColor(QLatin1String(header));
        return t;
    };

    themeList << make("sistema", tr("Sistema (padrão do macOS)"), false, "", "", "", "", "", "", "", "", "");
    themeList << make("claro", tr("Claro"), false, "#F5F4F7", "#FFFFFF", "#26262C", "#8A8894", "#9B6BE0", "#D9D6E0", "#E6DCF7", "#F3F1F6", "#F1ECFA");
    themeList << make("papel", tr("Papel"), false, "#EFEAE0", "#FBF8F2", "#3A342C", "#8F8574", "#A0612F", "#DDD4C4", "#EADFCB", "#F2ECE1", "#F0E8DA");
    themeList << make("grafite", tr("Grafite (escuro)"), true, "#2A2A2F", "#1F1F23", "#E4E4E9", "#9A99A3", "#B590F2", "#3E3D45", "#45385E", "#2A2930", "#302A3D");
    themeList << make("noite", tr("Noite (escuro)"), true, "#1C2030", "#151925", "#DCE1EC", "#8A93A8", "#7AA2F7", "#2E3548", "#2C3B5E", "#1F2535", "#222C45");

    QSettings settings;
    currentThemeId = settings.value(QStringLiteral("NotepadNext/theme"), QStringLiteral("sistema")).toString();
}

AppTheme ThemeManager::current() const
{
    for (const AppTheme &t : themeList)
        if (t.id == currentThemeId)
            return t;
    return themeList.first();
}

void ThemeManager::setTheme(const QString &id)
{
    currentThemeId = id;
    QSettings().setValue(QStringLiteral("NotepadNext/theme"), id);

    const AppTheme t = current();
    if (t.id == QLatin1String("sistema")) {
        if (!originalStyle.isEmpty())
            QApplication::setStyle(QStyleFactory::create(originalStyle));
        QApplication::setPalette(originalPalette);
    }
    else {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QPalette p;
        p.setColor(QPalette::Window, t.window);
        p.setColor(QPalette::WindowText, t.text);
        p.setColor(QPalette::Base, t.base);
        p.setColor(QPalette::AlternateBase, t.window);
        p.setColor(QPalette::Text, t.text);
        p.setColor(QPalette::Button, t.window);
        p.setColor(QPalette::ButtonText, t.text);
        p.setColor(QPalette::ToolTipBase, t.base);
        p.setColor(QPalette::ToolTipText, t.text);
        p.setColor(QPalette::PlaceholderText, t.muted);
        p.setColor(QPalette::Highlight, t.accent);
        p.setColor(QPalette::HighlightedText, t.dark ? t.base : QColor(Qt::white));
        p.setColor(QPalette::Link, t.accent);
        p.setColor(QPalette::Mid, t.border);
        p.setColor(QPalette::Midlight, t.border.lighter(110));
        p.setColor(QPalette::Dark, t.border.darker(130));
        p.setColor(QPalette::Light, t.dark ? t.window.lighter(130) : QColor(Qt::white));
        p.setColor(QPalette::Shadow, t.dark ? QColor(Qt::black) : t.border.darker(150));
        for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            p.setColor(QPalette::Disabled, role, t.muted);
        QApplication::setPalette(p);
    }

    // Janelas com folha de estilo guardam a paleta antiga: reaplica a folha
    // (com as cores das abas no tema) para atualizar tudo
    QString css;
    if (t.id != QLatin1String("sistema")) {
        const QColor tab = t.dark ? t.window.darker(115) : t.window.darker(106);
        css = QStringLiteral(
            "\n/* notepadnext-tema */\n"
            "ads--CDockWidgetTab { background: %1; border-left: 1px solid %2; border-top: 2px solid %2; border-right: 1px solid %2; }\n"
            "ads--CDockWidgetTab[activeTab=\"true\"] { background: %3; border-top: 3px solid %4; }\n"
            "ads--CDockWidgetTab[focused=\"true\"] { background: %3; border-top: 3px solid %4; }\n"
            "ads--CDockWidgetTab:hover[activeTab=\"false\"] { background: %3; }\n"
            "ads--CDockWidgetTab ads--CElidingLabel { color: %5; }\n"
            "ads--CDockWidgetTab[activeTab=\"false\"] QLabel { color: %6; }\n"
            "ads--CDockAreaTitleBar { border-bottom: 1px solid %2; }\n"
            "QStatusBar { border-top: 1px solid %2; }\n"
            "QToolBar { border: none; }\n")
            .arg(tab.name(), t.border.name(), t.base.name(), t.accent.name(), t.text.name(), t.muted.name());
    }
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (!w->property("baseStyleSheet").isValid())
            w->setProperty("baseStyleSheet", w->styleSheet());
        const QString base = w->property("baseStyleSheet").toString();
        if (base.isEmpty() && css.isEmpty()) continue;
        w->setStyleSheet(base + css);
    }
    for (QWidget *w : QApplication::allWidgets()) {
        // Widgets com paleta própria (ex.: a prévia) passam a seguir a do app
        if (w->testAttribute(Qt::WA_SetPalette) && w->inherits("QTextBrowser"))
            w->setPalette(QApplication::palette());
        w->update();
    }

    emit themeChanged();
    emit previewStyleChanged();
}

void ThemeManager::applyToEditor(ScintillaNext *editor)
{
    const AppTheme t = current();
    if (!editor || t.id == QLatin1String("sistema"))
        return;

    const double bgLight = t.base.lightnessF();
    for (int style = 0; style <= STYLE_MAX; ++style) {
        if (style == STYLE_LINENUMBER) continue;
        editor->styleSetBack(style, bgr(t.base));

        // Mantém as cores do realce de sintaxe, mas garante contraste com o fundo
        QColor fore = fromBgr(editor->styleFore(style));
        float h, s, l;
        fore.getHslF(&h, &s, &l);
        if (t.dark && l < 0.55f) {
            fore = QColor::fromHslF(h, qMin(s, 0.75f), qMax(0.62f, 1.0f - l));
        }
        else if (!t.dark && l > 0.62f && bgLight > 0.8) {
            fore = QColor::fromHslF(h, s, qMin(0.42f, 1.0f - l));
        }
        if (fore == QColor(Qt::black) || (t.dark && fore == QColor(Qt::white)))
            fore = t.text;
        editor->styleSetFore(style, bgr(fore));
    }
    editor->styleSetFore(STYLE_DEFAULT, bgr(t.text));
    editor->styleSetFore(0, bgr(t.text));
    editor->styleSetBack(STYLE_LINENUMBER, bgr(t.window));
    editor->styleSetFore(STYLE_LINENUMBER, bgr(t.muted));
    editor->styleSetFore(STYLE_INDENTGUIDE, bgr(t.border));

    editor->setCaretFore(bgr(t.text));
    editor->setSelBack(true, bgr(t.selection));
    editor->setElementColour(SC_ELEMENT_SELECTION_BACK, bgra(t.selection));
    QColor caretLine = t.dark ? t.base.lighter(125) : t.window;
    editor->setElementColour(SC_ELEMENT_CARET_LINE_BACK, bgra(caretLine));
    editor->setFoldMarginColour(true, bgr(t.window));
    editor->setFoldMarginHiColour(true, bgr(t.window));
    editor->setWhitespaceFore(true, bgr(t.border));
    for (int m = 0; m < 5; ++m)
        editor->setMarginBackN(m, bgr(t.window));
}

PreviewStyle ThemeManager::previewStyle() const
{
    PreviewStyle s;
    const AppTheme t = current();
    if (t.id == QLatin1String("sistema")) {
        const QPalette p = QApplication::palette();
        s.text = p.color(QPalette::Text);
        s.muted = p.color(QPalette::PlaceholderText);
        const bool dark = p.color(QPalette::Base).lightnessF() < 0.5;
        if (dark) {
            s.codeBackground = QColor(QStringLiteral("#2A2930"));
            s.tableBorder = QColor(QStringLiteral("#4A4852"));
            s.tableHeader = QColor(QStringLiteral("#302A3D"));
            s.link = QColor(QStringLiteral("#B590F2"));
        }
    }
    else {
        s.text = t.text;
        s.muted = t.muted;
        s.link = t.accent;
        s.codeBackground = t.codeBackground;
        s.tableBorder = t.border;
        s.tableHeader = t.tableHeader;
        if (t.dark) s.error = QColor(QStringLiteral("#FF6B6B"));
    }

    QSettings settings;
    s.bodyFont = settings.value(QStringLiteral("NotepadNext/preview/bodyFont")).toString();
    s.bodySize = settings.value(QStringLiteral("NotepadNext/preview/bodySize"), 14).toInt();
    s.headingFont = settings.value(QStringLiteral("NotepadNext/preview/headingFont")).toString();
    s.headingSize = settings.value(QStringLiteral("NotepadNext/preview/headingSize"), 24).toInt();
    s.codeFont = settings.value(QStringLiteral("NotepadNext/preview/codeFont")).toString();
    s.codeSize = settings.value(QStringLiteral("NotepadNext/preview/codeSize"), 12).toInt();
    if (s.codeFont.isEmpty()) {
#ifdef Q_OS_MACOS
        s.codeFont = QStringLiteral("Menlo");
#else
        s.codeFont = QStringLiteral("monospace");
#endif
    }
    return s;
}

void ThemeManager::setPreviewFonts(const QString &body, int bodySize, const QString &heading, int headingSize, const QString &code, int codeSize)
{
    QSettings settings;
    settings.setValue(QStringLiteral("NotepadNext/preview/bodyFont"), body);
    settings.setValue(QStringLiteral("NotepadNext/preview/bodySize"), bodySize);
    settings.setValue(QStringLiteral("NotepadNext/preview/headingFont"), heading);
    settings.setValue(QStringLiteral("NotepadNext/preview/headingSize"), headingSize);
    settings.setValue(QStringLiteral("NotepadNext/preview/codeFont"), code);
    settings.setValue(QStringLiteral("NotepadNext/preview/codeSize"), codeSize);
    emit previewStyleChanged();
}

// ---------------------------------------------------------------------------
// Ícones simples e monocromáticos, no espírito "menos, porém melhor"
// ---------------------------------------------------------------------------

static void drawLines(QPainter &p, const QList<QPair<qreal, qreal>> &spans, qreal top = 14, qreal step = 12)
{
    qreal y = top;
    for (const auto &s : spans) {
        p.drawLine(QPointF(s.first, y), QPointF(s.second, y));
        y += step;
    }
}

static void drawGrid(QPainter &p, const QColor &color, const QRectF &r, int rows, int cols)
{
    p.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, 3, 3);
    for (int i = 1; i < rows; ++i) {
        const qreal y = r.top() + r.height() * i / rows;
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }
    for (int i = 1; i < cols; ++i) {
        const qreal x = r.left() + r.width() * i / cols;
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }
}

static void drawBadge(QPainter &p, const QColor &accent, const QPointF &c, const QString &sign)
{
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawEllipse(c, 10, 10);
    p.setPen(QPen(Qt::white, 3.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(c.x() - 5, c.y()), QPointF(c.x() + 5, c.y()));
    if (sign == QLatin1String("+"))
        p.drawLine(QPointF(c.x(), c.y() - 5), QPointF(c.x(), c.y() + 5));
    if (sign == QLatin1String("x")) {
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawEllipse(c, 10, 10);
        p.setPen(QPen(Qt::white, 3.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(c.x() - 4, c.y() - 4), QPointF(c.x() + 4, c.y() + 4));
        p.drawLine(QPointF(c.x() + 4, c.y() - 4), QPointF(c.x() - 4, c.y() + 4));
    }
}

QIcon ThemeManager::icon(const QString &name) const
{
    const AppTheme t = current();
    const QColor fg = t.id == QLatin1String("sistema") ? QApplication::palette().color(QPalette::WindowText) : t.text;
    const QColor accent = t.id == QLatin1String("sistema") ? QColor(0x9B, 0x6B, 0xE0) : t.accent;

    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QPen line(fg, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(line);

    auto text = [&](const QString &s, int px, bool bold, bool italic, const QRectF &r, int flags = Qt::AlignCenter, const QString &family = QString()) {
        QFont f(family.isEmpty() ? QApplication::font().family() : family);
        f.setPixelSize(px);
        f.setBold(bold);
        f.setItalic(italic);
        p.setFont(f);
        p.drawText(r, flags, s);
    };

    if (name == QLatin1String("bold")) text(QStringLiteral("B"), 44, true, false, pm.rect());
    else if (name == QLatin1String("italic")) text(QStringLiteral("I"), 46, false, true, pm.rect(), Qt::AlignCenter, QStringLiteral("Georgia"));
    else if (name == QLatin1String("underline")) { text(QStringLiteral("U"), 38, false, false, QRectF(0, -4, 64, 60)); p.drawLine(QPointF(16, 54), QPointF(48, 54)); }
    else if (name == QLatin1String("strike")) { text(QStringLiteral("S"), 42, false, false, pm.rect()); p.drawLine(QPointF(10, 33), QPointF(54, 33)); }
    else if (name == QLatin1String("superscript")) { text(QStringLiteral("x"), 36, false, false, QRectF(4, 14, 36, 44)); p.setPen(accent); text(QStringLiteral("2"), 24, true, false, QRectF(36, 2, 24, 28)); }
    else if (name == QLatin1String("subscript")) { text(QStringLiteral("x"), 36, false, false, QRectF(4, 6, 36, 44)); p.setPen(accent); text(QStringLiteral("2"), 24, true, false, QRectF(36, 34, 24, 28)); }
    else if (name == QLatin1String("heading")) text(QStringLiteral("H"), 42, true, false, pm.rect());
    else if (name == QLatin1String("align-left")) drawLines(p, {{10, 54}, {10, 40}, {10, 54}, {10, 34}});
    else if (name == QLatin1String("align-center")) drawLines(p, {{10, 54}, {18, 46}, {10, 54}, {22, 42}});
    else if (name == QLatin1String("align-right")) drawLines(p, {{10, 54}, {24, 54}, {10, 54}, {30, 54}});
    else if (name == QLatin1String("list-bullet") || name == QLatin1String("list-number") || name == QLatin1String("list-task")) {
        drawLines(p, {{26, 56}, {26, 56}, {26, 56}}, 16, 16);
        for (int i = 0; i < 3; ++i) {
            const qreal y = 16 + i * 16;
            if (name == QLatin1String("list-bullet")) {
                p.setBrush(fg);
                p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(13, y), 4, 4);
                p.setPen(line);
                p.setBrush(Qt::NoBrush);
            }
            else if (name == QLatin1String("list-number")) {
                text(QString::number(i + 1), 15, true, false, QRectF(4, y - 9, 16, 18));
            }
            else {
                p.setPen(QPen(fg, 2.5));
                p.drawRoundedRect(QRectF(6, y - 6, 12, 12), 2, 2);
                if (i == 0) {
                    p.setPen(QPen(accent, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    p.drawPolyline(QPolygonF({QPointF(8, y), QPointF(11.5, y + 3.5), QPointF(18, y - 5)}));
                }
                p.setPen(line);
            }
        }
    }
    else if (name == QLatin1String("task-done")) {
        p.setPen(QPen(fg, 4));
        p.drawRoundedRect(QRectF(12, 12, 40, 40), 7, 7);
        p.setPen(QPen(accent, 5.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(QPolygonF({QPointF(21, 32), QPointF(29, 40), QPointF(44, 23)}));
    }
    else if (name == QLatin1String("indent") || name == QLatin1String("outdent")) {
        drawLines(p, {{10, 54}, {30, 54}, {30, 54}, {10, 54}});
        QPainterPath arrow;
        if (name == QLatin1String("indent")) { arrow.moveTo(10, 22); arrow.lineTo(20, 31); arrow.lineTo(10, 40); }
        else { arrow.moveTo(20, 22); arrow.lineTo(10, 31); arrow.lineTo(20, 40); }
        p.setPen(QPen(accent, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(arrow);
    }
    else if (name.startsWith(QLatin1String("table"))) {
        drawGrid(p, fg, QRectF(6, 10, 52, 44), 3, 3);
        if (name == QLatin1String("table-insert")) drawBadge(p, accent, QPointF(50, 50), QStringLiteral("+"));
        else if (name == QLatin1String("table-row-below")) { p.fillRect(QRectF(8, 40, 48, 12), QColor(accent.red(), accent.green(), accent.blue(), 110)); drawBadge(p, accent, QPointF(50, 50), QStringLiteral("+")); }
        else if (name == QLatin1String("table-row-above")) { p.fillRect(QRectF(8, 12, 48, 12), QColor(accent.red(), accent.green(), accent.blue(), 110)); drawBadge(p, accent, QPointF(50, 14), QStringLiteral("+")); }
        else if (name == QLatin1String("table-col-right")) { p.fillRect(QRectF(41, 12, 15, 40), QColor(accent.red(), accent.green(), accent.blue(), 110)); drawBadge(p, accent, QPointF(50, 50), QStringLiteral("+")); }
        else if (name == QLatin1String("table-col-left")) { p.fillRect(QRectF(8, 12, 15, 40), QColor(accent.red(), accent.green(), accent.blue(), 110)); drawBadge(p, accent, QPointF(14, 50), QStringLiteral("+")); }
        else if (name == QLatin1String("table-row-delete")) { p.fillRect(QRectF(8, 26, 48, 12), QColor(accent.red(), accent.green(), accent.blue(), 110)); drawBadge(p, accent, QPointF(50, 50), QStringLiteral("-")); }
        else if (name == QLatin1String("table-col-delete")) { p.fillRect(QRectF(24, 12, 16, 40), QColor(accent.red(), accent.green(), accent.blue(), 110)); drawBadge(p, accent, QPointF(50, 50), QStringLiteral("-")); }
        else if (name == QLatin1String("table-delete")) drawBadge(p, accent, QPointF(50, 50), QStringLiteral("x"));
        else if (name == QLatin1String("table-shade")) p.fillRect(QRectF(23.5, 25, 17, 14), accent);
        else if (name == QLatin1String("table-border")) { p.setPen(QPen(accent, 6)); p.drawRoundedRect(QRectF(6, 10, 52, 44), 3, 3); }
    }
    else if (name == QLatin1String("image")) {
        p.drawRoundedRect(QRectF(8, 12, 48, 40), 5, 5);
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(42, 24), 5, 5);
        QPainterPath m;
        m.moveTo(12, 48); m.lineTo(26, 30); m.lineTo(36, 42); m.lineTo(42, 36); m.lineTo(52, 48); m.closeSubpath();
        p.setBrush(fg);
        p.drawPath(m);
    }
    else if (name == QLatin1String("image-props")) {
        p.drawRoundedRect(QRectF(8, 12, 40, 34), 5, 5);
        QPainterPath m;
        m.moveTo(12, 42); m.lineTo(23, 28); m.lineTo(32, 38); m.lineTo(44, 42); m.closeSubpath();
        p.setBrush(fg);
        p.setPen(Qt::NoPen);
        p.drawPath(m);
        p.setPen(QPen(accent, 4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(40, 56), QPointF(58, 56));
        p.drawLine(QPointF(54, 40), QPointF(54, 58));
    }
    else if (name == QLatin1String("formula")) {
        text(QStringLiteral("fx"), 34, false, true, pm.rect(), Qt::AlignCenter, QStringLiteral("Georgia"));
    }
    // --- Abas e barras do Grafo e do Meeting Assistant ---
    else if (name == QLatin1String("notes")) {
        p.drawRoundedRect(QRectF(12, 6, 40, 52), 6, 6);
        drawLines(p, {{20, 44}, {20, 44}, {20, 36}}, 22, 11);
        p.setPen(QPen(accent, 4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(20, 14), QPointF(32, 14));
    }
    else if (name == QLatin1String("graph")) {
        const QPointF a(16, 18), b(46, 14), c(32, 40), d(14, 50), e(52, 48);
        p.setPen(QPen(fg, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(a, c); p.drawLine(b, c); p.drawLine(c, d); p.drawLine(c, e); p.drawLine(a, b);
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        for (const QPointF &pt : {a, b, d, e}) p.drawEllipse(pt, 6, 6);
        p.setBrush(accent);
        p.drawEllipse(c, 8, 8);
    }
    else if (name == QLatin1String("meeting") || name == QLatin1String("voices")) {
        if (name == QLatin1String("meeting")) {
            p.drawRoundedRect(QRectF(24, 6, 16, 32), 8, 8);
            p.drawArc(QRectF(14, 18, 36, 30), 180 * 16, 180 * 16);
            p.drawLine(QPointF(32, 48), QPointF(32, 58));
            p.drawLine(QPointF(22, 58), QPointF(42, 58));
            p.setPen(QPen(accent, 4, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(29, 16), QPointF(35, 16));
        }
        else {
            p.drawEllipse(QPointF(26, 22), 10, 10);
            p.drawArc(QRectF(8, 36, 36, 30), 0, 180 * 16);
            p.setPen(QPen(accent, 4, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(QRectF(40, 14, 12, 18), -60 * 16, 120 * 16);
            p.drawArc(QRectF(38, 8, 20, 30), -60 * 16, 120 * 16);
        }
    }
    else if (name == QLatin1String("record")) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xE1, 0x55, 0x54));
        p.drawEllipse(QPointF(32, 32), 18, 18);
    }
    else if (name == QLatin1String("stop")) {
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        p.drawRoundedRect(QRectF(16, 16, 32, 32), 5, 5);
    }
    else if (name == QLatin1String("folder-open") || name == QLatin1String("vault")) {
        QPainterPath f;
        f.moveTo(8, 50); f.lineTo(8, 14); f.lineTo(24, 14); f.lineTo(30, 20); f.lineTo(54, 20); f.lineTo(54, 50); f.closeSubpath();
        p.drawPath(f);
        p.setPen(QPen(accent, 4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(14, 28), QPointF(48, 28));
    }
    else if (name == QLatin1String("audio-file")) {
        p.drawRoundedRect(QRectF(12, 6, 40, 52), 6, 6);
        p.setPen(QPen(accent, 4, Qt::SolidLine, Qt::RoundCap));
        const qreal hs[] = {6, 14, 22, 12, 18, 8};
        for (int i = 0; i < 6; ++i) {
            const qreal x = 20 + i * 5;
            p.drawLine(QPointF(x, 32 - hs[i] / 2), QPointF(x, 32 + hs[i] / 2));
        }
    }
    else if (name == QLatin1String("refresh") || name == QLatin1String("graph-spin")) {
        p.setPen(QPen(name == QLatin1String("graph-spin") ? accent : fg, 4.5, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(12, 12, 40, 40), 30 * 16, 290 * 16);
        QPainterPath head;
        head.moveTo(52, 12); head.lineTo(50, 28); head.lineTo(36, 22); head.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(name == QLatin1String("graph-spin") ? accent : fg);
        p.drawPath(head);
    }
    else if (name == QLatin1String("note-save")) {
        p.drawRoundedRect(QRectF(12, 6, 40, 52), 6, 6);
        drawLines(p, {{20, 44}, {20, 36}}, 18, 10);
        p.setPen(QPen(accent, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(QPolygonF({QPointF(22, 42), QPointF(30, 50), QPointF(44, 34)}));
    }
    else if (name == QLatin1String("graph-tags")) {
        p.setPen(accent);
        text(QStringLiteral("#"), 46, true, false, pm.rect());
    }
    else if (name == QLatin1String("graph-orphans")) {
        p.setPen(QPen(fg, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(14, 44), QPointF(30, 24));
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        p.drawEllipse(QPointF(14, 44), 6, 6);
        p.drawEllipse(QPointF(30, 24), 6, 6);
        p.setBrush(accent);
        p.drawEllipse(QPointF(48, 44), 7, 7);
    }
    else if (name == QLatin1String("graph-fit")) {
        p.drawEllipse(QPointF(32, 32), 16, 16);
        p.drawLine(QPointF(32, 6), QPointF(32, 18));
        p.drawLine(QPointF(32, 46), QPointF(32, 58));
        p.drawLine(QPointF(6, 32), QPointF(18, 32));
        p.drawLine(QPointF(46, 32), QPointF(58, 32));
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawEllipse(QPointF(32, 32), 5, 5);
    }
    p.end();
    return QIcon(pm);
}
