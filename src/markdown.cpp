#include "markdown.h"

#include "assets.h"
#include "theme.h"

#include <QDir>
#include <QHash>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextList>
#include <QUrl>
#include <functional>
#include <md4c.h>

namespace jot {

using RE = QRegularExpression;

bool Atom::operator==(const Atom &o) const
{
    return type == o.type && src == o.src && alt == o.alt && qRound(width) == qRound(o.width) && display == o.display;
}

bool Run::operator==(const Run &o) const
{
    return text == o.text && styles == o.styles && href == o.href && atom == o.atom;
}

// =============================================================================
// Parsing: md4c's event stream -> a flat list of blocks with inline runs
// =============================================================================

namespace {

QString decodeEntity(const QString &entity)
{
    static QHash<QString, QString> cache;
    const auto hit = cache.constFind(entity);
    if (hit != cache.constEnd())
        return *hit;
    QString out;
    if (entity.startsWith(QLatin1String("&#"))) {
        const QString num = entity.mid(2, entity.size() - 3);
        bool ok = false;
        const uint cp = num.startsWith('x', Qt::CaseInsensitive) ? num.mid(1).toUInt(&ok, 16) : num.toUInt(&ok, 10);
        if (ok && cp > 0 && cp <= 0x10FFFF)
            out = QString::fromUcs4(&cp, 1);
    } else {
        QTextDocument d;        // Qt knows the named entities
        d.setHtml(entity);
        out = d.toRawText();
    }
    if (out.isEmpty())
        out = entity;
    cache.insert(entity, out);
    return out;
}

QString attrEscape(QString s)
{
    return s.replace('&', "&amp;").replace('"', "&quot;").replace('<', "&lt;").replace('>', "&gt;");
}

QString attrUnescape(QString s)
{
    return s.replace("&quot;", "\"").replace("&#39;", "'").replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&");
}

QHash<QString, QString> htmlAttrs(const QString &text)
{
    static const RE rx(QStringLiteral(R"RX(([A-Za-z_:][\w:.-]*)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'>]+)))RX"));
    QHash<QString, QString> out;
    auto it = rx.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString value = !m.captured(2).isNull() ? m.captured(2) : !m.captured(3).isNull() ? m.captured(3) : m.captured(4);
        out.insert(m.captured(1).toLower(), attrUnescape(value));
    }
    return out;
}

// Local paths are percent-decoded for the document and minimally re-encoded
// for the file; real URLs are left exactly as written.
QString decodeSrc(const QString &src)
{
    return hasScheme(src) ? src : QUrl::fromPercentEncoding(src.toUtf8());
}

QString encodeSrc(const QString &src)
{
    if (hasScheme(src))
        return src;
    QString out;
    for (const QChar c : src) {
        switch (c.unicode()) {
        case ' ': out += "%20"; break;
        case '(': out += "%28"; break;
        case ')': out += "%29"; break;
        case '<': out += "%3C"; break;
        case '>': out += "%3E"; break;
        case '\\': out += "%5C"; break;
        case '%': out += "%25"; break;
        case '"': out += "%22"; break;
        default: out += c;
        }
    }
    return out;
}

Atom imgAtom(const QString &attrText)
{
    static const RE num(QStringLiteral(R"(^\s*(\d+(?:\.\d+)?)\s*(?:px)?\s*$)"));
    const auto a = htmlAttrs(attrText);
    Atom atom;
    atom.type = Atom::Image;
    atom.src = decodeSrc(a.value("src"));
    atom.alt = a.value("alt");
    const auto m = num.match(a.value("width"));
    if (m.hasMatch())
        atom.width = m.captured(1).toDouble();
    return atom;
}

bool htmlStyle(const QString &tag, Style *style)
{
    static const QHash<QString, Style> map = {
        {"b", Bold}, {"strong", Bold}, {"i", Italic}, {"em", Italic}, {"s", Strike}, {"del", Strike},
        {"strike", Strike}, {"u", Underline}, {"ins", Underline}, {"code", CodeSpan}, {"tt", CodeSpan},
        {"kbd", CodeSpan}};
    const auto it = map.constFind(tag);
    if (it == map.constEnd())
        return false;
    *style = *it;
    return true;
}

QString attribute(const MD_ATTRIBUTE &a)
{
    QString out;
    if (!a.text || !a.size)
        return out;
    for (int i = 0; a.substr_offsets[i] < a.size; ++i) {
        const MD_OFFSET off = a.substr_offsets[i];
        const QString piece = QString::fromUtf8(a.text + off, int(a.substr_offsets[i + 1] - off));
        if (a.substr_types[i] == MD_TEXT_ENTITY)
            out += decodeEntity(piece);
        else if (a.substr_types[i] == MD_TEXT_NULLCHAR)
            out += QChar(QChar::ReplacementCharacter);
        else
            out += piece;
    }
    return out;
}

struct Sax {
    QVector<MdBlock> out;

    // The leaf block being read (paragraph, heading, table cell) and its inline state.
    bool leaf = false, implicit = false, cell = false;
    MdBlock::Type leafType = MdBlock::Paragraph;
    int leafLevel = 0;
    Runs runs;
    struct Entry { Style style; bool link; QString href; };
    QVector<Entry> stack;
    QString html;               // raw inline HTML not yet interpreted
    int capture = 0;            // collecting text for: 1 an image's alt, 2 a formula
    int captureDepth = 0;
    Atom atom;
    QString captured;

    bool raw = false;           // inside a code or HTML block
    MdBlock rawBlock, tableBlock;
    QVector<Runs> row;

