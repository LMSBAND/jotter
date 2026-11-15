// Fonts, colours and sizes. The document holds meaning; this decides how it looks.
#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>

namespace jot {

// An icon by its freedesktop name: from the desktop's theme, else Jotter's built-in copy.
QIcon icon(const QString &name);

constexpr int PageWidth = 860;      // logical px of "paper"
constexpr int PageMargin = 70;      // px between the paper edge and the text

struct Theme {
    explicit Theme(bool dark = false);

    bool dark;
    QString body, mono;     // font families
    qreal size = 11.0;      // body point size
    qreal zoom = 1.0;

    QColor canvas, paper, text, muted, codeBg, codeBorder, inlineBg, error, link, accent, rule, spell;

    qreal pt() const { return size * zoom; }
    qreal monoPt() const { return pt() * 0.9; }
    qreal em() const;                       // body font size in logical px
    qreal layoutEm() const;                 // the same at zoom 1: spacing is stored in the document, so it must not follow zoom
    QFont bodyFont(qreal scale = 1.0) const;
    QFont monoFont(qreal scale = 1.0) const;
    static qreal headingScale(int level);

    static bool systemIsDark();
};

}  // namespace jot
