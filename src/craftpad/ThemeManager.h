/*
 * craftpad++ - extensão do Notepad Next.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QColor>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QPalette>

#include "MarkdownRenderer.h"

class ScintillaNext;

struct CraftTheme {
    QString id;
    QString name;
    bool dark = false;
    QColor window, base, text, muted, accent, border, selection, codeBackground, tableHeader;
};

// Temas claros e escuros, aplicados com sutileza: paleta do app,
// cores do editor e da prévia. "Sistema" mantém o visual do macOS.
class ThemeManager : public QObject
{
    Q_OBJECT

public:
    static ThemeManager *instance();

    QList<CraftTheme> themes() const { return themeList; }
    CraftTheme current() const;
    QString currentId() const { return currentThemeId; }
    void setTheme(const QString &id);

    void applyToEditor(ScintillaNext *editor);
    PreviewStyle previewStyle() const;

    // Fonte da prévia (só visual, não muda o arquivo)
    void setPreviewFonts(const QString &body, int bodySize, const QString &heading, int headingSize, const QString &code, int codeSize);

    // Ícones da barra de formatação, desenhados na cor do tema
    QIcon icon(const QString &name) const;

signals:
    void themeChanged();
    void previewStyleChanged();

private:
    explicit ThemeManager(QObject *parent = nullptr);
    QList<CraftTheme> themeList;
    QString currentThemeId;
    QPalette originalPalette;
    QString originalStyle;
};

#endif // THEMEMANAGER_H