    Styles styles() const
    {
        Styles s;
        for (const Entry &e : stack)
            if (!e.link)
                s |= e.style;
        return s;
    }
    QString href() const
    {
        for (int i = stack.size() - 1; i >= 0; --i)
            if (stack[i].link)
                return stack[i].href;
        return QString();
    }
    void addText(QString t, Styles extra = Styles())
    {
        if (t.isEmpty())
            return;
        t.replace('\n', ' ');
        runs.append(Run{t, styles() | extra, href(), Atom()});
    }
    void addAtom(const Atom &a)
    {
        Run r;
        r.href = href();
        r.atom = a;
        runs.append(r);
    }
    void pop(Style style, bool link)
    {
        for (int i = stack.size() - 1; i >= 0; --i) {
            if (stack[i].link == link && (link || stack[i].style == style)) {
                stack.remove(i);
                return;
            }
        }
    }
    void push(MdBlock::Type type)
    {
        MdBlock b;
        b.type = type;
        out.append(b);
    }
    void openLeaf(MdBlock::Type type, int level, bool isCell, bool isImplicit)
    {
        leaf = true;
        leafType = type;
        leafLevel = level;
        cell = isCell;
        implicit = isImplicit;
        runs.clear();
        stack.clear();
    }
    void closeLeaf()
    {
        flushHtml();
        if (cell) {
            row.append(runs);
        } else {
            MdBlock b;
            b.type = leafType;
            b.level = leafLevel;
            b.runs = runs;
            out.append(b);
        }
        leaf = false;
        runs.clear();
        stack.clear();
    }
    // md4c gives the text of a tight list item without a paragraph around it.
    void ensureLeaf()
    {
        if (!leaf)
            openLeaf(MdBlock::Paragraph, 0, false, true);
    }
    void closeImplicit()
    {
        if (leaf && implicit)
            closeLeaf();
    }
    void flushHtml()
    {
        if (html.isEmpty())
            return;
        static const RE piece(QStringLiteral("<[^>]*>|[^<]+|<"), RE::DotMatchesEverythingOption);
        static const RE tag(QStringLiteral(R"(^<(/?)([A-Za-z][A-Za-z0-9]*)\b([^>]*?)/?>$)"), RE::DotMatchesEverythingOption);
        const QString buffer = html;
        html.clear();
        auto it = piece.globalMatch(buffer);
        while (it.hasNext()) {
            const QString p = it.next().captured(0);
            const auto m = tag.match(p);
            const QString name = m.hasMatch() ? m.captured(2).toLower() : QString();
            const bool closing = m.hasMatch() && !m.captured(1).isEmpty();
            Style style;
            if (name == "br") {
                addText(QString(QChar::LineSeparator));
            } else if (name == "img" && !closing) {
                addAtom(imgAtom(m.captured(3)));
            } else if (htmlStyle(name, &style)) {
                if (closing)
                    pop(style, false);
                else
                    stack.append({style, false, QString()});
            } else if (name == "a" && closing) {
                pop(Bold, true);
            } else if (name == "a" && htmlAttrs(m.captured(3)).contains("href")) {
                stack.append({Bold, true, htmlAttrs(m.captured(3)).value("href")});
            } else {
                addText(p, RawSpan);    // unknown HTML: keep it, verbatim
            }
        }
    }
};

int enterBlock(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    Sax &s = *static_cast<Sax *>(userdata);
    s.closeImplicit();
    switch (type) {
    case MD_BLOCK_QUOTE: s.push(MdBlock::QuoteOpen); break;
    case MD_BLOCK_UL: s.push(MdBlock::ListOpen); break;
    case MD_BLOCK_OL:
        s.push(MdBlock::ListOpen);
        s.out.last().ordered = true;
        break;
    case MD_BLOCK_LI: {
        const auto *d = static_cast<MD_BLOCK_LI_DETAIL *>(detail);
        s.push(MdBlock::ItemOpen);
        s.out.last().task = !d->is_task ? 0 : (d->task_mark == 'x' || d->task_mark == 'X') ? 2 : 1;
        break;
    }
    case MD_BLOCK_HR: s.push(MdBlock::Rule); break;
    case MD_BLOCK_H: s.openLeaf(MdBlock::Heading, int(static_cast<MD_BLOCK_H_DETAIL *>(detail)->level), false, false); break;
    case MD_BLOCK_P: s.openLeaf(MdBlock::Paragraph, 0, false, false); break;
    case MD_BLOCK_CODE:
        s.raw = true;
        s.rawBlock = MdBlock();
        s.rawBlock.type = MdBlock::CodeBlock;
        s.rawBlock.info = attribute(static_cast<MD_BLOCK_CODE_DETAIL *>(detail)->info);
        break;
    case MD_BLOCK_HTML:
        s.raw = true;
        s.rawBlock = MdBlock();
        s.rawBlock.type = MdBlock::HtmlBlock;
        break;
    case MD_BLOCK_TABLE:
        s.tableBlock = MdBlock();
        s.tableBlock.type = MdBlock::Table;
        break;
    case MD_BLOCK_TR: s.row.clear(); break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD: s.openLeaf(MdBlock::Paragraph, 0, true, false); break;
    default: break;
    }
    return 0;
}

int leaveBlock(MD_BLOCKTYPE type, void *, void *userdata)
{
    Sax &s = *static_cast<Sax *>(userdata);
    s.closeImplicit();
    switch (type) {
    case MD_BLOCK_QUOTE: s.push(MdBlock::QuoteClose); break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL: s.push(MdBlock::ListClose); break;
    case MD_BLOCK_LI: s.push(MdBlock::ItemClose); break;
    case MD_BLOCK_H:
    case MD_BLOCK_P:
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        if (s.leaf)
            s.closeLeaf();
        break;
    case MD_BLOCK_CODE:
    case MD_BLOCK_HTML:
        s.out.append(s.rawBlock);
        s.raw = false;
        break;
    case MD_BLOCK_TR: s.tableBlock.rows.append(s.row); break;
    case MD_BLOCK_TABLE: s.out.append(s.tableBlock); break;
    default: break;
    }
    return 0;
}

int enterSpan(MD_SPANTYPE type, void *detail, void *userdata)
{
    Sax &s = *static_cast<Sax *>(userdata);
    s.ensureLeaf();
    if (s.capture) {
        ++s.captureDepth;
        return 0;
    }
    s.flushHtml();
    switch (type) {
    case MD_SPAN_EM: s.stack.append({Italic, false, QString()}); break;
    case MD_SPAN_STRONG: s.stack.append({Bold, false, QString()}); break;
    case MD_SPAN_DEL: s.stack.append({Strike, false, QString()}); break;
    case MD_SPAN_U: s.stack.append({Underline, false, QString()}); break;
    case MD_SPAN_CODE: s.stack.append({CodeSpan, false, QString()}); break;
    case MD_SPAN_A: s.stack.append({Bold, true, attribute(static_cast<MD_SPAN_A_DETAIL *>(detail)->href)}); break;
    case MD_SPAN_IMG:
        s.capture = 1;
        s.captureDepth = 0;
        s.captured.clear();
        s.atom = Atom();
        s.atom.type = Atom::Image;
        s.atom.src = decodeSrc(attribute(static_cast<MD_SPAN_IMG_DETAIL *>(detail)->src));
        break;
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
        s.capture = 2;
        s.captureDepth = 0;
        s.captured.clear();
        s.atom = Atom();
        s.atom.type = Atom::Math;
        s.atom.display = type == MD_SPAN_LATEXMATH_DISPLAY;
        break;
    default: break;
    }
    return 0;
}

int leaveSpan(MD_SPANTYPE type, void *, void *userdata)
{
    Sax &s = *static_cast<Sax *>(userdata);
    if (s.capture) {
        if (s.captureDepth > 0) {
            --s.captureDepth;
            return 0;
        }
        if (s.capture == 1)
            s.atom.alt = s.captured;
        else
            s.atom.src = s.captured;
        s.capture = 0;
        s.addAtom(s.atom);
        return 0;
    }
    s.flushHtml();
    switch (type) {
    case MD_SPAN_EM: s.pop(Italic, false); break;
    case MD_SPAN_STRONG: s.pop(Bold, false); break;
    case MD_SPAN_DEL: s.pop(Strike, false); break;
    case MD_SPAN_U: s.pop(Underline, false); break;
    case MD_SPAN_CODE: s.pop(CodeSpan, false); break;
    case MD_SPAN_A: s.pop(Bold, true); break;
    default: break;
    }
    return 0;
}

int textEvent(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    Sax &s = *static_cast<Sax *>(userdata);
    const QString t = type == MD_TEXT_NULLCHAR ? QString(QChar(QChar::ReplacementCharacter))
                                               : QString::fromUtf8(text, int(size));
    if (s.raw) {
        s.rawBlock.text += t;
        return 0;
    }
    s.ensureLeaf();
    if (s.capture) {
        if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR)
            s.captured += ' ';
        else if (type == MD_TEXT_ENTITY)
            s.captured += decodeEntity(t);
        else
            s.captured += t;
        return 0;
    }
    if (type == MD_TEXT_HTML) {
        s.html += t;
        return 0;
    }
    s.flushHtml();
    switch (type) {
    case MD_TEXT_BR: s.addText(QString(QChar::LineSeparator)); break;
    case MD_TEXT_SOFTBR: s.addText(QStringLiteral(" ")); break;
    case MD_TEXT_ENTITY: s.addText(decodeEntity(t)); break;
    default: s.addText(t); break;
    }
    return 0;
}

}  // namespace

