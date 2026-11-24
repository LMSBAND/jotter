// Jotter's tests: one plain executable, no framework.
//   jotter-tests            run everything
//   jotter-tests markdown   run the tests whose name contains "markdown"
#include "assets.h"
#include "editor.h"
#include "exporter.h"
#include "kernel.h"
#include "mainwindow.h"
#include "markdown.h"
#include "mathtex.h"
#include "model.h"
#include "render.h"
#include "storyboard.h"

#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QUrl>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QDialog>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QFile>
#include <QFontInfo>
#include <QFontDatabase>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QToolButton>
#include <QPainter>
#include <QPlainTextEdit>
#include <QClipboard>
#include <QComboBox>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTextDocument>
#include <QTextLayout>
#include <cstdio>
#include <functional>
#include <random>

using namespace jot;

static int g_failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            ++g_failures;                                                              \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                 \
    do {                                                                               \
        const auto va = (a);                                                           \
        const auto vb = (b);                                                           \
        if (!(va == vb)) {                                                             \
            ++g_failures;                                                              \
            std::fprintf(stderr, "  FAIL %s:%d: %s == %s\n    got:  %s\n    want: %s\n", __FILE__, __LINE__, #a, #b, \
                         qPrintable(show(va)), qPrintable(show(vb)));                  \
        }                                                                              \
    } while (0)

static QString show(const QString &s) { QString t = s; return "\"" + t.replace('\n', "\\n") + "\""; }
static QString show(int v) { return QString::number(v); }

static QString show(const Runs &runs)
{
    QStringList parts;
    for (const Run &r : runs) {
        if (r.isAtom())
            parts << QString("{%1 %2 alt=%3 w=%4 href=%5}").arg(r.atom.type == Atom::Math ? "math" : "img", r.atom.src, r.atom.alt)
                         .arg(r.atom.width).arg(r.href);
        else
            parts << QString("{%1 s=%2 href=%3}").arg(show(r.text)).arg(int(r.styles)).arg(r.href);
    }
    return parts.join(" ");
}

static QVector<QTextBlock> blocksOf(const QTextDocument &doc)
{
    QVector<QTextBlock> v;
    for (QTextBlock b = doc.firstBlock(); b.isValid(); b = b.next())
        v.append(b);
    return v;
}

static AssetStore &store()
{
    static AssetStore s;
    s.docPath = "/tmp/jotter-test-doc/notes.md";
    return s;
}

static QString roundtrip(const QString &text, QTextDocument *doc)
{
    const QString front = loadMarkdown(doc, text, &store());
    return MarkdownWriter(doc, &store(), false, front).text();
}

static void printDiff(const QString &want, const QString &got)
{
    const QStringList a = want.split('\n'), b = got.split('\n');
    for (int i = 0; i < qMax(a.size(), b.size()); ++i) {
        const QString x = i < a.size() ? a[i] : "<end>", y = i < b.size() ? b[i] : "<end>";
        if (x != y) {
            std::fprintf(stderr, "    line %d\n      want: %s\n      got:  %s\n", i + 1, qPrintable(x), qPrintable(y));
            return;
        }
    }
}

// ---- Markdown ---------------------------------------------------------------

// Text in canonical form comes back byte-identical.
static const char *kCanonical = R"MD(---
title: Test
author: me
---

# Heading *one*

Plain paragraph with **bold**, *italic*, ***both***, ~~struck~~, <u>under</u>, `code` and a [link](https://example.com/a?b=1).

Second paragraph with a hard\
break, an escaped \*star\*, snake_case, 2 \* 3, \<tag>, a price of \$5 and \$6, about ~3 of them, and AT&T.

## Lists

- one
- two with **bold**
    - nested a
    - nested b
        - deep
- three

1. first
2. second
    1. inner
3. third

- [ ] todo
- [x] done

> A quote.
>
> Second quote paragraph.

---

```python
import numpy as np

x = np.linspace(0, 1)
print(x)
```

```output
[0.  0.5 1. ]
```

![output](notes.assets/out-0123456789ab.png)

```error
Traceback (most recent call last):
ZeroDivisionError: division by zero
```

<!--cell python
plt.plot(x, y)

plt.title("hidden")
-->

![output](notes.assets/out-ba9876543210.png)

```bash
echo "fence with ``` inside is fine"
```

| Name | Value |
| --- | --- |
| a \| b | **1** |
| c | `2`<br>two lines |

Inline math $e^{i\pi} = -1$ and an image ![alt text](pics/photo.png) in a sentence.

$$
\int_0^1 x^2\,dx = \frac{1}{3}
$$

<img src="notes.assets/shot.png" alt="" width="320">

<div align="center">
raw html stays
</div>

Final paragraph.
)MD";

static void test_markdown_canonical()
{
    QTextDocument doc;
    const QString want = QString::fromUtf8(kCanonical);
    const QString got = roundtrip(want, &doc);
    if (got != want) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL canonical round trip differs\n");
        printDiff(want, got);
    }

    // structure landed where it should
    QHash<int, QVector<QTextBlock>> kinds;
    for (const QTextBlock &b : blocksOf(doc))
        kinds[kindOf(b)].append(b);
    CHECK_EQ(kinds[Code].size(), 5);
    CHECK_EQ(langOf(kinds[Code].value(0)), QString("python"));
    CHECK_EQ(langOf(kinds[Code].value(4)), QString("bash"));
    CHECK_EQ(kinds[Output].size(), 5);
    CHECK(kinds[Output].value(3).blockFormat().boolProperty(PErr));
    QTextCharFormat cf;
    CHECK(objectFormat(kinds[Output].value(1), ObjFigure, &cf));
    CHECK_EQ(cf.stringProperty(PSrc), QString("asset:out-0123456789ab.png"));
    CHECK(objectFormat(kinds[Fold].value(0), ObjFold, &cf));
    CHECK_EQ(cf.stringProperty(PLang), QString("python"));
    CHECK_EQ(cf.stringProperty(PCode), QString("plt.plot(x, y)\n\nplt.title(\"hidden\")"));
    CHECK_EQ(kinds[Heading].size(), 2);
    CHECK_EQ(kinds[Quote].size(), 2);
    CHECK_EQ(kinds[Rule].size(), 1);
    CHECK_EQ(kinds[Raw].size(), 3);

    const QVector<QTextBlock> heads = cellHeads(&doc);
    CHECK_EQ(heads.size(), 3);
    QString info, code;
    cellSource(heads.value(0), &info, &code);
    CHECK(code.startsWith("import numpy"));
    cellSource(heads.value(1), &info, &code);
    CHECK_EQ(info, QString("python"));
    QTextBlock first, last;
    CHECK(cellOutputs(heads.value(0), &first, &last));
    CHECK_EQ(first.blockNumber(), kinds[Output].value(0).blockNumber());
    CHECK_EQ(last.blockNumber(), kinds[Output].value(3).blockNumber());
    CHECK_EQ(cellHead(kinds[Output].value(2)).blockNumber(), heads.value(0).blockNumber());
}

// Markdown written by other tools settles after one pass and keeps its meaning.
static const char *kForeign = R"MD(Setext Heading
==============

Hard-wrapped paragraph that
continues on a second line with __bold__ and _emphasis_
and a <mark>marked</mark> word.

* star bullet
* another

  continuation paragraph inside the item

+ plus bullet

1) paren list
2) second

    indented code block

~~~js
tilde fence
~~~

```{.python .numberLines}
pandoc style
```

```python {hide}
hidden = "-->"
```

Term with trailing hash #
# ATX with closing hashes ##

![remote](https://example.com/x.png "title")
[ref link][r] and <https://auto.link> and &copy; entity

[r]: https://example.com/ref

Line ending with backslash\\

- [ ] task in list
- plain

> quote with **bold**
> lazy continuation

| a | b |
|:--|--:|
| 1 | 2 |

\# not a heading

1\. not a list

Text with * loose asterisk and under_score_s and 3 < 4 and a|pipe.
)MD";

static void test_markdown_foreign()
{
    QTextDocument doc, doc2;
    const QString once = roundtrip(QString::fromUtf8(kForeign), &doc);
    const QString twice = roundtrip(once, &doc2);
    if (once != twice) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL foreign markdown did not settle\n");
        printDiff(once, twice);
    }
    const char *needles[] = {
        "# Setext Heading", "**bold** and *emphasis*", "<mark>marked</mark>", "- star bullet",
        "    continuation paragraph", "1. paren list", "```js\ntilde fence\n```",
        "```{.python .numberLines}\npandoc style\n```", "```python {hide}\nhidden = \"-->\"\n```",
        "# ATX with closing hashes", "![remote](https://example.com/x.png)", "[ref link](https://example.com/ref)",
        "\xC2\xA9", "- [ ] task in list", "> quote with **bold** lazy continuation", "| a | b |",
        "\\# not a heading", "1\\. not a list", "loose asterisk", "under_score_s", "3 < 4", "a|pipe",
        "Term with trailing hash #", "Line ending with backslash\\\\"};
    for (const char *needle : needles) {
        if (!once.contains(QString::fromUtf8(needle))) {
            ++g_failures;
            std::fprintf(stderr, "  FAIL missing after round trip: %s\n", needle);
        }
    }
    if (g_failures)
        std::fprintf(stderr, "%s\n", qPrintable(once));
}

static Runs semantic(const QTextBlock &b)
{
    Runs raw = MarkdownWriter(b.document()).runsOf(b);
    if (kindOf(b) == Heading)
        for (Run &r : raw)
            r.styles &= ~Styles(Bold);
    Runs runs = MarkdownWriter::prepared(raw);
    for (Run &r : runs) {      // whitespace-only text may carry any style; ignore that
        if (kindOf(b) == Heading)
            r.styles &= ~Styles(Bold);      // bold means nothing in a heading
        if (!r.isAtom() && r.text.trimmed().isEmpty()) {
            r.text = " ";
            r.styles = Styles();
            r.href.clear();
        }
    }
    return runs;
}

static QTextDocument *docFrom(const Runs &runs, const QTextBlockFormat &fmt)
{
    auto *doc = new QTextDocument;
    QTextCursor c(doc);
    c.setBlockFormat(fmt);
    MarkdownReader(c, &store()).insertRuns(runs);
    return doc;
}

static Run textRun(const QString &text, Styles styles = Styles(), const QString &href = QString())
{
    Run r;
    r.text = text;
    r.styles = styles;
    r.href = href;
    return r;
}

// Everyday formatting must come out as plain Markdown, not the HTML fallback.
static void test_markdown_pretty()
{
    struct Case { Runs runs; QString want; };
    const QVector<Case> cases = {
        {{textRun("Note: see ", Bold), textRun("the docs", Bold, "https://x.y/docs"), textRun(" now", Bold)},
         "**Note: see [the docs](https://x.y/docs) now**"},
        {{textRun("a", Bold | Italic), textRun("b", Bold)}, "***a*b**"},
        {{textRun("plain "), textRun("bold", Bold), textRun("italic", Italic), textRun(" end")}, "plain **bold***italic* end"},
        {{textRun("call "), textRun("f(x)", CodeSpan), textRun(" then "), textRun("under", Underline), textRun(" and "),
          textRun("gone", Strike)},
         "call `f(x)` then <u>under</u> and ~~gone~~"},
        {{textRun("link with "), textRun("code", CodeSpan, "http://a.b"), textRun(" inside", Styles(), "http://a.b")},
         "link with [`code` inside](http://a.b)"},
        {{textRun("5 * 3 = 15, a_b_c, [x], <b>, & &amp; costs $5, ~3 #1")},
         "5 \\* 3 = 15, a_b_c, \\[x\\], \\<b>, & \\&amp; costs $5, ~3 #1"},
        {{textRun("in"), textRun("tra", Italic), textRun("word")}, "in*tra*word"},
    };
    for (const Case &c : cases) {
        QScopedPointer<QTextDocument> doc(docFrom(c.runs, QTextBlockFormat()));
        MarkdownWriter w(doc.data());
        CHECK_EQ(w.text().trimmed(), c.want);
        CHECK_EQ(w.modeUsed, 0);
    }
}

