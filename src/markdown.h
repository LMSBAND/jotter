// Markdown <-> document.
//
// The file on disk is plain Markdown that renders sensibly anywhere:
//
//     ```python                    a code cell
//     print(6 * 7)
//     ```
//
//     ```output                    what it printed (```error for a traceback)
//     42
//     ```
//
//     ![output](notes.assets/out-3fa1c2d4e5f6.png)      a figure a cell produced
//
//     <!--cell python              a cell whose code is hidden in the document
//     plt.plot(x, y)
//     -->
//
//     $e^{i\pi} = -1$              math, inline or $$display$$
//
// Reading goes through md4c (CommonMark + tables, strikethrough, task lists,
// math), so any Markdown opens. Writing checks itself: every paragraph is parsed
// back and compared, and if the pretty form would not round-trip it is written
// again with HTML tags / full escaping. Formatting is never silently lost.
#pragma once

#include "model.h"

#include <QSet>
#include <QStringList>

namespace jot {

class AssetStore;

// ---- inline content --------------------------------------------------------
struct Atom {
    enum Type { None, Image, Math } type = None;
    QString src;            // image source, or LaTeX for math
    QString alt;
    qreal width = 0;
    bool display = false;   // math: display style
    bool operator==(const Atom &o) const;
};

// A stretch of inline content: styled text, or one object (atom).
struct Run {
    QString text;
    Styles styles;
    QString href;
    Atom atom;
    bool isAtom() const { return atom.type != Atom::None; }
    bool operator==(const Run &o) const;
};
using Runs = QVector<Run>;

// ---- parsed Markdown -------------------------------------------------------
struct MdBlock {
    enum Type { Paragraph, Heading, CodeBlock, HtmlBlock, Rule, ListOpen, ListClose,
                ItemOpen, ItemClose, QuoteOpen, QuoteClose, Table } type = Paragraph;
    int level = 0;                  // Heading
    bool ordered = false;           // ListOpen
    int task = 0;                   // ItemOpen: 0 plain, 1 unchecked, 2 checked
    QString info;                   // CodeBlock: fence info string
    QString text;                   // CodeBlock / HtmlBlock: raw content
    Runs runs;                      // Paragraph / Heading
    QVector<QVector<Runs>> rows;    // Table
};
QVector<MdBlock> parseMarkdown(const QString &text);

// YAML front matter is kept verbatim: returns it (or ""), puts the rest in *body.
QString splitFrontMatter(const QString &text, QString *body);

// ---- document -> Markdown ---------------------------------------------------
class MarkdownWriter
{
public:
    explicit MarkdownWriter(const QTextDocument *doc, const AssetStore *store = nullptr, bool absolute = false,
                            const QString &frontMatter = QString());
    QString text();

    QSet<QString> assets;       // names of every "asset:" picture written
    int modeUsed = 0;           // deepest fallback inlineText() needed (0 = plain Markdown); for tests

    Runs runsOf(const QTextBlock &block) const;
    enum { OneLine = 1, Cell = 2 };      // flags: no raw line breaks / inside a table cell
    QString inlineText(Runs runs, int flags = 0, int heading = 0);
    static Runs prepared(const Runs &runs);     // runs in the form they will read back in

private:
    struct Item { QTextBlock block; QTextTable *table = nullptr; };
    void collect(QTextFrame *frame, QVector<Item> &items) const;
    Kind kind(const QTextBlock &b) const;
    QString lang(const QTextBlock &b) const;
    QString fileSrc(const QString &src);
    QString atomText(const Atom &a, bool html) const;
    struct Escape { bool oneLine, cell, everything, tilde, dollar; };
    QString emitRuns(const Runs &runs, int mode, const Escape &esc) const;
    QString table(QTextTable *t);
    QString list(const QVector<Item> &items, int &i);
    static QString fence(const QString &info, const QStringList &lines);

    const QTextDocument *m_doc;
    const AssetStore *m_store;
    bool m_absolute;
    QString m_front;
};

// ---- Markdown -> document ---------------------------------------------------
class MarkdownReader
{
public:
    explicit MarkdownReader(const QTextCursor &cursor, AssetStore *store = nullptr, bool importImages = false);
    void read(const QString &text);         // into an empty block: everything becomes blocks
    void insert(const QString &text);       // paste: a single paragraph goes inline
    void insertRuns(const Runs &runs);
    QTextCursor cursor() const { return m_c; }
    QString baseDir;            // where relative picture paths in the text are relative to (importing a file)

private:
    void begin(const QTextBlockFormat &fmt);
    void lines(const QTextBlockFormat &fmt, const QString &text);
    void paragraph(Runs runs);
    void imageBlock(Runs runs);
    void fence(const QString &info, const QString &content);
    void html(const QString &content);
    void table(const MdBlock &block);
    void blocks(const QVector<MdBlock> &blocks);

    struct ListCtx { bool ordered; QTextList *list; };
    QTextCursor m_c;
    AssetStore *m_store;
    bool m_import;              // copy foreign pictures in (pasting)
    bool m_fresh = true;        // the cursor's block is empty and ours to use
    QVector<ListCtx> m_lists;
    bool m_itemOpen = false;
    int m_itemTask = 0;
    int m_quote = 0;
};

// Replace the document's content with Markdown. Returns the front matter.
QString loadMarkdown(QTextDocument *doc, const QString &text, AssetStore *store = nullptr, bool importImages = false);

}  // namespace jot