QVector<MdBlock> parseMarkdown(const QString &text)
{
    Sax sax;
    MD_PARSER parser = {};
    parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_LATEXMATHSPANS;
    parser.enter_block = enterBlock;
    parser.leave_block = leaveBlock;
    parser.enter_span = enterSpan;
    parser.leave_span = leaveSpan;
    parser.text = textEvent;
    const QByteArray utf8 = text.toUtf8();
    md_parse(utf8.constData(), MD_SIZE(utf8.size()), &parser, &sax);
    return sax.out;
}

QString splitFrontMatter(const QString &text, QString *body)
{
    static const RE front(QStringLiteral(R"(\A---[ \t]*\n(.*?\n)(?:---|\.\.\.)[ \t]*\n)"), RE::DotMatchesEverythingOption);
    static const RE key(QStringLiteral(R"(^[\w-]+\s*:)"));
    const auto m = front.match(text);
    if (m.hasMatch() && key.match(m.captured(1)).hasMatch()) {
        *body = text.mid(m.capturedEnd(0));
        QString f = m.captured(0);
        while (f.endsWith('\n'))
            f.chop(1);
        return f;
    }
    *body = text;
    return QString();
}

// =============================================================================
// Writing inline content
// =============================================================================

namespace {

const Style kOrder[] = {Bold, Italic, Strike, Underline};

bool isPunct(QChar c)
{
    static const QByteArray punct("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");
    return c.unicode() < 128 && punct.contains(char(c.unicode()));
}

bool loose(const Run &r)        // ordinary text, whose whitespace we are free to move or trim
{
    return !r.isAtom() && !(r.styles & (CodeSpan | RawSpan));
}

int leadingSpace(const QString &s)
{
    int i = 0;
    while (i < s.size() && s[i].isSpace())
        ++i;
    return i;
}

int trailingSpace(const QString &s)
{
    int i = s.size();
    while (i > 0 && s[i - 1].isSpace())
        --i;
    return s.size() - i;
}

bool isEdge(QChar c)            // what Markdown drops at the ends of a paragraph
{
    return c == ' ' || c == '\t' || c == QChar::LineSeparator;
}

QString stripEdge(QString s, bool left, bool right)
{
    while (left && !s.isEmpty() && isEdge(s[0]))
        s.remove(0, 1);
    while (right && !s.isEmpty() && isEdge(s[s.size() - 1]))
        s.chop(1);
    return s;
}

QString stripBlanks(QString s, bool left, bool right)   // spaces and tabs only
{
    while (left && !s.isEmpty() && (s[0] == ' ' || s[0] == '\t'))
        s.remove(0, 1);
    while (right && !s.isEmpty() && (s[s.size() - 1] == ' ' || s[s.size() - 1] == '\t'))
        s.chop(1);
    return s;
}

Runs merged(const Runs &runs)   // join neighbouring text runs that are formatted alike; drop empty ones
{
    Runs out;
    for (const Run &r : runs) {
        if (r.isAtom()) {
            out.append(r);
        } else if (!r.text.isEmpty()) {
            if (!out.isEmpty() && !out.last().isAtom() && out.last().styles == r.styles && out.last().href == r.href)
                out.last().text += r.text;
            else
                out.append(r);
        }
    }
    return out;
}

// Markdown emphasis can't begin or end with whitespace: move the edges of each
// styled span inwards past any whitespace.
Runs hoisted(const Runs &runs, const std::function<bool(const Run &)> &has, const std::function<void(Run &)> &drop)
{
    Runs out;
    int i = 0;
    const int n = runs.size();
    while (i < n) {
        if (!has(runs[i])) {
            out.append(runs[i++]);
            continue;
        }
        int j = i;
        while (j < n && has(runs[j]))
            ++j;
        Runs span = runs.mid(i, j - i), lead, trail;
        while (!span.isEmpty()) {
            Run &r = span.first();
            if (!loose(r))
                break;
            const int ws = leadingSpace(r.text);
            if (ws < r.text.size()) {
                if (ws) {
                    Run w = r;
                    w.text = r.text.left(ws);
                    drop(w);
                    lead.append(w);
                    r.text = r.text.mid(ws);
                }
                break;
            }
            Run w = span.takeFirst();
            drop(w);
            lead.append(w);
        }
        while (!span.isEmpty()) {
            Run &r = span.last();
            if (!loose(r))
                break;
            const int ws = trailingSpace(r.text);
            if (ws < r.text.size()) {
                if (ws) {
                    Run w = r;
                    w.text = r.text.right(ws);
                    drop(w);
                    trail.prepend(w);
                    r.text.chop(ws);
                }
                break;
            }
            Run w = span.takeLast();
            drop(w);
            trail.prepend(w);
        }
        out += lead;
        out += span;
        out += trail;
        i = j;
    }
    return out;
}

// Comparable form of a run list.
Runs canon(const Runs &runs)
{
    Runs out;
    for (Run r : runs) {
        if (r.isAtom()) {
            r.text.clear();
            r.styles = Styles();
            if (r.atom.type == Atom::Math)
                r.atom.src = r.atom.src.simplified();
            out.append(r);
        } else if (!r.text.isEmpty()) {
            out.append(r);
        }
    }
    return merged(out);
}

QString codeSpan(QString text)
{
    int longest = 0, run = 0;
    for (const QChar c : text) {
        run = c == '`' ? run + 1 : 0;
        longest = qMax(longest, run);
    }
    const QString ticks(longest + 1, '`');
    if (text.startsWith('`') || text.endsWith('`')
        || (text.startsWith(' ') && text.endsWith(' ') && !text.trimmed().isEmpty()))
        text = " " + text + " ";
    return ticks + text + ticks;
}

QString dest(const QString &url)
{
    static const RE plain(QStringLiteral(R"(^[^\s()<>\\]+$)"));
    if (plain.match(url).hasMatch())
        return url;
    QString u = url;
    u.replace('\\', "\\\\").replace('<', "\\<").replace('>', "\\>").replace('\n', ' ');
    return "<" + u + ">";
}

QString escapeText(const QString &s, bool oneLine, bool cell, bool everything, bool tilde, bool dollar)
{
    static const RE entity(QStringLiteral(R"(&(?:#\d+|#[xX][0-9a-fA-F]+|[A-Za-z][A-Za-z0-9]*);)"));
    QString out;
    out.reserve(s.size() + 8);
    const int n = s.size();
    for (int i = 0; i < n; ++i) {
        const QChar ch = s[i];
        const QChar prev = i ? s[i - 1] : QChar();
        const QChar next = i + 1 < n ? s[i + 1] : QChar();
        if (ch == QChar::LineSeparator) {
            out += oneLine ? QStringLiteral("<br>") : QStringLiteral("\\\n");
        } else if (ch == QChar::Nbsp) {
            out += QStringLiteral("&nbsp;");    // visible in the source, and survives at the ends of a paragraph
        } else if (everything && isPunct(ch)) {
            out += '\\';
            out += ch;
        } else if (ch == '\\') {
            // doubled wherever it would otherwise escape what we write next
            const bool risky = next.isNull() || isPunct(next) || next == QChar::LineSeparator || next == QChar::Nbsp;
            out += risky ? QStringLiteral("\\\\") : QStringLiteral("\\");
        } else if (ch == '`' || ch == '*' || ch == '[' || ch == ']' || (ch == '~' && tilde) || (ch == '$' && dollar)) {
            out += '\\';
            out += ch;
        } else if (ch == '_') {
            out += (prev.isLetterOrNumber() && next.isLetterOrNumber()) ? QStringLiteral("_") : QStringLiteral("\\_");
        } else if (ch == '<') {
            const bool risky = next.isNull() || next.isLetter() || next == '/' || next == '!' || next == '?';
            out += risky ? QStringLiteral("\\<") : QStringLiteral("<");
        } else if (ch == '&') {
            const bool risky = entity.match(s, i, RE::NormalMatch, RE::AnchoredMatchOption).hasMatch();
            out += risky ? QStringLiteral("\\&") : QStringLiteral("&");
        } else if (ch == '|' && cell) {
            out += QStringLiteral("\\|");
        } else {
            out += ch;
        }
    }
    return out;
}

// Stop a paragraph line from turning into a heading, list, quote or rule.
QString fixLineStarts(const QString &text)
{
    static const RE numbered(QStringLiteral(R"(^(\d{1,9})([.)])(?=\s|$))"));
    static const RE other(QStringLiteral(R"(^(?:#{1,6}(?=\s|$)|>|[-+](?=\s|$)|[-=]+\s*$|(?:-\s*){3,}$))"));
    QStringList lines = text.split('\n');
    for (QString &line : lines) {
        QString s = stripBlanks(line, true, false);
        const auto m = numbered.match(s);
        if (m.hasMatch())
            s = m.captured(1) + "\\" + s.mid(m.capturedLength(1));
        else if (other.match(s).hasMatch())
            s = "\\" + s;
        line = s;
    }
    return lines.join('\n');
}

}  // namespace