// Any mix of inline formatting written from a document reads back identical.
static void test_markdown_inline_random()
{
    std::mt19937 rng(7);
    auto rnd = [&](int n) { return int(rng() % uint(n)); };
    auto chance = [&](double p) { return (rng() % 10000) < p * 10000; };
    const QStringList words = {
        "alpha", "beta ", " gamma", "a*b", "x_y", "_edge_", "**", "`tick`", "[brackets]", "(parens)", "<tag>", "1. ",
        "# ", "- ", "!", "\\", "$5", "~~", "~", "&amp;", " ", "  ", "é ü", "end.", "a|b", "http://x.y/z?q=1", ">", "+ ",
        "===", "---", QString(QChar::Nbsp), QString(QChar::LineSeparator), "back\\slash", "!(", "]("};
    const Style styleList[] = {Bold, Italic, Strike, Underline, CodeSpan};
    const QStringList hrefs = {"", "", "", "https://example.com/p?a=1&b=2", "http://x.y/with space", "rel/path(1).md"};

    int fails = 0, modes[3] = {0, 0, 0};
    for (int n = 0; n < 3000; ++n) {
        Runs runs;
        const int count = 1 + rnd(7);
        for (int k = 0; k < count; ++k) {
            Run r;
            r.href = hrefs[rnd(hrefs.size())];
            if (chance(0.12)) {
                const int which = rnd(3);
                r.atom.type = which == 2 ? Atom::Math : Atom::Image;
                r.atom.src = which == 0 ? "pics/a b.png" : which == 1 ? "asset:out-aaaaaaaaaaaa.png" : "\\frac{a}{b}";
                r.atom.alt = which == 0 ? "alt" : "";
                r.atom.width = which == 1 ? 240 : 0;
            } else {
                const int parts = 1 + rnd(4);
                for (int p = 0; p < parts; ++p)
                    r.text += words[rnd(words.size())];
                for (const Style s : styleList)
                    if (chance(0.25))
                        r.styles |= s;
            }
            runs.append(r);
        }
        const int which = rnd(3);
        const QTextBlockFormat fmt = which == 0 ? QTextBlockFormat() : which == 1 ? blockFmt(Heading, 1 + rnd(4)) : blockFmt(Quote);
        QScopedPointer<QTextDocument> doc(docFrom(runs, fmt));
        MarkdownWriter writer(doc.data(), &store());
        const QString text = writer.text();
        ++modes[writer.modeUsed];
        QTextDocument doc2;
        loadMarkdown(&doc2, text, &store());

        QVector<Runs> want, got;
        for (const QTextBlock &b : blocksOf(*doc))
            if (!semantic(b).isEmpty())
                want.append(semantic(b));
        for (const QTextBlock &b : blocksOf(doc2))
            if (!semantic(b).isEmpty())
                got.append(semantic(b));
        const bool sameKind = want.isEmpty() || kindOf(doc->firstBlock()) == kindOf(doc2.firstBlock());
        if (want != got || !sameKind) {
            if (++fails <= 5) {
                std::fprintf(stderr, "  MISMATCH %d\n    runs: %s\n    text: %s\n", n, qPrintable(show(runs)), qPrintable(show(text)));
                for (const Runs &r : want)
                    std::fprintf(stderr, "    want: %s\n", qPrintable(show(r)));
                for (const Runs &r : got)
                    std::fprintf(stderr, "    got:  %s\n", qPrintable(show(r)));
            }
        }
    }
    std::printf("  inline writes: plain markdown %d, html tags %d, full escape %d\n", modes[0], modes[1], modes[2]);
    CHECK_EQ(fails, 0);
}

// ---- kernel -----------------------------------------------------------------

