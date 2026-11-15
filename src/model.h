// Document semantics.
//
// A Jotter document is a plain QTextDocument. What a block *is* (code, output,
// heading, quote...) lives in a few QTextBlockFormat properties, and inline
// meaning lives in ordinary character formats (bold, italic, fixed pitch =
// inline code). Nothing in the document knows about colours or fonts: the theme
// supplies those at highlight/paint time, and normalize() derives spacing from
// structure.
#pragma once

#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QVector>

namespace jot {

enum Prop {
    PKind = QTextFormat::UserProperty + 1,  // block: a Kind
    PLang,                                  // code block / fold object: fence info ("python", "bash"...)
    PErr,                                   // output block: this is an error/traceback
    PCell,                                  // code block: which cell a line belongs to, so that two cells
                                            // of the same language can sit next to each other
    PSrc = QTextFormat::UserProperty + 10,  // figure: "asset:name", a path relative to the document, or a URL
    PWidth,                                 // figure: display width in logical px (0 = natural)
    PAlt,                                   // figure: alt text
    PTex,                                   // math: LaTeX source
    PDisplay,                               // math: display (centred, own line) rather than inline
    PCode,                                  // fold object: the hidden cell's source
    PRaw,                                   // inline text written back to Markdown verbatim (unknown HTML)
    PFace = QTextFormat::UserProperty + 30, // the document (its root frame): the text face it is set in, by name
};

enum Kind { Para, Code, Output, Raw, Fold, Rule, Quote, Heading };

// Inline objects: each occupies one QChar::ObjectReplacementCharacter in the text.
enum { ObjFigure = QTextFormat::UserObject + 1, ObjMath, ObjFold };

enum Style { Bold = 0x1, Italic = 0x2, Strike = 0x4, Underline = 0x8, CodeSpan = 0x10, RawSpan = 0x20 };
Q_DECLARE_FLAGS(Styles, Style)
Q_DECLARE_OPERATORS_FOR_FLAGS(Styles)

// ---- reading meaning ------------------------------------------------------
Kind kindOf(const QTextBlock &block);
QString langOf(const QTextBlock &block);
QString langName(const QString &info);          // 'python title="x"' / '{.python}' -> "python"
QTextTable *tableOf(const QTextBlock &block);
QVector<QTextFragment> fragments(const QTextBlock &block);
bool objectFormat(const QTextBlock &block, int objectType, QTextCharFormat *out = nullptr);
bool hasFigure(const QTextBlock &block);
bool loneObject(const QTextBlock &block, QTextCharFormat *out);   // the block is exactly one inline object
Styles stylesOf(const QTextCharFormat &format, QString *href = nullptr);

// The face the document's text is set in ("" = the system's own). It is kept
// in the document so that changing it is an edit like any other: it can be undone.
QString faceOf(const QTextDocument *doc);
void setFace(QTextDocument *doc, const QString &name);

// ---- making formats -------------------------------------------------------
QTextBlockFormat blockFmt(Kind kind = Para, int level = 0, const QString &lang = QString(), bool err = false);
QTextCharFormat charFmt(Styles styles, const QString &href = QString());
QTextCharFormat figureFmt(const QString &src, qreal width = 0, const QString &alt = QString());
QTextCharFormat mathFmt(const QString &tex, bool display);
QTextCharFormat foldFmt(const QString &lang, const QString &code);
QTextTableFormat tableFmt(int columns);
QTextTable *insertTable(QTextCursor &cursor, int rows, int columns);    // leaves no stray empty paragraph
void removeBlock(const QTextBlock &block);                             // neighbours keep their formats
bool emptyParagraph(const QTextBlock &block);                          // a plain, empty line of body text

// ---- runs and cells -------------------------------------------------------
// A "run" is a stretch of adjacent blocks drawn as one thing: the lines of a
// code block, the lines of its output, a multi-paragraph quote. A "cell" is a
// code run (or a folded cell) plus the output run right after it.
bool sameRun(const QTextBlock &a, const QTextBlock &b);
QTextBlock runFirst(QTextBlock block);
QTextBlock runLast(QTextBlock block);
QTextBlock cellHead(QTextBlock block);          // first code line / fold block of the cell, or invalid
QTextBlock cellTail(const QTextBlock &head);    // last block of the cell's source
void cellSource(const QTextBlock &head, QString *info, QString *code);
bool cellOutputs(const QTextBlock &head, QTextBlock *first, QTextBlock *last);
QTextBlock cellEnd(const QTextBlock &head);     // last block of the cell including its output
QVector<QTextBlock> cellHeads(const QTextDocument *doc);

// ---- spacing --------------------------------------------------------------
// Margins depend on neighbours (the first line of a code block carries the
// band's top padding, the last list item carries the gap after the list), so
// they are recomputed around every edit rather than set once.
constexpr qreal PadV = 0.55;    // em of code-band padding above/below the text
constexpr qreal PadH = 0.9;     // em of code-band padding left/right

// (em: the body type size in px. leading: what the text face's own line height
// is to be multiplied by, see Theme::setFace.)
bool needsNormalize(const QTextDocument *doc, QTextBlock first, QTextBlock last, qreal em, qreal leading);
bool normalize(QTextDocument *doc, QTextBlock first, QTextBlock last, qreal em, qreal leading);

}  // namespace jot