Runs MarkdownWriter::prepared(const Runs &runs)
{
    // Objects carry no styles, blank text is not emphasised, and whitespace
    // Markdown ignores is gone.
    Runs out;
    for (Run r : runs) {
        if (r.isAtom()) {
            r.styles = Styles();
            r.text.clear();
            if (r.atom.type == Atom::Math)
                r.atom.src = r.atom.src.simplified();
            out.append(r);
            continue;
        }
        if (r.text.isEmpty())
            continue;
        r.text.replace('\n', ' ');
        if (!(r.styles & CodeSpan)) {
            out.append(r);
            continue;
        }
        const Styles plain = r.styles & ~Styles(CodeSpan);  // a code span can't hold a line break or be blank
        const QStringList parts = r.text.split(QChar::LineSeparator);
        for (int k = 0; k < parts.size(); ++k) {
            if (k)
                out.append(Run{QString(QChar::LineSeparator), plain, r.href, Atom()});
            if (!parts[k].isEmpty())
                out.append(Run{parts[k], parts[k].trimmed().isEmpty() ? plain : r.styles, r.href, Atom()});
        }
    }
    out = merged(out);
    for (int i = out.size() - 1; i > 0; --i) {
        Run &a = out[i - 1];
        const Run &b = out[i];
        if (a.atom.type == Atom::Math && b.atom.type == Atom::Math && a.href == b.href) {
            a.atom.src += " " + b.atom.src;     // "$a$$b$" would not read back as two formulas
            out.remove(i);
        }
    }
    // spaces next to a line break mean nothing
    for (int i = 0; i < out.size(); ++i) {
        if (!loose(out[i]))
            continue;
        QStringList parts = out[i].text.split(QChar::LineSeparator);
        for (int k = 0; k < parts.size(); ++k)
            parts[k] = stripBlanks(parts[k], k > 0, k < parts.size() - 1);
        out[i].text = parts.join(QChar::LineSeparator);
        if (out[i].text.startsWith(QChar::LineSeparator) && i && loose(out[i - 1]))
            out[i - 1].text = stripBlanks(out[i - 1].text, false, true);
        if (out[i].text.endsWith(QChar::LineSeparator) && i + 1 < out.size() && loose(out[i + 1]))
            out[i + 1].text = stripBlanks(out[i + 1].text, true, false);
    }
    out = merged(out);
    while (!out.isEmpty() && loose(out.first()) && stripEdge(out.first().text, true, false).isEmpty())
        out.removeFirst();
    while (!out.isEmpty() && loose(out.last()) && stripEdge(out.last().text, false, true).isEmpty())
        out.removeLast();
    if (!out.isEmpty() && loose(out.first()))
        out.first().text = stripEdge(out.first().text, true, false);
    if (!out.isEmpty() && loose(out.last()))
        out.last().text = stripEdge(out.last().text, false, true);
    for (const Style style : kOrder) {
        out = hoisted(out, [style](const Run &r) { return bool(r.styles & style); },
                      [style](Run &r) { r.styles &= ~Styles(style); });
    }
    QSet<QString> hrefs;
    for (const Run &r : qAsConst(out))
        if (!r.href.isEmpty())
            hrefs.insert(r.href);
    for (const QString &href : qAsConst(hrefs)) {
        out = hoisted(out, [&href](const Run &r) { return r.href == href; }, [](Run &r) { r.href.clear(); });
    }
    return merged(out);
}

MarkdownWriter::MarkdownWriter(const QTextDocument *doc, const AssetStore *store, bool absolute, const QString &frontMatter)
    : m_doc(doc), m_store(store), m_absolute(absolute), m_front(frontMatter)
{
}

QString MarkdownWriter::fileSrc(const QString &src)
{
    // An image source as it appears in the file (before URL-quoting).
    if (src.startsWith(QLatin1String("asset:")))
        assets.insert(src.mid(6));
    return m_store ? m_store->toMarkdown(src, m_absolute) : src;
}

QString MarkdownWriter::atomText(const Atom &a, bool html) const
{
    if (a.type == Atom::Math) {
        const QString tex = a.src.simplified();
        if (tex.isEmpty())
            return QString();
        return a.display ? "$$" + tex + "$$" : "$" + tex + "$";
    }
    const QString src = encodeSrc(a.src);
    if (a.width > 0 || html) {
        QString out = "<img src=\"" + attrEscape(src) + "\" alt=\"" + attrEscape(a.alt) + "\"";
        if (a.width > 0)
            out += QString(" width=\"%1\"").arg(qRound(a.width));
        return out + ">";
    }
    QString alt = a.alt;
    alt.replace('\n', ' ');
    return "![" + escapeText(alt, true, false, true, true, true) + "](" + dest(src) + ")";
}