// Shared state between cells, figures, errors, interrupt, restart, one-shot languages.
static void test_kernel()
{
    Kernel k;
    QTemporaryDir tmp;
    struct Result { QVector<Kernel::Item> items; bool ok; };
    QVector<Result> results;
    QStringList states;
    QObject::connect(&k, &Kernel::stateChanged, [&](const QString &s) { states << s; });
    auto run = [&](const QString &info, const QString &code) {
        k.run(info, code, tmp.path(), tmp.path(), [&](const QVector<Kernel::Item> &items, bool ok) { results.append({items, ok}); });
    };
    auto wait = [&](int n) {
        QElapsedTimer t;
        t.start();
        while (results.size() < n && t.elapsed() < 60000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return results.size() >= n;
    };
    auto text = [&](int i, int item = 0) { return results.value(i).items.value(item).text; };
    auto type = [&](int i, int item = 0) { return int(results.value(i).items.value(item).type); };

#ifdef Q_OS_WIN
    const QString shell = "batch", hello = "@echo off\necho hi\necho err 1>&2\ncd", fail = "@exit 3";
    const QString here = QDir::toNativeSeparators(tmp.path());
#else
    const QString shell = "bash", hello = "echo hi; echo err 1>&2; pwd", fail = "exit 3";
    const QString here = tmp.path();
#endif
    CHECK(Kernel::canRun("python") && Kernel::canRun(shell) && !Kernel::canRun("yaml"));

    run("python", "x = 6\nprint('a')\nx * 7");
    run("python", "x += 1\nx");                 // shared namespace, queued behind the first
    run("python", "import matplotlib.pyplot as plt\nplt.plot([1, 2, 3])");
    run("python", "1/0");
    run(shell, hello);
    run("dot", "digraph { a -> b }");
    run(shell, fail);
    if (!wait(7)) {
        CHECK(!"kernel timed out");
        return;
    }
    CHECK_EQ(text(0), QString("a\n42\n"));
    CHECK(results[0].ok && results[0].items.size() == 1);
    CHECK_EQ(text(1), QString("7\n"));
    CHECK(results[2].ok && results[2].items.size() == 1 && type(2) == Kernel::Item::Image);
    CHECK(QFile::exists(tmp.path() + "/" + text(2)));
    CHECK(!results[3].ok && type(3) == Kernel::Item::Error);
    CHECK(text(3).contains("ZeroDivisionError") && text(3).contains("<cell 4>"));
    CHECK(results[4].ok && text(4).contains("hi") && text(4).contains("err") && text(4).contains(here, Qt::CaseInsensitive));
    if (Kernel::canRun("dot"))
        CHECK(results[5].ok && type(5) == Kernel::Item::Image && QFile::exists(tmp.path() + "/" + text(5)));
    CHECK(!results[6].ok);
    CHECK_EQ(text(6), QString("Exit code 3."));

    // interrupt a long-running cell; the kernel survives and keeps its state
    run("python", "import time\nfor i in range(600): time.sleep(0.1)");
    QTimer::singleShot(1500, &k, [&] { k.interrupt(); });
    CHECK(wait(8));
    CHECK(!results.value(7).ok && results.value(7).items.value(results.value(7).items.size() - 1).text.contains("KeyboardInterrupt"));
    run("python", "x");
    CHECK(wait(9));
    CHECK_EQ(text(8), QString("7\n"));

    // restart forgets state
    k.restart();
    run("python", "x");
    CHECK(wait(10));
    CHECK(!results.value(9).ok && text(9).contains("NameError"));

    // a !shell line: what the command printed and nothing more; a slow one can be stopped
    const int base = results.size();
    run("python", "!echo out");
#ifdef Q_OS_WIN
    run("python", "y = 5\n!python -c \"import time; time.sleep(60)\"");
#else
    run("python", "y = 5\n!sleep 60");
#endif
    QElapsedTimer stopwatch;
    stopwatch.start();
    QTimer::singleShot(2500, &k, [&] { k.interrupt(); });
    CHECK(wait(base + 2));
    run("python", "y");
    CHECK(wait(base + 3));
    CHECK_EQ(text(base), QString("out\n"));
    CHECK(!results.value(base + 1).ok && stopwatch.elapsed() < 20000);
    CHECK_EQ(text(base + 2), QString("5\n"));
    results.remove(base, results.size() - base);

    // sql cells run in the Python session: a query's rows are `_` there, :name is a Python variable
    run("sql", "create table t(name text, score int);\ninsert into t values ('ann', 91), ('bob', 78);\n"
               "select name, score from t order by score");
    run("python", "cutoff = 80\nprint(_.columns, _['score'], list(_))");
    run("sql", "select name from t where score > :cutoff");
    run("sql", "select * from nowhere");
    run("python", "_[0][0]");
    CHECK(wait(base + 5));
    CHECK(Kernel::canRun("sql"));
    CHECK_EQ(text(base), QString("name  score\n----  -----\nbob      78\nann      91\n"));
    CHECK_EQ(text(base + 1), QString("['name', 'score'] [78, 91] [('bob', 78), ('ann', 91)]\n"));
    CHECK_EQ(text(base + 2), QString("name\n----\nann\n"));
    CHECK(!results.value(base + 3).ok && type(base + 3) == Kernel::Item::Error && text(base + 3).contains("no such table: nowhere"));
    CHECK_EQ(text(base + 4), QString("'ann'\n"));
    results.remove(base, results.size() - base);

    // .open, as in the sqlite3 shell: a file for this cell and the ones after it; alone, back to the scratch tables
    run("sql", ".open \"made here.db\"\ncreate table if not exists p (a text, n integer);\nselect * from p;");
    run("sql", "insert into p values ('x', 1);\nselect count(*) as n from p");
    run("sql", ".open\nselect count(*) as n from t");
    run("sql", ".tables");
    CHECK(wait(base + 4));
    CHECK_EQ(text(base), QString("a  n\n-  -\n"));
    CHECK(QFile::exists(tmp.path() + "/made here.db"));
    CHECK_EQ(text(base + 1), QString("n\n-\n1\n"));
    CHECK_EQ(text(base + 2), QString("n\n-\n2\n"));
    CHECK(!results.value(base + 3).ok && text(base + 3).contains(".open"));
    results.remove(base, results.size() - base);

    // stored variables reach every language: a session that is running, a one-shot tool, a fresh session
    const auto vars = Kernel::parseEnvironment("# mine\nJOTTER_SECRET = \"s3 cret\"\nexport DATABASE=shop.db\n\nnot a variable\n");
    CHECK(vars.size() == 2 && vars.value("JOTTER_SECRET") == "s3 cret" && vars.value("DATABASE") == "shop.db");
    k.setEnvironment(vars);
    const QString secret = "import os\nprint(os.environ.get('JOTTER_SECRET'))";
    run("python", secret);
#ifdef Q_OS_WIN
    run(shell, "@echo %JOTTER_SECRET%");
#else
    run(shell, "echo \"$JOTTER_SECRET\"");
#endif
    run("sql", "create table orders(id int)");      // DATABASE: a file beside the document
    CHECK(wait(base + 3));
    k.restart();
    run("python", secret);
    k.setEnvironment({});
    run("python", secret);
    run("sql", "select count(*) as n from t");      // back on the scratch database, which the restart emptied
    CHECK(wait(base + 6));
    CHECK_EQ(text(base), QString("s3 cret\n"));
    CHECK(text(base + 1).startsWith("s3 cret"));
    CHECK(results.value(base + 2).ok && QFile::exists(tmp.path() + "/shop.db"));
    CHECK_EQ(text(base + 3), QString("s3 cret\n"));
    CHECK_EQ(text(base + 4), QString("None\n"));
    CHECK(!results.value(base + 5).ok && text(base + 5).contains("no such table: t"));
    results.remove(base, results.size() - base);

    // unicode, and a kernel that dies mid-cell
    run("python", QString::fromUtf8("print('h\xC3\xA9llo \xE2\x9C\x93')"));
    run("python", "import os; os._exit(4)");
    run("python", "print('back')");
    CHECK(wait(13));
    CHECK_EQ(text(10), QString::fromUtf8("h\xC3\xA9llo \xE2\x9C\x93\n"));
    CHECK(!results.value(11).ok && text(11).contains("stopped"));
    CHECK_EQ(text(12), QString("back\n"));
    CHECK(states.contains("busy") && !states.isEmpty() && states.last() == "idle");
    k.shutdown();
}

// ---- editor -----------------------------------------------------------------

struct Typist {
    Editor ed;
    Typist()
    {
        ed.theme = Theme(false);
        ed.applyTheme();
        ed.setAttribute(Qt::WA_DontShowOnScreen);
        ed.resize(1000, 800);
        ed.show();
        QApplication::processEvents();
        ed.setFocus();
    }
    void type(const QString &text)
    {
        for (const QChar ch : text) {
            if (ch == '\n')
                QTest::keyClick(&ed, Qt::Key_Return);
            else if (ch == '\t')
                QTest::keyClick(&ed, Qt::Key_Tab);
            else
                QTest::keyClicks(&ed, QString(ch));
        }
    }
    void key(Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) { QTest::keyClick(&ed, k, m); }
    QString md() const { return ed.toMarkdown(); }
    bool tidy() const   // spacing agrees with structure everywhere
    {
        return !needsNormalize(ed.document(), ed.document()->firstBlock(), ed.document()->lastBlock(), ed.theme.layoutEm(),
                               ed.theme.leading);
    }
    bool settle(int msecs = 60000)      // wait for running cells
    {
        QElapsedTimer t;
        t.start();
        while (ed.cellBusy() && t.elapsed() < msecs)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return !ed.cellBusy();
    }
};

// Markdown habits format as you type.
static void test_editor_typing()
{
    Typist t;
    t.type("# Title\nBody text\n- one\ntwo\n\nAfter list\n1. first\nsecond\n\n> quoted\n\n[] todo\n[x] done\n\n---\nend");
    CHECK_EQ(t.md(), QString("# Title\n\nBody text\n\n- one\n- two\n\nAfter list\n\n1. first\n2. second\n\n> quoted\n\n"
                             "- [ ] todo\n- [x] done\n\n---\n\nend\n"));
    CHECK(t.tidy());

    Typist i;
    i.type("This is **bold** and *it* and `code` and ~~gone~~ then $x^2$ ok");
    CHECK_EQ(i.md(), QString("This is **bold** and *it* and `code` and ~~gone~~ then $x^2$ ok\n"));
    i.key(Qt::Key_Z, Qt::ControlModifier);          // one undo takes back the last conversion only
    CHECK(i.md().contains("**bold**"));

    // Enter at the end of a heading starts body text; Backspace at its start unstyles it
    Typist h;
    h.type("## Head\ntext");
    CHECK_EQ(kindOf(h.ed.document()->firstBlock()), Heading);
    CHECK_EQ(kindOf(h.ed.document()->lastBlock()), Para);
    QTextCursor c(h.ed.document());
    h.ed.setTextCursor(c);
    h.key(Qt::Key_Backspace);
    CHECK_EQ(kindOf(h.ed.document()->firstBlock()), Para);
    CHECK_EQ(h.md(), QString("Head\n\ntext\n"));
    CHECK(h.tidy());
}

// Code cells: indentation, running, output placement, staying separate from prose.
static void test_editor_cells()
{
    Typist t;
    t.type("Intro\n```python\nfor i in range(3):\nprint(i)");
    CHECK_EQ(t.md(), QString("Intro\n\n```python\nfor i in range(3):\n    print(i)\n```\n"));
    CHECK_EQ(t.ed.document()->blockCount(), 4);     // a paragraph to type in is kept after the cell
    CHECK(t.tidy());

    t.key(Qt::Key_Return, Qt::ControlModifier);     // run in place
    CHECK(t.settle());
    CHECK_EQ(t.md(), QString("Intro\n\n```python\nfor i in range(3):\n    print(i)\n```\n\n```output\n0\n1\n2\n```\n"));
    CHECK(t.tidy());

    // running again replaces the output; the session remembers variables
    t.key(Qt::Key_Return);
    t.key(Qt::Key_Backspace);                       // out of the indent
    t.type("total = i * 10");
    t.key(Qt::Key_Return, Qt::ShiftModifier);       // run and move on
    CHECK(t.settle());
    CHECK_EQ(kindOf(t.ed.textCursor().block()), Para);
    t.type("After.\n```python\ntotal");
    t.key(Qt::Key_Return, Qt::ControlModifier);
    CHECK(t.settle());
    CHECK_EQ(t.md(), QString("Intro\n\n```python\nfor i in range(3):\n    print(i)\ntotal = i * 10\n```\n\n```output\n0\n1\n2\n```\n\n"
                             "After.\n\n```python\ntotal\n```\n\n```output\n20\n```\n"));

    // an error is shown as one, and a plot becomes a picture in the document
    t.key(Qt::Key_Return, Qt::AltModifier);         // new paragraph below the cell
    t.type("```python\nimport matplotlib.pyplot as plt\nplt.plot([1, 2, 3])\n1/0");
    t.key(Qt::Key_Return, Qt::ControlModifier);
    CHECK(t.settle());
    const QString md = t.md();
    CHECK(md.contains("```error\n") && md.contains("ZeroDivisionError"));
    CHECK(md.contains("![output](") && md.contains("out-"));
    CHECK(t.tidy());

    // a cell that ends in a blank line keeps it when its output is replaced or cleared
    {
        Typist e;
        const QString src = "```python\nprint(1)\n\n```\n\n```output\nold\n```\n";
        e.ed.loadMarkdown(src);
        e.ed.runCell();
        CHECK(e.settle());
        CHECK_EQ(e.md(), QString("```python\nprint(1)\n\n```\n\n```output\n1\n```\n"));
        e.ed.clearOutputs(true);
        CHECK_EQ(e.md(), QString("```python\nprint(1)\n\n```\n"));
        CHECK(e.tidy());
    }

    // hide the code and bring it back
    const QString before = t.md();
    t.ed.toggleFold();
    CHECK(t.md().contains("<!--cell python\nimport matplotlib.pyplot as plt"));
    CHECK(t.tidy());
    t.ed.toggleFold();
    CHECK_EQ(t.md(), before);

    // every cell's code hidden at once, in one undo step, and back
    {
        Typist f;
        const QString doc = "Intro\n\n```python\na = 1\nb = 2\n```\n\n```output\nx\n```\n\nMiddle\n\n```bash\necho hi\n```\n\nEnd\n";
        f.ed.loadMarkdown(doc);
        CHECK(f.ed.hasVisibleCode() && !f.ed.hasHiddenCode());
        f.ed.foldAll(true);
        CHECK_EQ(f.md(), QString("Intro\n\n<!--cell python\na = 1\nb = 2\n-->\n\n```output\nx\n```\n\nMiddle\n\n<!--cell bash\necho hi\n-->\n\nEnd\n"));
        CHECK(!f.ed.hasVisibleCode() && f.ed.hasHiddenCode());
        CHECK(f.tidy());
        f.ed.undoStep();
        CHECK_EQ(f.md(), doc);
        f.ed.redoStep();
        CHECK(!f.ed.hasVisibleCode());
        f.ed.foldAll(false);
        CHECK_EQ(f.md(), doc);
        CHECK(f.tidy());
    }

    // the x on a cell's tag: the cell goes and what it printed goes with it; one undo brings both back
    {
        Typist d;
        const QString doc = "before\n\n```python\nprint(1)\n```\n\n```output\n1\n```\n\nafter\n\n```bash\necho hi\n```\n";
        auto head = [&](int n) { return cellHeads(d.ed.document()).value(n); };
        d.ed.loadMarkdown(doc);
        d.ed.deleteCell(head(0));
        CHECK_EQ(d.md(), QString("before\n\nafter\n\n```bash\necho hi\n```\n"));
        CHECK(d.tidy());
        d.ed.undoStep();
        CHECK_EQ(d.md(), doc);
        CHECK(d.tidy());
        d.ed.deleteCell(head(1));                                                       // the last thing in the document
        CHECK_EQ(d.md(), QString("before\n\n```python\nprint(1)\n```\n\n```output\n1\n```\n\nafter\n"));
        CHECK(d.tidy());
        const QString first = "```python\nprint(1)\n```\n\n```output\n1\n```\n\n- after\n";
        d.ed.loadMarkdown(first);                                                       // the first thing
        d.ed.deleteCell(head(0));
        CHECK_EQ(d.md(), QString("- after\n"));
        CHECK(d.tidy());
        d.ed.loadMarkdown("```python\nprint(1)\n```\n\n```output\n1\n```\n");          // all there is
        d.ed.deleteCell(head(0));
        CHECK_EQ(d.md(), QString());
        d.ed.loadMarkdown("text\n\n<!--cell python\nprint(1)\n-->\n\n```output\n1\n```\n\nmore\n");    // one whose code is hidden
        d.ed.deleteCell(head(0));
        CHECK_EQ(d.md(), QString("text\n\nmore\n"));
        CHECK(d.tidy());
        d.ed.loadMarkdown("```python\nimport time\ntime.sleep(30)\n```\n\n```python\nprint(2)\n```\n");   // while it runs
        d.ed.runAll();
        QTest::qWait(1500);
        d.ed.deleteCell(head(0));
        CHECK(d.settle(15000));
        CHECK_EQ(d.md(), QString("```python\nprint(2)\n```\n"));                         // nothing landed on its neighbour
        // by mouse: the x is the last thing on the tag
        d.ed.loadMarkdown(first);
        QApplication::processEvents();
        QTest::mouseClick(d.ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(d.ed.viewport()->width() - 82, 74));
        CHECK_EQ(d.md(), QString("- after\n"));
    }

    // a code cell offers to finish what is typed with what the document already holds, and nothing else
    {
        Typist d;
        d.ed.loadMarkdown("Use `git push origin master` to publish.\n\n| command | what |\n| --- | --- |\n"
                          "| git status --short | what changed |\n| docker ps | running |\n\n"
                          "```python\nmembers = ['ann', 'bob']\nmember_count = len(members)\n```\n\n```output\ngit secret\n```\n\nend\n");
        QTextCursor c = d.ed.textCursor();
        c.movePosition(QTextCursor::End);
        d.ed.setTextCursor(c);
        d.type("gi");
        CHECK(d.ed.completions().isEmpty());                // not in ordinary text
        d.type("\n```bash\ngit ");
        CHECK_EQ(d.ed.completions().join("|"), QString("git push origin master|git status --short"));   // not what a cell printed
        d.type("s");
        CHECK_EQ(d.ed.completions().join("|"), QString("git status --short"));
        QTest::keyClick(&d.ed, Qt::Key_Tab);                // Tab takes the first
        CHECK(d.md().contains("```bash\ngit status --short\n```"));
        d.type("\necho mem");
        CHECK_EQ(d.ed.completions().join("|"), QString("members|member_count"));        // names the document's code uses
        QWidget *list = QApplication::activePopupWidget();  // the list itself: the second offer picked with the arrows, then Enter
        CHECK(list);
        if (list) {
            QTest::keyClick(list, Qt::Key_Down);
            QTest::keyClick(list, Qt::Key_Down);
            QTest::keyClick(list, Qt::Key_Return);
        }
        CHECK(d.md().contains("```bash\ngit status --short\necho member_count\n```"));
        CHECK(!QApplication::activePopupWidget());
        d.type("\ndoc");
        CHECK_EQ(d.ed.completions().join("|"), QString("docker ps"));                   // from the table
        list = QApplication::activePopupWidget();
        CHECK(list);
        if (list)
            QTest::keyClick(list, Qt::Key_Return);          // nothing picked: Enter is the new line it always is
        CHECK(d.md().contains("echo member_count\ndoc\n\n```"));
        CHECK(!QApplication::activePopupWidget());
        d.type("doc");
        list = QApplication::activePopupWidget();
        if (list)
            QTest::keyClick(list, Qt::Key_Escape);          // Esc puts the list away and changes nothing
        CHECK(!QApplication::activePopupWidget());
        CHECK(d.md().contains("echo member_count\ndoc\ndoc\n```"));
        CHECK(d.tidy());
    }

    // undo walks back through consistent states only, and redo returns
    const QString full = t.md();
    int steps = 0;
    while (t.ed.document()->isUndoAvailable() && steps < 400) {
        t.ed.undoStep();
        ++steps;
        if (!t.tidy()) {
            CHECK(!"undo stopped in a half-formatted state");
            break;
        }
    }
    CHECK_EQ(t.md(), QString());
    while (t.ed.document()->isRedoAvailable() && steps-- > -400)
        t.ed.redoStep();
    CHECK_EQ(t.md(), full);
    CHECK(t.tidy());
}

// Prose and code do not bleed into each other; cells of one language stay separate.
static void test_editor_structure()
{
    Typist t;
    t.ed.loadMarkdown("Above\n\n```python\na = 1\n```\n\n```python\nb = 2\n```\n\nBelow\n");
    CHECK_EQ(cellHeads(t.ed.document()).size(), 2);
    CHECK(t.tidy());
    // Backspace at the start of "Below" must not pull it into the code
    QTextCursor c(t.ed.document()->lastBlock());
    t.ed.setTextCursor(c);
    t.key(Qt::Key_Backspace);
    CHECK_EQ(t.md(), QString("Above\n\n```python\na = 1\n```\n\n```python\nb = 2\n```\n\nBelow\n"));
    // Delete at the end of "Above" must not pull code into prose
    c = QTextCursor(t.ed.document()->firstBlock());
    c.movePosition(QTextCursor::EndOfBlock);
    t.ed.setTextCursor(c);
    t.key(Qt::Key_Delete);
    CHECK_EQ(t.md(), QString("Above\n\n```python\na = 1\n```\n\n```python\nb = 2\n```\n\nBelow\n"));
    // Tab indents code, Shift+Tab takes it back, typing in code is literal
    c = QTextCursor(t.ed.document()->findBlockByNumber(1));
    t.ed.setTextCursor(c);
    t.key(Qt::Key_Tab);
    t.type("**x** ");
    CHECK(t.md().contains("```python\n    **x** a = 1\n```"));
    t.key(Qt::Key_Backtab);
    CHECK(t.md().contains("```python\n**x** a = 1\n```"));

    // a table: Tab walks cells and adds a row at the end
    Typist tb;
    tb.type("Data");
    tb.ed.insertTable(2, 2);
    tb.type("a\tb\t1\t2\t3");
    CHECK_EQ(tb.md(), QString("Data\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n| 3 |  |\n"));
    tb.key(Qt::Key_Return, Qt::AltModifier);
    tb.type("after");
    CHECK(tb.md().endsWith("| 3 |  |\n\nafter\n"));
    CHECK(tb.tidy());

    // copy and paste between documents keeps everything
    Typist a, b;
    const QString sample = "# T\n\nText with **bold** and $x$.\n\n```python\nprint(1)\n```\n\n```output\n1\n```\n\n- [ ] item\n\n| h |\n| --- |\n| c |\n\nEnd\n";
    a.ed.loadMarkdown(sample);
    a.ed.selectAll();
    a.ed.copy();
    b.ed.paste();
    CHECK_EQ(b.md(), sample);
    CHECK(b.tidy());
    CHECK(QApplication::clipboard()->text().contains("print(1)"));
}

// Saving keeps pictures beside the document; pasting brings formatting and pictures in; export stands alone.
static void test_editor_files()
{
    QTemporaryDir dir;
    const QString path = dir.path() + "/notes.md";
    {
        Typist t;
        t.type("Plot\n```python\nimport matplotlib.pyplot as plt\nplt.plot([1, 2])");
        t.key(Qt::Key_Return, Qt::ControlModifier);
        CHECK(t.settle());
        QString error;
        CHECK(t.ed.save(path, &error));
        const QStringList first = QDir(dir.path() + "/notes.assets").entryList({"out-*.png"});
        CHECK_EQ(first.size(), 1);
        QFile f(path);
        CHECK(f.open(QIODevice::ReadOnly));
        const QString saved = QString::fromUtf8(f.readAll());
        f.close();          // (Windows will not replace a file that is open)
        CHECK(saved.contains("![output](notes.assets/" + first.value(0) + ")"));

        // run it again with a different picture: after saving and closing, the old one is gone
        QTextCursor c(t.ed.document()->findBlockByNumber(2));
        c.movePosition(QTextCursor::EndOfBlock);
        t.ed.setTextCursor(c);
        t.type("\nplt.title('again')");
        t.key(Qt::Key_Return, Qt::ControlModifier);
        CHECK(t.settle());
        CHECK_EQ(QDir(dir.path() + "/notes.assets").entryList({"out-*.png"}).size(), 2);
        CHECK(t.ed.save(path, &error));
        t.ed.store().tidy();
        const QStringList second = QDir(dir.path() + "/notes.assets").entryList({"out-*.png"});
        CHECK_EQ(second.size(), 1);
        CHECK(second != first);
    }
    {
        Typist t;       // opening it again shows the same document without running anything
        QFile f(path);
        CHECK(f.open(QIODevice::ReadOnly));
        const QString saved = QString::fromUtf8(f.readAll());
        t.ed.loadMarkdown(saved, path);
        CHECK_EQ(t.md(), saved);
        CHECK(!t.ed.document()->isModified());
        QTextCharFormat cf;
        bool found = false;
        for (const QTextBlock &b : blocksOf(*t.ed.document()))
            found = found || objectFormat(b, ObjFigure, &cf);
        CHECK(found && QFile::exists(t.ed.store().resolve(cf.stringProperty(PSrc))));

        const QString html = pageHtml(t.ed.document(), t.ed.store(), "Notes");
        CHECK(html.contains("<pre class=\"code\"><code>") && html.contains("src=\"data:image/png;base64,"));
        CHECK(html.contains("<title>Notes</title>") && !html.contains("notes.assets"));
    }

    // importing another Markdown file brings its content and its pictures in
    {
        QDir().mkpath(dir.path() + "/other/pics");
        QImage dot(8, 8, QImage::Format_RGB32);
        dot.fill(Qt::blue);
        CHECK(dot.save(dir.path() + "/other/pics/dot.png"));
        QFile other(dir.path() + "/other/readme.md");
        CHECK(other.open(QIODevice::WriteOnly));
        other.write("---\ntitle: x\n---\n\n# Readme\n\n- one\n- two\n\n![dot](pics/dot.png)\n\n```sh\nmake\n```\n");
        other.close();
        Typist t;
        t.type("Mine\n");
        CHECK(t.ed.importFile(other.fileName()));
        CHECK(t.md().startsWith("Mine\n\n# Readme\n\n- one\n- two\n\n![dot]("));
        CHECK(t.md().contains("```sh\nmake\n```") && !t.md().contains("title: x"));
        QTextCharFormat cf;
        bool found = false;
        for (const QTextBlock &b : blocksOf(*t.ed.document()))
            found = found || objectFormat(b, ObjFigure, &cf);
        CHECK(found && cf.stringProperty(PSrc) == "asset:dot.png" && QFile::exists(t.ed.store().resolve("asset:dot.png")));
        CHECK(t.tidy());
        t.ed.undoStep();
        CHECK_EQ(t.md(), QString("Mine\n"));

        // a file copied in the file manager and pasted (or dropped) is announced to the window
        QStringList got;
        QObject::connect(&t.ed, &Editor::filesDropped, [&](const QStringList &paths) { got = paths; });
        auto *files = new QMimeData;
        files->setUrls({QUrl::fromLocalFile(other.fileName())});
        QApplication::clipboard()->setMimeData(files);
        t.ed.paste();
        QCoreApplication::processEvents();
        CHECK_EQ(got.join(","), other.fileName());
    }

    // formatted text from another program arrives as structure, not styling
    Typist p;
    auto *web = new QMimeData;
    web->setHtml("<h2 style='color:red'>Title</h2><p>Some <b>bold</b> and <a href='http://x.y'>a link</a>.</p>"
                 "<ul><li>one</li><li>two</li></ul><pre>code line</pre>");
    QApplication::clipboard()->setMimeData(web);
    p.ed.paste();
    CHECK_EQ(p.md(), QString("## Title\n\nSome **bold** and [a link](http://x.y).\n\n- one\n- two\n\n```\ncode line\n```\n"));
    CHECK(p.tidy());

    // a screenshot on the clipboard becomes a picture in the document's folder
    Typist s;
    QImage shot(40, 30, QImage::Format_RGB32);
    shot.fill(Qt::red);
    auto *img = new QMimeData;
    img->setImageData(shot);
    QApplication::clipboard()->setMimeData(img);
    s.type("See ");
    s.ed.paste();
    CHECK(s.md().startsWith("See ![](") && s.md().contains("pasted-"));
    QTextCharFormat cf;
    CHECK(objectFormat(s.ed.document()->firstBlock(), ObjFigure, &cf));
    CHECK(QFile::exists(s.ed.store().resolve(cf.stringProperty(PSrc))));
}

// Spelling: a learned word is remembered, along with its plural and possessive.
static void test_spelling()
{
    Typist t;
    Renderer *r = t.ed.renderer();
    r->setSpellCheck(true);
    if (!r->spellCheck()) {
        std::printf("  (no dictionary on this machine: skipped)\n");
        return;
    }
    CHECK(!r->misspelled("document") && r->misspelled("docuemnt"));
    CHECK(r->suggestions("docuemnt").contains("document"));
    CHECK(r->misspelled("biquad") && r->misspelled("biquads"));
    r->learn("biquad");
    CHECK(!r->misspelled("biquad") && !r->misspelled("biquads") && !r->misspelled("biquad's"));
    CHECK(r->misspelled("biquadd"));
    r->learn("jsfxes");         // learned as a plural: the singular comes too
    CHECK(!r->misspelled("jsfxes") && !r->misspelled("jsfxe"));
    r->setSpellCheck(false);    // and it is all still there next time
    r->setSpellCheck(true);
    CHECK(!r->misspelled("biquads") && r->misspelled("docuemnt"));
    t.type("some biquads and a docuemnt");
    int start = 0, end = 0;
    QString word;
    CHECK(r->wordAt(t.ed.document()->firstBlock(), 22, &start, &end, &word) && word == "docuemnt");
    CHECK(!r->wordAt(t.ed.document()->firstBlock(), 7, &start, &end, &word));
}

// A PDF comes out of whatever prints pages on this machine: Jotter's own helper,
// or a browser run without a window.
static void test_pdf()
{
    Typist t;
    t.ed.loadMarkdown("# Title\n\nSome text.\n\n```python\nprint(1)\n```\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n\n"
                      "<!--font: EB Garamond-->\n");
    QTemporaryDir dir;
    for (const bool browser : {false, true}) {
        if (!canExportPdf(browser)) {
            std::printf("  (no %s on this machine: skipped)\n", browser ? "browser that prints" : "PDF printer");
            continue;
        }
        const QString path = dir.path() + (browser ? "/browser.pdf" : "/any.pdf");
        int result = -1;
        exportPdf(t.ed.document(), t.ed.store(), "Title", path, "a4", &t.ed, [&](bool ok) { result = ok; }, browser);
        QElapsedTimer timer;
        timer.start();
        while (result < 0 && timer.elapsed() < 90000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK_EQ(result, 1);
        QFile f(path);
        CHECK(f.open(QIODevice::ReadOnly));
        const QByteArray pdf = f.readAll();
        CHECK(pdf.startsWith("%PDF-") && pdf.contains("EBGaramond"));       // in the document's own face
    }
}

// Script mode: a screenplay's parts, as they are typed, kept in the file and exported.
static const char *kScript = R"MD(INT. KITCHEN - NIGHT

Rain on the window. MARA cooks.

MARA
(not looking up)
You're *late*.

BRYAN (O.S.)
Traffic.

.THE ROOF

!INT. is how a scene heading starts.

@MARA

> FADE OUT.

CUT TO:

# Act Two

@
Said by nobody.

<!--script-->
)MD";

static void test_script()
{
    // the file: Fountain's notation, read back as the same parts
    QTextDocument doc;
    const QString want = QString::fromUtf8(kScript);
    const QString got = roundtrip(want, &doc);
    if (got != want) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL script round trip differs\n");
        printDiff(want, got);
    }
    CHECK(isScript(&doc));
    CHECK_EQ(textFace(&doc), QString("Courier Prime"));
    const QVector<Kind> kinds = {Scene, Para, Character, Paren, Dialogue, Character, Dialogue, Scene, Para, Character,
                                 Transition, Transition, Heading, Dialogue};
    const QVector<QTextBlock> blocks = blocksOf(doc);
    CHECK_EQ(blocks.size(), kinds.size());
    for (int i = 0; i < qMin(blocks.size(), kinds.size()); ++i)
        CHECK_EQ(int(kindOf(blocks[i])), int(kinds[i]));
    CHECK_EQ(blocks.value(7).text(), QString("THE ROOF"));
    CHECK_EQ(blocks.value(8).text(), QString("INT. is how a scene heading starts."));
    CHECK_EQ(blocks.value(10).text(), QString("FADE OUT."));

    // lines of action under one another stay lines; small letters where capitals belong become capitals
    QTextDocument loose;
    loadMarkdown(&loose, "He walks.\nShe runs.\n\n@mara\nhi\n\n<!--script-->\n", &store());
    normalize(&loose, loose.firstBlock(), loose.lastBlock(), 15, 1);
    CHECK_EQ(MarkdownWriter(&loose, &store()).text(), QString("He walks.\\\nShe runs.\n\nMARA\nhi\n\n<!--script-->\n"));
    // ...and none of it is a script unless the file says so, or it goes into one
    QTextDocument plain;
    CHECK_EQ(roundtrip("MARA\nHello.\n", &plain), QString("MARA Hello.\n"));
    CHECK(!isScript(&plain));
    plain.clear();
    MarkdownReader(QTextCursor(&plain)).insert("@MARA\nHi.\n\n<!--script-->\n");
    CHECK_EQ(MarkdownWriter(&plain).text(), QString("MARA\n\nHi.\n"));

    // typing: Enter goes on to what comes next, Tab changes what the line is
    Typist t;
    t.ed.setScript(true);
    CHECK(t.ed.isScript());
    t.type("int. kitchen - night\nRain.\n\tmara\nYou're late.\n\tbryan\tquietly\nTraffic.\n\t\tcut to:\next. roof - day");
    CHECK_EQ(t.md(), QString("INT. KITCHEN - NIGHT\n\nRain.\n\nMARA\nYou're late.\n\nBRYAN\n(quietly)\nTraffic.\n\n"
                             "CUT TO:\n\nEXT. ROOF - DAY\n\n<!--script-->\n"));
    CHECK(t.tidy());
    CHECK_EQ(t.ed.face(), QString("Courier Prime"));
    {
        // sixty letters to the line; what is said sits inside that, who says it further in
        const QTextBlock scene = t.ed.document()->firstBlock(), who = scene.next().next(), said = who.next();
        const qreal letter = QFontMetricsF(t.ed.theme.bodyFont()).horizontalAdvance('M');
        const qreal width = t.ed.document()->textWidth() - 2 * t.ed.document()->documentMargin();
        CHECK(width >= 60 * letter && width < 61 * letter);
        CHECK(who.blockFormat().leftMargin() > said.blockFormat().leftMargin() && said.blockFormat().rightMargin() > 0);
        CHECK_EQ(int(who.blockFormat().bottomMargin()), 0);
        CHECK(said.blockFormat().bottomMargin() > 0);
    }

    // a name that has spoken is offered again
    t.type("\n\tm");
    CHECK_EQ(t.ed.completions().join('|'), QString("MARA"));
    t.key(Qt::Key_Tab);
    CHECK_EQ(t.ed.textCursor().block().text(), QString("MARA"));
    t.type("\nHi.");
    CHECK(t.md().endsWith("MARA\nHi.\n\n<!--script-->\n"));

    // on paper
    const QString html = pageHtml(t.ed.document(), t.ed.store(), "Script");
    CHECK(html.contains("<article class=\"script\">") && html.contains("<p class=\"scene\">INT. KITCHEN - NIGHT</p>"));
    CHECK(html.contains("<div class=\"speech\">\n<p class=\"character\">BRYAN</p>\n<p class=\"paren\">(quietly)</p>\n"
                        "<p class=\"dialogue\">Traffic.</p>\n</div>"));
    CHECK(html.contains("Courier Prime"));

    // the window: Script > Script Mode, and the parts in the style box and under Ctrl+1 to Ctrl+6
    {
        MainWindow w;
        w.resize(1100, 900);
        w.show();
        w.activateWindow();
        CHECK(QTest::qWaitForWindowActive(&w));
        auto *ed = w.findChild<Editor *>();
        ed->setFocus();
        QAction *mode = nullptr;
        for (QAction *a : w.findChildren<QAction *>())
            if (a->text() == "Script &Mode")
                mode = a;
        QComboBox *style = nullptr;
        for (QComboBox *box : w.findChildren<QComboBox *>())
            if (box->toolTip() == "Paragraph style")
                style = box;
        CHECK(mode && style);
        if (mode && style) {
            CHECK_EQ(style->itemText(0), QString("Body Text"));
            QTest::keyClick(ed, Qt::Key_3, Qt::ControlModifier);        // not in an ordinary document
            CHECK_EQ(int(ed->currentKind()), int(Para));
            mode->trigger();
            CHECK(ed->isScript() && mode->isChecked());
            CHECK_EQ(style->itemText(0), QString("Scene Heading"));
            CHECK_EQ(style->currentText(), QString("Action"));
            QTest::keyClick(ed, Qt::Key_3, Qt::ControlModifier);
            CHECK_EQ(int(ed->currentKind()), int(Character));
            CHECK_EQ(style->currentText(), QString("Character"));
            QTest::keyClicks(ed, "mara");
            QTest::keyClick(ed, Qt::Key_4, Qt::ControlModifier);
            CHECK_EQ(ed->textCursor().block().text(), QString("(MARA)"));
            QTest::keyClick(ed, Qt::Key_Z, Qt::ControlModifier);
            CHECK_EQ(ed->toMarkdown(), QString("@MARA\n\n<!--script-->\n"));
            mode->trigger();
            CHECK(!ed->isScript() && !mode->isChecked());
            CHECK_EQ(style->itemText(0), QString("Body Text"));
            CHECK_EQ(ed->toMarkdown(), QString("MARA\n"));
            ed->document()->setModified(false);
        }
    }

    // an ordinary document again, and back: each is one step
    const QString script = t.md();
    t.ed.setScript(false);
    CHECK(!t.ed.isScript() && !t.md().contains("<!--script-->") && t.md().contains("MARA\n\nYou're late."));
    CHECK(t.tidy());
    t.ed.undoStep();
    CHECK(t.ed.isScript());
    CHECK_EQ(t.md(), script);
    CHECK(t.tidy());
}

