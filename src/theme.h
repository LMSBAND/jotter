// Fonts, colours and sizes. The document holds meaning; this decides how it looks.
#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QVector>

namespace jot {

// An icon by its freedesktop name: from the desktop's theme, else Jotter's built-in copy.
QIcon icon(const QString &name);

constexpr int PageWidth = 860;      // logical px of "paper"
constexpr int PageMargin = 70;      // px between the paper edge and the text

// A text face a document can be set in. The first is the system's own text
// font; the others are part of Jotter (data/fonts), so a document set in one
// looks the same on every machine and in what is exported from it.
struct Face {
    QString name;           // in the menu, and in the file: <!--font: EB Garamond-->
    QString family;         // the font family behind it
    QStringList files;      // regular, italic, bold, bold italic (two for a family without italics)
    qreal scale = 1.0;      // type size next to the system font's, so that the letters look as big
    QString generic;        // what CSS calls this kind of face
    bool builtIn() const { return !files.isEmpty(); }
};
const QVector<Face> &faces();
const Face &faceNamed(const QString &name);     // the system's for a name that is not one of them
QFont fontOf(const Face &face, qreal pointSize);    // for showing a face's name in itself

struct Theme {
    explicit Theme(bool dark = false);

    bool dark;
    QString ui;             // the system's text font: labels, and the text of a document that names no face
    QString body, mono;     // font families
    qreal bodyScale = 1.0;  // the document's face: its type size next to the system font's...
    qreal leading = 1.0;    // ...and its line height, likewise (see setFace)
    QFont::StyleHint bodyHint = QFont::AnyStyle;
    qreal size = 11.0;      // body point size
    qreal zoom = 1.0;

    QColor canvas, paper, text, muted, codeBg, codeBorder, inlineBg, error, link, accent, rule, spell;

    void setFace(const Face &face);

    qreal pt() const { return size * zoom; }
    qreal bodyPt() const { return pt() * bodyScale; }
    qreal monoPt() const { return pt() * 0.9; }
    qreal em() const;                       // body font size in logical px
    qreal layoutEm() const;                 // the same at zoom 1: spacing is stored in the document, so it must not follow zoom
    QFont bodyFont(qreal scale = 1.0) const;
    QFont monoFont(qreal scale = 1.0) const;
    QFont uiFont(qreal scale = 1.0) const;
    static qreal headingScale(int level);

    static bool systemIsDark();
};

}  // namespace jot