QString MarkdownWriter::emitRuns(const Runs &runs, int mode, const Escape &esc) const
{
    struct Key {
        int style;          // 0 = link
        QString href;
        bool operator==(const Key &o) const { return style == o.style && href == o.href; }
    };
    static const char *mdOpen[] = {"**", "*", "~~", "<u>"}, *mdClose[] = {"**", "*", "~~", "</u>"};
    static const char *htmlOpen[] = {"<strong>", "<em>", "<del>", "<u>"}, *htmlClose[] = {"</strong>", "</em>", "</del>", "</u>"};
    auto index = [](int style) { return style == Bold ? 0 : style == Italic ? 1 : style == Strike ? 2 : 3; };
    auto opener = [&](const Key &k) -> QString {
        if (k.style)
            return QLatin1String((mode == 0 ? mdOpen : htmlOpen)[index(k.style)]);
        return mode == 0 ? QStringLiteral("[") : "<a href=\"" + attrEscape(k.href) + "\">";
    };
    auto closer = [&](const Key &k) -> QString {
        if (k.style)
            return QLatin1String((mode == 0 ? mdClose : htmlClose)[index(k.style)]);
        return mode == 0 ? "](" + dest(k.href) + ")" : QStringLiteral("</a>");
    };

    QVector<QVector<Key>> targets;
    for (const Run &r : runs) {
        QVector<Key> t;
        if (!r.href.isEmpty())
            t.append({0, r.href});
        for (const Style s : kOrder)
            if (r.styles & s)
                t.append({int(s), QString()});
        targets.append(t);
    }
    auto reach = [&](const Key &k, int i) {     // the last consecutive run still carrying k
        while (i + 1 < runs.size() && targets[i + 1].contains(k))
            ++i;
        return i;
    };

    QString out;
    QVector<Key> stack;
    for (int i = 0; i < runs.size(); ++i) {
        const Run &r = runs[i];
        const QVector<Key> &target = targets[i];
        int keep = 0;
        while (keep < stack.size() && target.contains(stack[keep]))
            ++keep;
        while (stack.size() > keep)
            out += closer(stack.takeLast());
        // open what is new, longest-lasting outermost, so "**see [this](url) now**" nests cleanly
        QVector<Key> fresh;
        for (const Key &k : target)
            if (!stack.contains(k))
                fresh.append(k);
        std::stable_sort(fresh.begin(), fresh.end(), [&](const Key &a, const Key &b) { return reach(a, i) > reach(b, i); });
        for (const Key &k : qAsConst(fresh)) {
            out += opener(k);
            stack.append(k);
        }
        if (r.isAtom()) {
            out += atomText(r.atom, mode > 0);
        } else if (r.styles & RawSpan) {
            out += r.text;
        } else if (r.styles & CodeSpan) {
            QString t = r.text;
            out += codeSpan(t.replace(QChar::LineSeparator, ' '));
        } else {
            out += escapeText(r.text, esc.oneLine, esc.cell, esc.everything, esc.tilde, esc.dollar);
        }
    }
    while (!stack.isEmpty())
        out += closer(stack.takeLast());
    return out;
}

// Markdown for a block's inline content, verified to parse back to the same thing.
QString MarkdownWriter::inlineText(Runs runs, int flags, int heading)
{
    static const RE risky(QStringLiteral(R"([\\`*_\[\]<>&$~|#!\x{2028}\x{00a0}])"));
    static const RE lineRisk(QStringLiteral(R"(^\s*(?:[-+=>:]|\d+[.)]))"), RE::MultilineOption);
    static const RE trailingHashes(QStringLiteral(R"((?<![\\#])(#+)$)"));

    runs = prepared(runs);
    if (runs.isEmpty())
        return QString();
    bool plain = true, math = false;
    int tildes = 0, dollars = 0;
    for (Run &r : runs) {
        if (r.atom.type == Atom::Image)
            r.atom.src = fileSrc(r.atom.src);
        math = math || r.atom.type == Atom::Math;
        plain = plain && !r.isAtom() && !r.styles && r.href.isEmpty();
        if (loose(r)) {
            tildes += r.text.count('~');
            dollars += r.text.count('$');
        }
    }
    if (runs.size() == 1 && runs[0].isAtom() && runs[0].href.isEmpty())
        return atomText(runs[0].atom, false);       // a lone picture or formula needs no checking
    const Runs want = canon(runs);
    // A single stray ~ or $ is harmless; two could pair up as strikethrough or math.
    Escape esc{bool(flags & OneLine), bool(flags & Cell), false, tildes >= 2, dollars >= 2 || (math && dollars)};
    QString text;
    for (int mode = 0; mode < 3; ++mode) {
        modeUsed = qMax(modeUsed, mode);
        esc.everything = mode == 2;
        text = emitRuns(runs, mode, esc);
        if (mode < 2) {
            text = fixLineStarts(text);
            if (heading)        // "# Title #" would lose its last word
                text.replace(trailingHashes, QStringLiteral(R"(\\1)"));
        }
        if (mode == 0 && plain && !risky.match(text).hasMatch() && !lineRisk.match(text).hasMatch())
            return text;
        const QVector<MdBlock> parsed = parseMarkdown(heading ? QString(heading, '#') + " " + text : text);
        if (parsed.size() == 1 && parsed[0].type == (heading ? MdBlock::Heading : MdBlock::Paragraph)
            && (!heading || parsed[0].level == heading) && canon(parsed[0].runs) == want)
            return text;
    }
    return text;
}

Runs MarkdownWriter::runsOf(const QTextBlock &block) const
{
    Runs runs;
    for (const QTextFragment &frag : fragments(block)) {
        const QTextCharFormat cf = frag.charFormat();
        const QString text = frag.text();
        const int objects = text.count(QChar::ObjectReplacementCharacter);
        if (!objects) {
            Run r;
            r.text = text;
            r.styles = stylesOf(cf, &r.href);
            runs.append(r);
            continue;
        }
        Run r;
        r.href = cf.isAnchor() ? cf.anchorHref() : QString();
        switch (cf.objectType()) {
        case ObjFigure:
            r.atom.type = Atom::Image;
            r.atom.src = cf.stringProperty(PSrc);
            r.atom.alt = cf.stringProperty(PAlt);
            r.atom.width = cf.doubleProperty(PWidth);
            break;
        case ObjMath:
            r.atom.type = Atom::Math;
            r.atom.src = cf.stringProperty(PTex);
            r.atom.display = cf.boolProperty(PDisplay);
            break;
        case QTextFormat::ImageObject:      // pasted HTML
            r.atom.type = Atom::Image;
            r.atom.src = cf.toImageFormat().name();
            break;
        default:
            continue;
        }
        for (int k = 0; k < objects; ++k)
            runs.append(r);
    }
    return runs;
}