// The text face: fonts that are part of Jotter, chosen per document and kept in it.
static void test_fonts()
{
    for (const Face &face : faces()) {
        if (!face.builtIn())
            continue;
        Theme th;
        th.setFace(face);
        CHECK_EQ(th.body, face.family);
        CHECK(QFontInfo(th.bodyFont()).family().startsWith(face.family));       // (the system may have the family too)
        CHECK(QFontDatabase().styles(face.family).size() >= face.files.size());     // real bold and italic, not faked ones
        CHECK(th.leading > 0.6 && th.leading < 1.2);
    }
    CHECK(!faceNamed("Papyrus").builtIn() && !faceNamed("").builtIn() && !faceNamed("Default").builtIn());
    CHECK_EQ(faceNamed(" eb garamond ").name, QString("EB Garamond"));

    // choosing one is an edit: it shows, it is saved in the document, it can be undone
    Typist t;
    int changes = 0;
    QObject::connect(&t.ed, &Editor::faceChanged, [&] { ++changes; });
    t.type("Title\nSome text");
    const QString plain = "Title\n\nSome text\n";
    CHECK_EQ(t.md(), plain);
    t.ed.setFace("EB Garamond");
    CHECK_EQ(t.ed.face(), QString("EB Garamond"));
    CHECK_EQ(t.ed.document()->defaultFont().family(), QString("EB Garamond"));
    CHECK_EQ(t.md(), plain + "\n<!--font: EB Garamond-->\n");
    CHECK(t.tidy() && t.ed.document()->isModified());
    t.ed.undoStep();
    CHECK_EQ(t.ed.face(), QString("Default"));
    CHECK_EQ(t.md(), plain);
    CHECK(t.tidy());
    t.ed.redoStep();
    CHECK_EQ(t.ed.face(), QString("EB Garamond"));
    CHECK_EQ(t.md(), plain + "\n<!--font: EB Garamond-->\n");
    CHECK(t.tidy());
    CHECK_EQ(changes, 3);
    t.key(Qt::Key_Return);
    t.type("typed in it");
    CHECK(t.tidy());
    t.ed.setFace("Default");
    CHECK_EQ(t.md(), plain + "\ntyped in it\n");
    CHECK(t.tidy());

    // whatever the face, its lines are as far apart
    t.ed.loadMarkdown(QString("words ").repeated(80).trimmed() + "\n");
    QVector<qreal> pitches;
    for (const Face &face : faces()) {
        if (!face.builtIn())
            continue;
        t.ed.setFace(face.name);
        QApplication::processEvents();
        const QTextLayout *lines = t.ed.document()->firstBlock().layout();
        CHECK(lines->lineCount() > 1);
        if (lines->lineCount() > 1)
            pitches << lines->lineAt(1).y() - lines->lineAt(0).y();
        CHECK(t.tidy());
    }
    for (const qreal pitch : pitches)
        CHECK(qAbs(pitch - pitches.first()) < 1.0);

    // a document says which face it is in; one that does not is in the default
    const QString comic = "# T\n\ntext\n\n<!--font: Comic Relief-->\n";
    t.ed.loadMarkdown(comic);
    CHECK_EQ(t.ed.face(), QString("Comic Relief"));
    CHECK_EQ(t.md(), comic);
    CHECK(t.tidy() && !t.ed.document()->isModified() && !t.ed.document()->isUndoAvailable());
    t.ed.loadMarkdown("plain\n");
    CHECK_EQ(t.ed.face(), QString("Default"));
    CHECK_EQ(t.md(), QString("plain\n"));
    t.ed.loadMarkdown("text\n\n<!-- font:  roboto -->\n");       // written by hand
    CHECK_EQ(t.ed.face(), QString("Roboto"));
    CHECK_EQ(t.md(), QString("text\n\n<!--font: Roboto-->\n"));
    for (const QString &ending : {QString("- one\n- two"), QString("```python\nprint(1)\n```"), QString("<!--cell python\nprint(1)\n-->"),
                                  QString("> quoted"), QString("| a | b |\n| --- | --- |\n| 1 | 2 |"), QString("$$\nx^2\n$$")}) {
        const QString all = "start\n\n" + ending + "\n\n<!--font: Source Serif-->\n";     // whatever the document ends in
        t.ed.loadMarkdown(all);
        CHECK_EQ(t.ed.face(), QString("Source Serif"));
        CHECK_EQ(t.md(), all);
        CHECK(t.tidy());
    }
    for (const QString &other : {QString("```\n<!--font: Roboto-->\n```\n"),         // in a code block it is code
                                 QString("text\n\n<!--font: Papyrus-->\n")}) {       // a face Jotter does not have is left alone
        t.ed.loadMarkdown(other);
        CHECK_EQ(t.ed.face(), QString("Default"));
        CHECK_EQ(t.md(), other);
    }

    // another document's content comes in without its face
    QTemporaryDir dir;
    {
        QFile other(dir.path() + "/other.md");
        CHECK(other.open(QIODevice::WriteOnly));
        other.write("theirs\n\n<!--font: Comic Relief-->\n");
        other.close();
        t.ed.loadMarkdown("mine\n\n<!--font: Roboto-->\n");
        QTextCursor end(t.ed.document());
        end.movePosition(QTextCursor::End);
        t.ed.setTextCursor(end);
        t.key(Qt::Key_Return);
        CHECK(t.ed.importFile(other.fileName()));
        CHECK_EQ(t.ed.face(), QString("Roboto"));
        CHECK_EQ(t.md(), QString("mine\n\ntheirs\n\n<!--font: Roboto-->\n"));
    }

    // an exported page carries the face with it: the styles it uses, no more
    t.ed.loadMarkdown("plain words\n\n<!--font: EB Garamond-->\n");
    QString html = pageHtml(t.ed.document(), t.ed.store(), "T");
    CHECK_EQ(html.count("@font-face"), 1);
    CHECK(html.contains("--face: \"EB Garamond\", serif; --scale: 1.16;") && html.contains("url(data:font/ttf;base64,AAEAAA"));
    CHECK(!html.contains("&lt;!--font") && !html.contains("<!--font"));
    t.ed.loadMarkdown("# Head\n\nsome *slanted* words\n\n<!--font: Computer Modern-->\n");
    CHECK_EQ(pageHtml(t.ed.document(), t.ed.store(), "T").count("@font-face"), 3);
    t.ed.loadMarkdown("# A *slanted* head\n\n<!--font: Computer Modern-->\n");
    html = pageHtml(t.ed.document(), t.ed.store(), "T");
    CHECK_EQ(html.count("@font-face"), 4);
    CHECK(html.contains("font-family: \"CMU Serif\"; font-weight: 700; font-style: italic; src: url(data:font/otf;base64,T1RUTw"));
    t.ed.loadMarkdown("some ***heavy slanted*** words\n\n<!--font: Computer Modern-->\n");
    CHECK_EQ(pageHtml(t.ed.document(), t.ed.store(), "T").count("@font-face"), 4);
    t.ed.loadMarkdown("some *slanted* words\n\n<!--font: Comic Relief-->\n");       // a family without italics
    CHECK_EQ(pageHtml(t.ed.document(), t.ed.store(), "T").count("@font-face"), 1);
    t.ed.loadMarkdown("plain words\n");
    html = pageHtml(t.ed.document(), t.ed.store(), "T");
    CHECK(!html.contains("@font-face") && html.contains("--scale: 1;"));

    // in the window: the toolbar's box sets it, and a new document starts in the face picked last
    QSettings().remove("font");
    auto boxOf = [](MainWindow &w) {
        QComboBox *box = nullptr;
        for (QComboBox *b : w.findChildren<QComboBox *>())
            if (b->toolTip().startsWith("Font"))
                box = b;
        return box;
    };
    {
        MainWindow w;
        w.show();
        auto *ed = w.findChild<Editor *>();
        QComboBox *box = boxOf(w);
        CHECK(box && box->currentText() == "Default");
        if (box) {
            const int i = box->findText("Source Serif");
            box->setCurrentIndex(i);
            emit box->activated(i);
            CHECK_EQ(ed->face(), QString("Source Serif"));
            CHECK(w.isWindowModified());
            int checked = 0;
            for (QAction *a : w.findChildren<QAction *>())
                if (a->isChecked() && a->data().toString() == "Source Serif")
                    ++checked;
            CHECK_EQ(checked, 1);
            ed->undoStep();
            CHECK_EQ(box->currentText(), QString("Default"));
            CHECK(!w.isWindowModified());
        }
        w.close();
    }
    {
        MainWindow w;
        w.show();
        auto *ed = w.findChild<Editor *>();
        CHECK_EQ(ed->face(), QString("Source Serif"));
        CHECK(!ed->document()->isModified() && ed->document()->isEmpty());
        CHECK(boxOf(w) && boxOf(w)->currentText() == "Source Serif");
        QFile old(dir.path() + "/old.md");      // a document from before there was a choice stays as it was
        CHECK(old.open(QIODevice::WriteOnly));
        old.write("old words\n");
        old.close();
        w.open(old.fileName());
        CHECK_EQ(ed->face(), QString("Default"));
        w.open(dir.path() + "/new.md");         // not there yet: a new document
        CHECK_EQ(ed->face(), QString("Source Serif"));
        CHECK(!w.isWindowModified());
        w.close();
    }
    QSettings().remove("font");
}