// =============================================================================
// Writing blocks
// =============================================================================

Kind MarkdownWriter::kind(const QTextBlock &b) const
{
    const Kind k = kindOf(b);
    if (k == Para) {    // tolerate documents Qt built from pasted HTML
        const QTextBlockFormat f = b.blockFormat();
        if (f.hasProperty(QTextFormat::BlockCodeLanguage) || f.nonBreakableLines())
            return Code;
        if (f.intProperty(QTextFormat::BlockQuoteLevel) > 0)
            return Quote;
        if (f.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))
            return Rule;
    }
    return k;
}

QString MarkdownWriter::lang(const QTextBlock &b) const
{
    const QTextBlockFormat f = b.blockFormat();
    const QString l = f.stringProperty(PLang);
    return l.isEmpty() ? f.stringProperty(QTextFormat::BlockCodeLanguage) : l;
}

void MarkdownWriter::collect(QTextFrame *frame, QVector<Item> &items) const
{
    for (auto it = frame->begin(); !it.atEnd(); ++it) {
        if (QTextFrame *child = it.currentFrame()) {
            if (auto *t = qobject_cast<QTextTable *>(child))
                items.append(Item{QTextBlock(), t});
            else
                collect(child, items);
        } else if (it.currentBlock().isValid()) {
            items.append(Item{it.currentBlock(), nullptr});
        }
    }
}

QString MarkdownWriter::fence(const QString &info, const QStringList &lines)
{
    // only a line made of backticks could close the fence early
    static const RE closer(QStringLiteral(R"(^ {0,3}(`{3,})[ \t]*$)"));
    int longest = 2;
    for (const QString &line : lines) {
        const auto m = closer.match(line);
        if (m.hasMatch())
            longest = qMax(longest, m.capturedLength(1));
    }
    const QString ticks(longest + 1, '`');
    QString cleanInfo = info;
    cleanInfo.remove('`');
    return ticks + cleanInfo + "\n" + lines.join('\n') + "\n" + ticks;
}

QString MarkdownWriter::table(QTextTable *t)
{
    QStringList rows;
    for (int r = 0; r < t->rows(); ++r) {
        QStringList cells;
        for (int c = 0; c < t->columns(); ++c) {
            const QTextTableCell cell = t->cellAt(r, c);
            const int last = cell.lastCursorPosition().block().blockNumber();
            QStringList parts;
            for (QTextBlock b = cell.firstCursorPosition().block(); b.isValid() && b.blockNumber() <= last; b = b.next()) {
                Runs runs = runsOf(b);
                if (r == 0)
                    for (Run &run : runs)
                        run.styles &= ~Styles(Bold);    // so is a header cell
                const QString part = inlineText(runs, OneLine | Cell);
                if (!part.isEmpty())
                    parts << part;
            }
            cells << parts.join("<br>");
        }
        rows << "| " + cells.join(" | ") + " |";
    }
    QString rule = "|";
    for (int c = 0; c < t->columns(); ++c)
        rule += " --- |";
    rows.insert(1, rule);
    return rows.join('\n');
}

QString MarkdownWriter::board(const QTextBlock &b, bool alone)
{
    return hasBoard(b) ? boardComment(boardOf(b), alone) : QString();
}

// One list region. Markdown can't keep two same-type lists apart, so adjacent
// ones are written (and numbered) as the single list they will read back as.
QString MarkdownWriter::list(const QVector<Item> &items, int &i)
{
    QStringList lines;
    int top = -1;                   // is the outermost list ordered?
    QHash<int, QPair<bool, int>> counts;
    while (i < items.size() && !items[i].table) {
        const QTextBlock b = items[i].block;
        QTextList *lst = b.textList();
        const int indent = b.blockFormat().indent();
        if (kind(b) != Para || (!lst && !(indent > 0 && !lines.isEmpty())))
            break;
        const QString text = inlineText(runsOf(b), OneLine);
        if (!lst) {                 // a further paragraph inside an item
            if (!text.isEmpty())
                lines << QString() << QString(4 * indent, ' ') + text;
            if (hasBoard(b)) {
                if (text.isEmpty())
                    lines << QString();
                lines << QString(4 * indent, ' ') + board(b, text.isEmpty());
            }
            ++i;
            continue;
        }
        const int level = qMax(1, lst->format().indent());
        const auto style = lst->format().style();
        const bool ordered = style != QTextListFormat::ListDisc && style != QTextListFormat::ListCircle
            && style != QTextListFormat::ListSquare;
        if (level == 1) {
            if (top < 0)
                top = ordered;
            else if (int(ordered) != top)
                break;              // a different kind of list starts here
        }
        for (const int deeper : counts.keys())
            if (deeper > level)
                counts.remove(deeper);
        const auto was = counts.value(level, qMakePair(ordered, 0));
        const int count = was.first == ordered ? was.second + 1 : 1;
        counts.insert(level, qMakePair(ordered, count));
        const QString mark = ordered ? QString::number(count) + ". " : QStringLiteral("- ");
        const auto marker = b.blockFormat().marker();
        const QString box = marker == QTextBlockFormat::MarkerType::Checked ? "[x] "
            : marker == QTextBlockFormat::MarkerType::Unchecked ? "[ ] " : "";
        lines << QString(4 * (level - 1), ' ') + mark + box + text;
        if (hasBoard(b))            // inside the item, so that the list goes on after it
            lines << QString(4 * (level - 1) + mark.size(), ' ') + board(b);
        ++i;
    }
    return lines.join('\n');
}

QString MarkdownWriter::text()
{
    QVector<Item> items;
    collect(m_doc->rootFrame(), items);
    QStringList chunks;
    const int n = items.size();
    auto gather = [&](int start, const std::function<bool(const QTextBlock &)> &same, int *end) {
        QVector<QTextBlock> blocks;
        int j = start;
        while (j < n && !items[j].table && same(items[j].block))
            blocks.append(items[j++].block);
        *end = j;
        return blocks;
    };
    auto add = [&](const QString &chunk) {
        if (!chunk.isEmpty())
            chunks << chunk;
    };
    // A cell's storyboard frame is written after all of the cell, not between the code and what it printed.
    auto outputNext = [&](int j) { return j < n && !items[j].table && kind(items[j].block) == Output; };

    int i = 0;
    while (i < n) {
        if (items[i].table) {
            add(table(items[i].table));
            ++i;
            continue;
        }
        const QTextBlock b = items[i].block;
        const Kind k = kind(b);
        if (k == Code) {
            const QString l = lang(b);
            QStringList lines;
            const int cell = b.blockFormat().intProperty(PCell);
            for (const QTextBlock &x : gather(i, [&](const QTextBlock &x) {
                     return kind(x) == Code && lang(x) == l && x.blockFormat().intProperty(PCell) == cell;
                 }, &i))
                lines << x.text();
            add(fence(l, lines));
            if (!outputNext(i))
                add(board(b));
        } else if (k == Output) {
            QStringList lines;
            bool err = false;
            auto flush = [&] {
                if (!lines.isEmpty())
                    add(fence(err ? "error" : "output", lines));
                lines.clear();
            };
            for (const QTextBlock &x : gather(i, [&](const QTextBlock &x) { return kind(x) == Output; }, &i)) {
                Runs atoms;
                for (const Run &r : runsOf(x))
                    if (r.isAtom())
                        atoms.append(r);
                const bool xErr = x.blockFormat().boolProperty(PErr);
                if (!atoms.isEmpty() || (!lines.isEmpty() && xErr != err))
                    flush();
                err = xErr;
                for (Run r : qAsConst(atoms)) {
                    if (r.atom.type == Atom::Image) {
                        r.atom.src = fileSrc(r.atom.src);
                        r.atom.alt = "output";
                    }
                    add(atomText(r.atom, false));
                }
                QString t = x.text();
                t.remove(QChar::ObjectReplacementCharacter);
                if (!t.isEmpty() || atoms.isEmpty())
                    lines << t;
            }
            flush();
            add(board(boardHost(b)));
        } else if (k == Raw) {
            QStringList lines;
            for (const QTextBlock &x : gather(i, [&](const QTextBlock &x) { return kind(x) == Raw; }, &i))
                lines << x.text();
            add(lines.join('\n'));
            add(board(b));
        } else if (k == Fold) {
            QTextCharFormat cf;
            if (objectFormat(b, ObjFold, &cf)) {
                const QString l = cf.stringProperty(PLang), code = cf.stringProperty(PCode);
                if (code.contains("-->"))        // would end the comment early
                    add(fence((l + " {hide}").trimmed(), code.split('\n')));
                else
                    add("<!--cell " + l + "\n" + code + "\n-->");
            }
            Runs extra;                         // anything typed next to the pill is still text
            for (const Run &r : runsOf(b))
                if (!r.isAtom())
                    extra.append(r);
            add(inlineText(extra));
            ++i;
            if (!outputNext(i))
                add(board(b));
        } else if (k == Rule) {
            add(inlineText(runsOf(b)));
            add("---");
            add(board(b));
            ++i;
        } else if (k == Heading) {
            const int level = b.blockFormat().headingLevel();
            Runs runs = runsOf(b);
            for (Run &r : runs)
                r.styles &= ~Styles(Bold);      // a heading is bold already (pasted HTML says so twice)
            const QString t = inlineText(runs, OneLine, level);
            if (!t.isEmpty())
                add(QString(level, '#') + " " + t);
            add(board(b, t.isEmpty()));
            ++i;
        } else if (k == Quote) {
            QStringList paras;
            for (const QTextBlock &x : gather(i, [&](const QTextBlock &x) { return kind(x) == Quote; }, &i)) {
                const QString t = inlineText(runsOf(x), OneLine);
                if (!t.isEmpty())
                    paras << "> " + t;
                if (hasBoard(x))        // on the next line of the same paragraph of the quote
                    paras << (t.isEmpty() ? QString() : paras.takeLast() + "\n") + "> " + board(x, t.isEmpty());
            }
            add(paras.join("\n>\n"));
        } else if (b.textList()) {
            add(list(items, i));
        } else {
            const Runs runs = runsOf(b);
            const QString t = runs.size() == 1 && runs[0].atom.type == Atom::Math && runs[0].atom.display
                ? "$$\n" + runs[0].atom.src.trimmed() + "\n$$" : inlineText(runs);
            add(t);
            add(board(b, t.isEmpty()));
            ++i;
        }
    }
    const QString face = faceComment(faceOf(m_doc));
    if (!face.isEmpty())
        add(face);
    QString body = chunks.join("\n\n");
    if (!m_front.isEmpty()) {
        QString f = m_front;
        while (f.endsWith('\n'))
            f.chop(1);
        body = f + "\n\n" + body;
    }
    return body.isEmpty() ? body : body + "\n";
}

// =============================================================================
// Reading
// =============================================================================

MarkdownReader::MarkdownReader(const QTextCursor &cursor, AssetStore *store, bool importImages)
    : m_c(cursor), m_store(store), m_import(importImages)
{
}

void MarkdownReader::read(const QString &text)
{
    QString t = text;
    blocks(parseMarkdown(t.replace("\r\n", "\n")));
}

void MarkdownReader::insert(const QString &text)
{
    QString t = text;
    const QVector<MdBlock> parsed = parseMarkdown(t.replace("\r\n", "\n"));
    if (parsed.size() == 1 && parsed[0].type == MdBlock::Paragraph) {
        insertRuns(parsed[0].runs);
        return;
    }
    if (m_c.block().length() <= 1) {
        m_fresh = true;
    } else if (m_c.atBlockStart()) {
        m_c.insertBlock();
        m_c.movePosition(QTextCursor::PreviousBlock);
        m_fresh = true;
    } else {
        if (!m_c.atBlockEnd()) {
            m_c.insertBlock();
            m_c.movePosition(QTextCursor::PreviousBlock);
            m_c.movePosition(QTextCursor::EndOfBlock);
        }
        m_fresh = false;
    }
    blocks(parsed);
}

void MarkdownReader::begin(const QTextBlockFormat &fmt)
{
    if (m_fresh) {
        m_c.setBlockFormat(fmt);
        m_c.setBlockCharFormat(QTextCharFormat());
        m_fresh = false;
    } else {
        m_c.insertBlock(fmt, QTextCharFormat());
    }
}

void MarkdownReader::insertRuns(const Runs &runs)
{
    for (const Run &r : runs) {
        if (!r.isAtom()) {
            m_c.insertText(r.text, charFmt(r.styles, r.href));
            continue;
        }
        QTextCharFormat f;
        if (r.atom.type == Atom::Image) {
            QString src = r.atom.src;
            if (!baseDir.isEmpty() && !hasScheme(src) && QDir::isRelativePath(src))
                src = QDir::cleanPath(baseDir + "/" + src);
            if (m_store)
                src = m_store->fromMarkdown(src, m_import);
            f = figureFmt(src, r.atom.width, r.atom.alt);
        } else {
            f = mathFmt(r.atom.src, r.atom.display);
        }
        if (!r.href.isEmpty()) {
            f.setAnchor(true);
            f.setAnchorHref(r.href);
        }
        m_c.insertText(QString(QChar::ObjectReplacementCharacter), f);
    }
}

void MarkdownReader::lines(const QTextBlockFormat &fmt, const QString &text)
{
    QString t = text;
    t.remove('\r');
    for (const QString &line : t.split('\n')) {
        begin(fmt);
        m_c.insertText(line, QTextCharFormat());
    }
}

// A paragraph that is only pictures. alt "output" marks a cell's figure.
void MarkdownReader::imageBlock(Runs runs)
{
    bool isOutput = true;
    for (const Run &r : qAsConst(runs))
        isOutput = isOutput && r.atom.alt == QLatin1String("output");
    begin(isOutput ? blockFmt(Output) : QTextBlockFormat());
    if (isOutput)
        for (Run &r : runs)
            r.atom.alt.clear();
    insertRuns(runs);
}