// ---- window -----------------------------------------------------------------

// The whole window, driven through its shortcuts as a person would.
static void test_window()
{
    QTemporaryDir dir;
    const QString path = dir.path() + "/first.md";
    MainWindow w;
    w.resize(1100, 900);
    w.show();
    w.activateWindow();
    CHECK(QTest::qWaitForWindowActive(&w));
    w.open(path);       // does not exist yet: a new document with that name
    auto *ed = w.findChild<Editor *>();
    ed->setFocus();
    auto type = [&](const QString &text) {
        for (const QChar ch : text) {
            if (ch == '\n')
                QTest::keyClick(ed, Qt::Key_Return);
            else
                QTest::keyClicks(ed, QString(ch));
        }
    };
    type("Report");
    QTest::keyClick(ed, Qt::Key_1, Qt::ControlModifier | Qt::AltModifier);
    type("\nplain ");
    QTest::keyClick(ed, Qt::Key_B, Qt::ControlModifier);
    type("bold");
    QTest::keyClick(ed, Qt::Key_B, Qt::ControlModifier);
    type(" and ");
    QTest::keyClick(ed, Qt::Key_E, Qt::ControlModifier);
    type("code");
    QTest::keyClick(ed, Qt::Key_E, Qt::ControlModifier);
    type(" text\n");
    QTest::keyClick(ed, Qt::Key_C, Qt::ControlModifier | Qt::AltModifier);
    type("print(2 + 2)");
    QTest::keyClick(ed, Qt::Key_Return, Qt::ControlModifier);
    QElapsedTimer timer;
    timer.start();
    while (ed->cellBusy() && timer.elapsed() < 30000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::keyClick(ed, Qt::Key_Return, Qt::AltModifier);
    type("after the cell");
    QTest::keyClick(ed, Qt::Key_S, Qt::ControlModifier);

    const QString want = "# Report\n\nplain **bold** and `code` text\n\n```python\nprint(2 + 2)\n```\n\n```output\n4\n```\n\nafter the cell\n";
    QFile f(path);
    CHECK(f.open(QIODevice::ReadOnly));
    CHECK_EQ(QString::fromUtf8(f.readAll()), want);
    f.close();
    CHECK(!w.isWindowModified());
    CHECK(w.windowTitle().startsWith("first.md"));

    // dropping a document on a window with work in it imports it there
    {
        QFile extra(dir.path() + "/extra.md");
        CHECK(extra.open(QIODevice::WriteOnly));
        extra.write("## Extra\n\nmore\n");
        extra.close();
        auto *files = new QMimeData;
        files->setUrls({QUrl::fromLocalFile(extra.fileName())});
        QApplication::clipboard()->setMimeData(files);
        ed->paste();
        QCoreApplication::processEvents();
        CHECK_EQ(ed->toMarkdown(), want + "\n## Extra\n\nmore\n");
        CHECK(w.windowTitle().startsWith("first.md"));
        ed->undoStep();
        CHECK_EQ(ed->toMarkdown(), want);
        QTest::keyClick(ed, Qt::Key_S, Qt::ControlModifier);
    }

    // the toolbar's Hide All Code button, and back
    QAction *hideAll = nullptr;
    for (QAction *a : w.findChildren<QAction *>())
        if (a->text() == "Hide All Code")
            hideAll = a;
    CHECK(hideAll);
    if (hideAll) {
        hideAll->trigger();
        CHECK(ed->toMarkdown().contains("<!--cell python\nprint(2 + 2)\n-->") && !ed->hasVisibleCode());
        CHECK_EQ(hideAll->text(), QString("Show All Code"));
        hideAll->trigger();
        CHECK_EQ(ed->toMarkdown(), want);
        CHECK_EQ(hideAll->text(), QString("Hide All Code"));
        QTest::keyClick(ed, Qt::Key_S, Qt::ControlModifier);
    }

    // find and replace; Shift+Enter in the find box searches backwards, it does not run a cell
    QTest::keyClick(ed, Qt::Key_H, Qt::ControlModifier);
    QLineEdit *find = nullptr, *replace = nullptr;
    for (QLineEdit *e : w.findChildren<QLineEdit *>()) {
        if (e->placeholderText() == "Find")
            find = e;
        if (e->placeholderText() == "Replace with")
            replace = e;
    }
    CHECK(find && replace && find->isVisible());
    if (find && replace) {
        find->setFocus();
        QTest::keyClicks(find, "plain");
        QTest::keyClick(find, Qt::Key_Return);
        CHECK_EQ(ed->textCursor().selectedText(), QString("plain"));
        QTest::keyClick(find, Qt::Key_Return, Qt::ShiftModifier);
        CHECK(!ed->cellBusy());
        replace->setFocus();
        QTest::keyClicks(replace, "simple");
        QTest::keyClick(replace, Qt::Key_Return);
        CHECK(ed->toMarkdown().contains("simple **bold**"));
        QTest::keyClick(replace, Qt::Key_Escape);
        CHECK(!find->isVisible());
    }
    CHECK(w.isWindowModified());
    ed->setFocus();
    QTest::keyClick(ed, Qt::Key_Z, Qt::ControlModifier);
    CHECK_EQ(ed->toMarkdown(), want);
    QTest::keyClick(ed, Qt::Key_Equal, Qt::ControlModifier);
    CHECK(qAbs(ed->theme.zoom - 1.1) < 0.01);
    QTest::keyClick(ed, Qt::Key_0, Qt::ControlModifier);
    CHECK(qAbs(ed->theme.zoom - 1.0) < 0.01);
    CHECK_EQ(ed->toMarkdown(), want);       // zooming is not an edit

    // Cell > Environment Variables: what is typed there goes to the settings, not into the document
    QAction *variables = nullptr;
    for (QAction *a : w.findChildren<QAction *>())
        if (a->text().startsWith("Environment"))
            variables = a;
    CHECK(variables);
    if (variables) {
        bool shown = false;
        QTimer::singleShot(300, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            shown = true;
            dialog->findChild<QPlainTextEdit *>()->setPlainText("JOTTER_KEY=abc");
            dialog->accept();
        });
        variables->trigger();
        CHECK(shown);
        CHECK_EQ(QSettings().value("env").toString(), QString("JOTTER_KEY=abc"));
        CHECK_EQ(ed->toMarkdown(), want);
        QSettings().remove("env");
    }

    // the list of shortcuts down the left side: made from the menus, hidden and shown from View
    auto *shortcuts = w.findChild<QDockWidget *>("shortcuts");
    CHECK(shortcuts && shortcuts->isVisible());
    if (shortcuts) {
        auto *list = shortcuts->findChild<QTreeWidget *>();
        CHECK(!list->findItems("Run All", Qt::MatchExactly | Qt::MatchRecursive).isEmpty());
        CHECK(!list->findItems("Export as PDF", Qt::MatchExactly | Qt::MatchRecursive).isEmpty());
        CHECK(list->findItems("Clear Output", Qt::MatchExactly | Qt::MatchRecursive).isEmpty());     // has no key
        for (QAction *a : w.findChildren<QAction *>())
            if (a->text() == "&Keyboard Shortcuts") {
                CHECK(a->isChecked());
                a->trigger();
                CHECK(!shortcuts->isVisible() && !a->isChecked());
                a->trigger();
                CHECK(shortcuts->isVisible() && a->isChecked());
            }
    }

#ifndef Q_OS_WIN
    // Help > Check for Updates, against a release kept in files: an AppImage is swapped for the newer one
    {
        auto write = [&](const QString &name, const QByteArray &bytes) {
            QFile f(dir.path() + "/" + name);
            CHECK(f.open(QIODevice::WriteOnly));
            f.write(bytes);
        };
        QAction *update = nullptr;
        for (QAction *a : w.findChildren<QAction *>())
            if (a->text().startsWith("Check for &Updates"))
                update = a;
        CHECK(update);
        const QString image = dir.path() + "/Jotter.AppImage";
        write("Jotter.AppImage", "old");
        QFile::setPermissions(image, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        write("new", "the new one");
        auto release = [&](const QString &version) {
            write("latest.json", QString("{\"tag_name\": \"v%1\", \"html_url\": \"\", \"assets\": [{\"name\": \"Jotter-x86_64.AppImage\", "
                                         "\"browser_download_url\": \"%2\"}]}")
                                     .arg(version, QUrl::fromLocalFile(dir.path() + "/new").toString()).toUtf8());
        };
        qputenv("JOTTER_UPDATE_URL", QUrl::fromLocalFile(dir.path() + "/latest.json").toString().toUtf8());
        qputenv("APPIMAGE", image.toUtf8());
        auto contents = [&] {
            QFile f(image);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        if (update) {
            int boxes = 0;
            QTimer answer;                          // says yes to whatever is asked, counting the boxes
            QObject::connect(&answer, &QTimer::timeout, [&boxes] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    ++boxes;
                    (box->button(QMessageBox::Yes) ? box->button(QMessageBox::Yes) : box->button(QMessageBox::Ok))->click();
                }
            });
            answer.start(100);
            release(JOTTER_VERSION);                // the same version: one box saying so, nothing touched
            update->trigger();
            CHECK_EQ(boxes, 1);
            CHECK_EQ(contents(), QString("old"));
            boxes = 0;
            release("99.0");                        // a newer one: asked, fetched, swapped, told
            update->trigger();
            CHECK_EQ(boxes, 2);
            CHECK_EQ(contents(), QString("the new one"));
            CHECK(QFileInfo(image).isExecutable() && !QFile::exists(image + ".new"));
        }
        qunsetenv("JOTTER_UPDATE_URL");
        qunsetenv("APPIMAGE");
    }
#endif

    // the list of libraries down the right side: what this Python can import, found by typing, hidden from View
    auto *libraries = w.findChild<QDockWidget *>("libraries");
    CHECK(libraries && libraries->isVisible());
    if (libraries) {
        auto *list = libraries->findChild<QTreeWidget *>();
        auto has = [&](const QString &name) {
            const auto items = list->findItems(name, Qt::MatchExactly | Qt::MatchRecursive);
            return !items.isEmpty() && !items.first()->isHidden();
        };
        QElapsedTimer t;
        t.start();
        while (!has("numpy") && t.elapsed() < 30000)
            QTest::qWait(50);
        CHECK(has("numpy") && has("matplotlib") && has("sqlite3") && has("math"));
        CHECK(!list->findItems("numpy", Qt::MatchExactly | Qt::MatchRecursive).value(0)->text(1).isEmpty());   // its version
        libraries->findChild<QLineEdit *>()->setText("SQL");
        CHECK(has("sqlite3") && !has("numpy") && !has("math"));
        libraries->findChild<QLineEdit *>()->clear();
        CHECK(has("numpy"));
        for (QAction *a : w.findChildren<QAction *>())
            if (a->text() == "&Libraries") {
                a->trigger();
                CHECK(!libraries->isVisible() && !a->isChecked());
                a->trigger();
                CHECK(libraries->isVisible() && a->isChecked());
            }
    }

    // Insert > Database: file, table, columns and their kinds become a sql cell, which is run
    for (QAction *a : w.findChildren<QAction *>()) {
        if (!a->text().startsWith("&Database"))
            continue;
        bool shown = false;
        QTimer::singleShot(300, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            shown = true;
            CHECK_EQ(dialog->findChild<QLineEdit *>("database")->text(), QString("first.db"));
            auto *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
            CHECK(!ok->isEnabled());
            dialog->findChild<QLineEdit *>("table")->setText("order");
            auto *columns = dialog->findChild<QTableWidget *>();
            const QStringList names = {"item", "unit price", "qty", "placed"};
            const QVector<int> kinds = {0, 2, 1, 3};
            for (int i = 0; i < names.size(); ++i) {
                columns->setItem(i, 0, new QTableWidgetItem(names[i]));
                qobject_cast<QComboBox *>(columns->cellWidget(i, 1))->setCurrentIndex(kinds[i]);
            }
            CHECK(ok->isEnabled() && columns->rowCount() == 5);     // a free line was added
            dialog->accept();
        });
        a->trigger();
        CHECK(shown);
        const QString cell = "```sql\n.open first.db\ncreate table if not exists \"order\" (\n    item text,\n"
                             "    \"unit price\" real,\n    qty integer,\n    placed date\n);\nselect * from \"order\";\n```\n";
        CHECK(ed->toMarkdown().contains(cell));
        QElapsedTimer t;
        t.start();
        while (!ed->toMarkdown().contains("```output\nitem") && !ed->toMarkdown().contains("```error") && t.elapsed() < 30000)
            QTest::qWait(50);
        CHECK(ed->toMarkdown().contains("```output\nitem  unit price  qty  placed\n"));
        CHECK(QFile::exists(dir.path() + "/first.db"));
    }
    ed->document()->setModified(false);
    w.close();
}