void MarkdownReader::paragraph(Runs runs)
{
    if (!m_lists.isEmpty() && m_itemOpen) {
        m_itemOpen = false;
        ListCtx &ctx = m_lists.last();
        QTextBlockFormat fmt;
        if (m_itemTask)
            fmt.setMarker(m_itemTask == 2 ? QTextBlockFormat::MarkerType::Checked : QTextBlockFormat::MarkerType::Unchecked);
        begin(fmt);
        if (!ctx.list) {
            static const QTextListFormat::Style bullets[] = {QTextListFormat::ListDisc, QTextListFormat::ListCircle,
                                                             QTextListFormat::ListSquare};
            QTextListFormat lf;
            lf.setIndent(m_lists.size());
            lf.setStyle(ctx.ordered ? QTextListFormat::ListDecimal : bullets[qMin(m_lists.size(), 3) - 1]);
            ctx.list = m_c.createList(lf);
        } else {
            ctx.list->add(m_c.block());
        }
        insertRuns(runs);
        return;
    }
    Runs pics;
    bool onlyPics = true;
    for (const Run &r : qAsConst(runs)) {
        if (r.atom.type == Atom::Image)
            pics.append(r);
        else if (r.isAtom() || !r.text.trimmed().isEmpty())
            onlyPics = false;
    }
    if (!pics.isEmpty() && onlyPics && m_lists.isEmpty() && !m_quote) {
        imageBlock(pics);
        return;
    }
    QTextBlockFormat fmt;
    if (!m_lists.isEmpty())
        fmt.setIndent(m_lists.size());      // a further paragraph inside a list item
    else if (m_quote)
        fmt = blockFmt(Quote);
    begin(fmt);
    insertRuns(runs);
}

void MarkdownReader::fence(const QString &rawInfo, const QString &content)
{
    static const RE hide(QStringLiteral(R"((?:^|\s)\{?\.?hide\}?\s*$)"));
    const QString info = rawInfo.trimmed();
    const QString body = content.endsWith('\n') ? content.left(content.size() - 1) : content;
    const QString first = info.section(RE(QStringLiteral("\\s+")), 0, 0).toLower();
    const auto h = hide.match(info);
    if (first == "output" || first == "error") {
        lines(blockFmt(Output, 0, QString(), first == "error"), body);
    } else if (h.hasMatch()) {
        begin(blockFmt(Fold));
        m_c.insertText(QString(QChar::ObjectReplacementCharacter), foldFmt(info.left(h.capturedStart(0)).trimmed(), body));
    } else {
        lines(blockFmt(Code, 0, info), body);
    }
}

void MarkdownReader::html(const QString &content)
{
    static const RE cell(QStringLiteral(R"(^<!--\s*cell\b[ \t]*([^\n]*)\n(.*?)\n?-->\s*$)"), RE::DotMatchesEverythingOption);
    static const RE img(QStringLiteral(R"(^<img\b([^>]*?)/?>\s*$)"),
                        RE::DotMatchesEverythingOption | RE::CaseInsensitiveOption);
    static const RE font(QStringLiteral(R"(^<!--\s*font:([^\n]*?)-->$)"));
    static const RE frame(QStringLiteral(R"(^<!--\s*storyboard\b(\s+alone\b)?([^>]*?)-->$)"));
    const QString s = content.trimmed();
    auto m = font.match(s);
    if (m.hasMatch() && faceNamed(m.captured(1)).builtIn()) {
        face = faceNamed(m.captured(1)).name;       // not content: how all of it looks
        return;
    }
    m = frame.match(s);
    if (m.hasMatch()) {
        board(m.captured(2), !m.captured(1).isEmpty());
        return;
    }
    m = cell.match(s);
    if (m.hasMatch()) {
        begin(blockFmt(Fold));
        m_c.insertText(QString(QChar::ObjectReplacementCharacter), foldFmt(m.captured(1).trimmed(), m.captured(2)));
        return;
    }
    m = img.match(s);
    if (m.hasMatch()) {
        Run r;
        r.atom = imgAtom(m.captured(1));
        paragraph({r});
        return;
    }
    lines(blockFmt(Raw), s);
}

// A storyboard frame: it goes with the line read just before it.
void MarkdownReader::board(const QString &drawing, bool alone)
{
    if (alone || m_fresh || m_itemOpen)     // its line is an empty one (or there is none yet)
        paragraph(Runs());
    const QTextBlock host = boardHost(m_c.block());
    if (host.isValid())
        setBoard(host, drawingText(parseDrawing(drawing)));
}

void MarkdownReader::table(const MdBlock &block)
{
    int cols = 1;
    for (const auto &row : block.rows)
        cols = qMax(cols, row.size());
    if (block.rows.isEmpty())
        return;
    QTextDocument *doc = m_c.document();
    QTextTable *t = insertTable(m_c, block.rows.size(), cols);
    for (int r = 0; r < block.rows.size(); ++r) {
        for (int c = 0; c < block.rows[r].size(); ++c) {
            m_c = t->cellAt(r, c).firstCursorPosition();
            insertRuns(block.rows[r][c]);
        }
    }
    m_c = QTextCursor(doc);
    m_c.setPosition(t->lastPosition() + 1);
    m_fresh = true;
}

void MarkdownReader::blocks(const QVector<MdBlock> &list)
{
    for (const MdBlock &b : list) {
        switch (b.type) {
        case MdBlock::Paragraph: paragraph(b.runs); break;
        case MdBlock::Heading:
            begin(blockFmt(Heading, b.level));
            insertRuns(b.runs);
            break;
        case MdBlock::CodeBlock: fence(b.info, b.text); break;
        case MdBlock::HtmlBlock: html(b.text); break;
        case MdBlock::Rule: begin(blockFmt(Rule)); break;
        case MdBlock::Table: table(b); break;
        case MdBlock::ListOpen: m_lists.append(ListCtx{b.ordered, nullptr}); break;
        case MdBlock::ListClose:
            if (!m_lists.isEmpty())
                m_lists.removeLast();
            break;
        case MdBlock::ItemOpen:
            m_itemOpen = true;
            m_itemTask = b.task;
            break;
        case MdBlock::ItemClose:
            if (m_itemOpen)         // an item with nothing in it
                paragraph(Runs());
            break;
        case MdBlock::QuoteOpen: ++m_quote; break;
        case MdBlock::QuoteClose: --m_quote; break;
        }
    }
}

QString faceComment(const QString &face)
{
    const Face &known = faceNamed(face);
    return known.builtIn() ? "<!--font: " + known.name + "-->" : QString();
}

QString boardComment(const QString &drawing, bool alone)
{
    const QString said = (QString(alone ? " alone" : "") + " " + drawing).trimmed();
    return said.isEmpty() ? QStringLiteral("<!--storyboard-->") : "<!--storyboard " + said + "-->";
}

QString loadMarkdown(QTextDocument *doc, const QString &text, AssetStore *store, bool importImages)
{
    QString normal = text, body;
    const QString front = splitFrontMatter(normal.replace("\r\n", "\n"), &body);
    doc->clear();
    QTextCursor cursor(doc);
    cursor.setBlockFormat(QTextBlockFormat());
    MarkdownReader reader(cursor, store, importImages);
    reader.read(body);
    setFace(doc, reader.face);
    return front;
}

}  // namespace jot