// Formula and link dialogs, context menus, the language tag.
static void test_dialogs()
{
    Typist t;
    int missing = 0;
#ifdef Q_OS_WIN
    const QString shell = "powershell";     // the shell the language menu offers
#else
    const QString shell = "bash";
#endif
    t.type("Energy ");
    QTimer::singleShot(300, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) {
            ++missing;
            return;
        }
        dialog->findChild<QPlainTextEdit *>()->setPlainText("E = mc^2");
        QTimer::singleShot(400, dialog, &QDialog::accept);
    });
    t.ed.editMath();
    CHECK_EQ(t.md(), QString("Energy $E = mc^2$\n"));
    if (MathRenderer::available()) {
        MathRenderer::instance()->waitForAll(20000);
        CHECK(MathRenderer::instance()->get("E = mc^2", false).state == MathRenderer::Formula::Ready);
    }
    QTimer::singleShot(300, [&] {       // the cursor is right after the formula: this edits it
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) {
            ++missing;
            return;
        }
        CHECK_EQ(dialog->findChild<QPlainTextEdit *>()->toPlainText(), QString("E = mc^2"));
        dialog->findChild<QPlainTextEdit *>()->setPlainText("a^2 + b^2 = c^2");
        QTimer::singleShot(100, dialog, &QDialog::accept);
    });
    t.ed.editMath();
    CHECK_EQ(t.md(), QString("Energy $a^2 + b^2 = c^2$\n"));

    QTextCursor c = t.ed.textCursor();
    c.movePosition(QTextCursor::Start);
    c.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
    t.ed.setTextCursor(c);
    QTimer::singleShot(300, [&] {
        auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        if (!dialog) {
            ++missing;
            return;
        }
        dialog->setTextValue("https://example.com");
        dialog->accept();
    });
    t.ed.editLink();
    CHECK(t.md().startsWith("[Energy](https://example.com) "));

    t.ed.loadMarkdown("```python\nprint(1)\n```\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n\ntext wth typo\n");
    QApplication::processEvents();
    for (const QPoint &p : {QPoint(200, 130), QPoint(200, 230), QPoint(200, 320)}) {
        QTimer::singleShot(200, [&] {
            if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()))
                menu->close();
            else
                ++missing;
        });
        QContextMenuEvent event(QContextMenuEvent::Mouse, p, t.ed.viewport()->mapToGlobal(p));
        QApplication::sendEvent(t.ed.viewport(), &event);
    }
    QTimer::singleShot(300, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) {
            ++missing;
            return;
        }
        for (QAction *a : menu->actions()) {
            if (a->text() == shell) {
                a->trigger();
                break;
            }
        }
        menu->close();
    });
    QTest::mouseClick(t.ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(t.ed.viewport()->width() - 100, 74));
    CHECK(t.md().startsWith("```" + shell + "\nprint(1)\n```"));
    CHECK_EQ(missing, 0);
}

// ---- runner -----------------------------------------------------------------

// The storyboard: frames that belong to lines, kept in the file, drawn in from the panel.
static void test_storyboard()
{
    // every kind of line that can have a frame, written and read back byte for byte
    const QString all = "# Title\n\n<!--storyboard k3:10,20,30,40-->\n\nA paragraph.\n\n<!--storyboard-->\n\n"
                        "- one\n  <!--storyboard r6:1,2-->\n- two\n    1. deep\n       <!--storyboard b12:5,5,9,9 g3:0,0-->\n\n"
                        "> quoted\n> <!--storyboard y3:640,360-->\n>\n> more\n\n"
                        "```python\nprint(1)\n```\n\n```output\n1\n```\n\n<!--storyboard k3:1,1,2,2-->\n\n"
                        "<!--cell python\nx = 2\n-->\n\n<!--storyboard k3:3,3-->\n\n---\n\n<!--storyboard-->\n\n"
                        "<!--storyboard alone k6:7,7-->\n\nEnd\n";
    {
        Typist t;
        t.ed.loadMarkdown(all);
        CHECK_EQ(t.md(), all);
        if (t.md() != all)
            printDiff(all, t.md());
        const auto boards = t.ed.boards();
        CHECK_EQ(boards.size(), 9);
        if (boards.size() == 9) {
            CHECK_EQ(boards[0].block.text(), QString("Title"));
            CHECK_EQ(boards[0].drawing, QString("k3:10,20,30,40"));
            CHECK_EQ(boards[1].drawing, QString());
            CHECK_EQ(boards[2].block.text(), QString("one"));
            CHECK_EQ(boards[3].block.text(), QString("deep"));
            CHECK_EQ(boards[4].block.text(), QString("quoted"));
            CHECK_EQ(boards[5].block.text(), QString("print(1)"));
            CHECK(kindOf(boards[6].block) == Fold && kindOf(boards[7].block) == Rule);
            CHECK_EQ(boards[8].block.text(), QString());
        }
        CHECK(t.tidy());
        // what does not read as a stroke is left out, and nothing is drawn off the sheet
        CHECK_EQ(drawingText(parseDrawing("k3:1,2 nonsense z3:1,1 r0:1,1 b2:5 g4:900,-5,7,x k2:3,4,5,6")), QString("k3:1,2 k2:3,4,5,6"));
        CHECK_EQ(drawingText(parseDrawing("y9:900,900")), QString("y9:640,360"));
    }

    // a frame for the cursor's line; drawing in it is an edit, one stroke to an undo
    Typist t;
    t.type("First\nSecond");
    CHECK_EQ(t.ed.boardAtCursor(), -1);
    CHECK_EQ(t.ed.addBoard(), 0);
    CHECK_EQ(t.md(), QString("First\n\nSecond\n\n<!--storyboard-->\n"));
    t.ed.drawBoard(0, "k3:1,2,3,4");
    t.ed.drawBoard(0, "k3:1,2,3,4 r6:9,9");
    CHECK_EQ(t.md(), QString("First\n\nSecond\n\n<!--storyboard k3:1,2,3,4 r6:9,9-->\n"));
    CHECK(t.ed.document()->isModified());
    t.ed.undoStep();
    CHECK_EQ(t.ed.boards().value(0).drawing, QString("k3:1,2,3,4"));
    t.ed.undoStep();
    t.ed.undoStep();
    CHECK_EQ(t.md(), QString("First\n\nSecond\n"));
    CHECK_EQ(t.ed.boards().size(), 0);
    t.ed.redoStep();
    t.ed.redoStep();
    t.ed.redoStep();
    const QString second = "First\n\nSecond\n\n<!--storyboard k3:1,2,3,4 r6:9,9-->\n";
    CHECK_EQ(t.md(), second);
    CHECK_EQ(t.ed.boardAtCursor(), 0);

    // Enter after the line, or before it: the frame stays with the line's text and is there once
    t.type("\nThird");
    CHECK_EQ(t.md(), second + "\nThird\n");
    CHECK_EQ(t.ed.boards().size(), 1);
    CHECK_EQ(t.ed.boardAtCursor(), -1);
    QTextCursor c(t.ed.document()->findBlockByNumber(1));
    t.ed.setTextCursor(c);
    t.key(Qt::Key_Return);
    CHECK_EQ(t.ed.boards().size(), 1);
    CHECK_EQ(t.ed.boards().value(0).block.text(), QString("Second"));
    t.ed.undoStep();
    CHECK_EQ(t.md(), second + "\nThird\n");

    // making the line something else keeps its frame, and is still one step to undo
    c = QTextCursor(t.ed.document()->findBlockByNumber(1));
    t.ed.setTextCursor(c);
    t.ed.setBlockKind(Heading, 2);
    CHECK_EQ(t.md(), QString("First\n\n## Second\n\n<!--storyboard k3:1,2,3,4 r6:9,9-->\n\nThird\n"));
    t.ed.undoStep();
    CHECK_EQ(t.md(), second + "\nThird\n");
    t.ed.setTextCursor(QTextCursor(t.ed.document()->findBlockByNumber(1)));
    t.ed.toggleList(false);
    CHECK_EQ(t.md(), QString("First\n\n- Second\n  <!--storyboard k3:1,2,3,4 r6:9,9-->\n\nThird\n"));
    t.ed.toggleList(false);
    CHECK_EQ(t.md(), second + "\nThird\n");

    // joined to the line above, the frame is that line's now; the whole line deleted, it goes too (and comes back)
    t.ed.setTextCursor(QTextCursor(t.ed.document()->findBlockByNumber(1)));
    t.key(Qt::Key_Backspace);
    CHECK_EQ(t.md(), QString("FirstSecond\n\n<!--storyboard k3:1,2,3,4 r6:9,9-->\n\nThird\n"));
    t.ed.undoStep();
    CHECK_EQ(t.md(), second + "\nThird\n");
    t.ed.redoStep();
    CHECK_EQ(t.md(), QString("FirstSecond\n\n<!--storyboard k3:1,2,3,4 r6:9,9-->\n\nThird\n"));
    t.ed.undoStep();
    CHECK_EQ(t.md(), second + "\nThird\n");
    c = QTextCursor(t.ed.document()->findBlockByNumber(1));
    c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
    t.ed.setTextCursor(c);
    t.key(Qt::Key_Delete);
    CHECK_EQ(t.md(), QString("First\n\nThird\n"));
    CHECK_EQ(t.ed.boards().size(), 0);
    t.ed.undoStep();
    CHECK_EQ(t.md(), second + "\nThird\n");
    CHECK_EQ(t.ed.boards().size(), 1);

    // cut and pasted, the line takes its frame along
    c = QTextCursor(t.ed.document()->findBlockByNumber(1));
    c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
    t.ed.setTextCursor(c);
    t.ed.cut();
    CHECK_EQ(t.ed.boards().size(), 0);
    t.ed.setTextCursor(QTextCursor(t.ed.document()->firstBlock()));
    t.ed.paste();
    CHECK_EQ(t.md(), QString("Second\n\n<!--storyboard k3:1,2,3,4 r6:9,9-->\n\nFirst\n\nThird\n"));
    CHECK_EQ(t.ed.boards().size(), 1);
    CHECK(t.tidy());

    // a frame on a line that is still empty stays through what is typed there, Markdown habits included
    Typist e;
    CHECK_EQ(e.ed.addBoard(), 0);
    CHECK_EQ(e.md(), QString("<!--storyboard alone-->\n"));
    e.ed.drawBoard(0, "g3:5,5");
    e.type("# Scene one");
    CHECK_EQ(e.md(), QString("# Scene one\n\n<!--storyboard g3:5,5-->\n"));
    e.type("\n");
    e.ed.addBoard();
    e.type("- wide shot\n");
    e.ed.addBoard();
    e.type("close up");
    CHECK_EQ(e.md(), QString("# Scene one\n\n<!--storyboard g3:5,5-->\n\n- wide shot\n  <!--storyboard-->\n- close up\n  <!--storyboard-->\n"));
    CHECK_EQ(e.ed.boards().size(), 3);
    CHECK(e.tidy());
    Typist c2;
    c2.type("Before\n");
    c2.ed.addBoard();
    c2.ed.drawBoard(0, "y3:2,2");
    c2.type("```python\nx = 1");
    CHECK_EQ(c2.md(), QString("Before\n\n```python\nx = 1\n```\n\n<!--storyboard y3:2,2-->\n"));
    c2.ed.deleteCell(c2.ed.document()->findBlockByNumber(1));    // the x on a cell takes its frame with it
    CHECK_EQ(c2.md(), QString("Before\n"));
    CHECK_EQ(c2.ed.boards().size(), 0);
    c2.ed.undoStep();
    CHECK_EQ(c2.md(), QString("Before\n\n```python\nx = 1\n```\n\n<!--storyboard y3:2,2-->\n"));
    CHECK_EQ(c2.ed.boards().size(), 1);
    CHECK(c2.tidy());

    // in a script a speech has one frame, on who speaks, whichever of its lines the cursor is on
    {
        const QString script = "INT. KITCHEN - NIGHT\n\n<!--storyboard k3:1,1-->\n\nRain on the window.\n\n"
                               "MARA\n(not looking up)\nYou're late.\n\n<!--storyboard r3:2,2-->\n\n"
                               "BRYAN\nTraffic.\n\nCUT TO:\n\n<!--storyboard b3:3,3-->\n\n<!--script-->\n";
        Typist s;
        s.ed.loadMarkdown(script);
        CHECK_EQ(s.md(), script);
        CHECK_EQ(s.ed.boards().size(), 3);
        CHECK_EQ(s.ed.boards().value(1).block.text(), QString("MARA"));
        CHECK(kindOf(s.ed.boards().value(2).block) == Transition);
        s.ed.setTextCursor(QTextCursor(s.ed.document()->findBlockByNumber(4)));     // "You're late."
        CHECK_EQ(s.ed.boardAtCursor(), 1);
        s.ed.setTextCursor(QTextCursor(s.ed.document()->findBlockByNumber(6)));     // "Traffic."
        CHECK_EQ(s.ed.boardAtCursor(), -1);
        CHECK_EQ(s.ed.addBoard(), 2);
        CHECK_EQ(s.ed.boards().value(2).block.text(), QString("BRYAN"));
        s.ed.drawBoard(2, "g3:4,4");
        QString framed = script;
        framed.replace("BRYAN\nTraffic.\n\n", "BRYAN\nTraffic.\n\n<!--storyboard g3:4,4-->\n\n");
        CHECK_EQ(s.md(), framed);
        s.key(Qt::Key_End);
        s.type("\nHe shrugs.");
        CHECK_EQ(s.ed.boards().size(), 4);
        CHECK_EQ(s.ed.boards().value(2).block.text(), QString("BRYAN"));
        Typist again;
        again.ed.loadMarkdown(s.md());
        CHECK_EQ(again.md(), s.md());
        CHECK_EQ(again.ed.boards().size(), 4);
        CHECK(s.tidy());
    }

    // a cell has one frame, whichever of its lines the cursor is on, shown or hidden
    Typist k;
    k.ed.loadMarkdown("```python\na = 1\nb = 2\n```\n\n```output\n3\n```\n\n| h |\n| --- |\n| c |\n");
    k.ed.setTextCursor(QTextCursor(k.ed.document()->findBlockByNumber(1)));
    CHECK_EQ(k.ed.addBoard(), 0);
    k.ed.drawBoard(0, "b3:8,8");
    const QString cell = "```python\na = 1\nb = 2\n```\n\n```output\n3\n```\n\n<!--storyboard b3:8,8-->\n\n| h |\n| --- |\n| c |\n";
    CHECK_EQ(k.md(), cell);
    k.ed.setTextCursor(QTextCursor(k.ed.document()->findBlockByNumber(2)));
    CHECK_EQ(k.ed.boardAtCursor(), 0);       // from its output, too
    k.ed.setTextCursor(QTextCursor(k.ed.document()->findBlockByNumber(0)));
    k.key(Qt::Key_End);
    k.type("\nc = 3");
    CHECK_EQ(k.ed.boards().size(), 1);
    CHECK_EQ(k.ed.boards().value(0).block.blockNumber(), 0);
    k.ed.undoStep();
    k.ed.undoStep();
    CHECK_EQ(k.md(), cell);
    k.ed.toggleFold();
    CHECK(k.md().contains("<!--cell python\na = 1\nb = 2\n-->\n\n```output\n3\n```\n\n<!--storyboard b3:8,8-->"));
    CHECK_EQ(k.ed.boards().size(), 1);
    k.ed.toggleFold();
    CHECK_EQ(k.md(), cell);
    CHECK_EQ(k.ed.boards().size(), 1);
    QTextCursor inTable(k.ed.document());
    inTable.movePosition(QTextCursor::End);
    inTable.movePosition(QTextCursor::PreviousBlock);
    while (!inTable.currentTable() && inTable.movePosition(QTextCursor::PreviousBlock)) {}
    k.ed.setTextCursor(inTable);
    CHECK(inTable.currentTable());
    CHECK_EQ(k.ed.addBoard(), -1);          // not in a table
    CHECK_EQ(k.md(), cell);
    CHECK(k.tidy());

    // the panel: shown from View, a frame from Insert, drawn in with the mouse
    QTemporaryDir dir;
    const QString path = dir.path() + "/board.md";
    MainWindow w;
    w.resize(1500, 900);
    w.show();
    w.activateWindow();
    CHECK(QTest::qWaitForWindowActive(&w));
    w.open(path);
    auto *ed = w.findChild<Editor *>();
    auto *dock = w.findChild<QDockWidget *>("storyboard");
    auto *panel = w.findChild<Storyboard *>();
    CHECK(dock && panel && !dock->isVisible());
    if (!dock || !panel)
        return;
    QAction *toggle = nullptr, *frame = nullptr;
    for (QAction *a : w.findChildren<QAction *>()) {
        if (a->text() == "Story&board")
            toggle = a;
        if (a->text() == "&Storyboard Frame")
            frame = a;
    }
    CHECK(toggle && frame && !toggle->isChecked());
    if (!toggle || !frame)
        return;
    toggle->trigger();
    CHECK(dock->isVisible() && toggle->isChecked());
    toggle->trigger();
    CHECK(!dock->isVisible() && !toggle->isChecked());

    ed->setFocus();
    QTest::keyClicks(ed, "The door opens.");
    QTest::keyClick(ed, Qt::Key_Return);
    QTest::keyClicks(ed, "Nobody is there.");
    QTest::keyClick(ed, Qt::Key_F, Qt::ControlModifier | Qt::AltModifier);     // Insert > Storyboard Frame
    QCoreApplication::processEvents();
    CHECK(dock->isVisible() && toggle->isChecked());
    CHECK_EQ(ed->boards().size(), 1);
    CHECK_EQ(ed->boards().value(0).block.text(), QString("Nobody is there."));
    CHECK(ed->hasFocus());                  // the keys stay with the page
    CHECK(!panel->findChild<QToolButton *>("add")->isEnabled());       // this line has its frame

    QWidget *sheet = panel->sheet();
    auto mouse = [&](QEvent::Type type, const QPoint &pos) {
        QMouseEvent event(type, pos, sheet->mapToGlobal(pos), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(sheet, &event);
    };
    auto drag = [&](const QPoint &from, const QPoint &to) {
        mouse(QEvent::MouseButtonPress, from);
        for (int i = 1; i <= 10; ++i)
            mouse(QEvent::MouseMove, from + (to - from) * i / 10);
        mouse(QEvent::MouseButtonRelease, to);
    };
    const QRect area = panel->canvas(0);
    CHECK(area.width() > 150 && qAbs(area.height() * 16 - area.width() * 9) <= 16);
    drag(area.topLeft() + QPoint(20, 20), area.topLeft() + QPoint(120, 20));
    Drawing drawn = parseDrawing(ed->boards().value(0).drawing);
    CHECK_EQ(drawn.size(), 1);
    CHECK(drawn.value(0).ink == 'k' && drawn.value(0).width == 6 && drawn.value(0).points.size() > 3);
    QTest::mouseClick(panel->findChild<QToolButton *>("ink-r"), Qt::LeftButton);
    QTest::mouseClick(panel->findChild<QToolButton *>("width-12"), Qt::LeftButton);
    drag(area.topLeft() + QPoint(20, 80), area.topLeft() + QPoint(120, 80));
    mouse(QEvent::MouseButtonPress, area.center());         // a dot
    mouse(QEvent::MouseButtonRelease, area.center());
    drawn = parseDrawing(ed->boards().value(0).drawing);
    CHECK_EQ(drawn.size(), 3);
    CHECK(drawn.value(1).ink == 'r' && drawn.value(1).width == 12 && drawn.value(2).points.size() == 1);
    CHECK(w.isWindowModified());

    // the eraser takes out the strokes it is dragged across; Ctrl+Z brings them back
    QTest::mouseClick(panel->findChild<QToolButton *>("eraser"), Qt::LeftButton);
    drag(area.topLeft() + QPoint(70, 5), area.topLeft() + QPoint(70, 40));
    drawn = parseDrawing(ed->boards().value(0).drawing);
    CHECK_EQ(drawn.size(), 2);
    CHECK(drawn.value(0).ink == 'r');
    QTest::keyClick(ed, Qt::Key_Z, Qt::ControlModifier);
    CHECK_EQ(parseDrawing(ed->boards().value(0).drawing).size(), 3);

    // a second frame; a click on the text above a frame goes to its line
    QTextCursor top(ed->document()->firstBlock());
    ed->setTextCursor(top);
    CHECK(panel->findChild<QToolButton *>("add")->isEnabled());
    QTest::mouseClick(panel->findChild<QToolButton *>("add"), Qt::LeftButton);
    QCoreApplication::processEvents();
    CHECK_EQ(ed->boards().size(), 2);
    CHECK_EQ(panel->current(), 0);
    mouse(QEvent::MouseButtonPress, panel->caption(1).center());
    CHECK_EQ(ed->textCursor().block().text(), QString("Nobody is there."));
    CHECK_EQ(panel->current(), 1);

    // saved with the document, and there when it is opened again
    QTest::keyClick(ed, Qt::Key_S, Qt::ControlModifier);
    QFile f(path);
    CHECK(f.open(QIODevice::ReadOnly));
    const QString saved = QString::fromUtf8(f.readAll());
    f.close();
    CHECK(saved.startsWith("The door opens.\n\n<!--storyboard-->\n\nNobody is there.\n\n<!--storyboard k6:"));
    CHECK(!w.isWindowModified());
    w.open(path);
    CHECK_EQ(ed->boards().size(), 2);
    CHECK_EQ(ed->toMarkdown(), saved);

    // the small picture in the page's margin brings the panel back; the x takes a frame away
    dock->hide();
    const QTextBlock line = ed->boards().value(1).block;
    const QRectF where = ed->document()->documentLayout()->blockBoundingRect(line);
    const QPoint mark(qRound(ed->document()->documentLayout()->documentSize().width() - PageMargin / 2.0),
                      qRound(where.top()) + 10 - ed->verticalScrollBar()->value());
    QTest::mouseClick(ed->viewport(), Qt::LeftButton, Qt::NoModifier, mark);
    CHECK(dock->isVisible());
    QCoreApplication::processEvents();
    mouse(QEvent::MouseButtonPress, panel->closeBox(0).center());
    CHECK_EQ(ed->boards().size(), 1);
    CHECK(ed->toMarkdown().startsWith("The door opens.\n\nNobody is there.\n\n<!--storyboard k6:"));
    ed->undoStep();
    CHECK_EQ(ed->toMarkdown(), saved);
    ed->document()->setModified(false);     // (nothing to ask about when the window closes)
}

int main(int argc, char **argv)
{
    QTemporaryDir home;         // tests never touch the real settings, caches or recovery files
#ifdef Q_OS_WIN
    // Windows has no variables to point those elsewhere: Qt's test mode gives
    // them folders of their own, which start out empty here. (And real windows:
    // Qt's off-screen platform cannot show a message box there.)
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
#else
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());
#endif
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("jotter");
    QCoreApplication::setApplicationName("jotter-tests");
#ifdef Q_OS_WIN
    for (const auto where : {QStandardPaths::GenericDataLocation, QStandardPaths::GenericCacheLocation, QStandardPaths::AppConfigLocation}) {
        const QString dir = QStandardPaths::writableLocation(where);
        if (dir.contains("qttest"))
            QDir(dir).removeRecursively();
    }
#endif
    const QString filter = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const QVector<QPair<QString, std::function<void()>>> tests = {
        {"markdown_canonical", test_markdown_canonical},
        {"markdown_foreign", test_markdown_foreign},
        {"markdown_pretty", test_markdown_pretty},
        {"markdown_inline_random", test_markdown_inline_random},
        {"kernel", test_kernel},
        {"editor_typing", test_editor_typing},
        {"editor_cells", test_editor_cells},
        {"editor_structure", test_editor_structure},
        {"editor_files", test_editor_files},
        {"spelling", test_spelling},
        {"pdf", test_pdf},
        {"script", test_script},
        {"fonts", test_fonts},
        {"window", test_window},
        {"dialogs", test_dialogs},
        {"storyboard", test_storyboard},
    };
    int ran = 0;
    for (const auto &t : tests) {
        if (!filter.isEmpty() && !t.first.contains(filter))
            continue;
        const int before = g_failures;
        std::printf("%s\n", qPrintable(t.first));
        std::fflush(stdout);
        t.second();
        if (g_failures == before)
            ++ran;
    }
    std::printf("%s (%d passed, %d failed checks)\n", g_failures ? "FAILED" : "ok", ran, g_failures);
    return g_failures ? 1 : 0;
}
