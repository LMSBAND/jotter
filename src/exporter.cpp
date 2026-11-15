#include "exporter.h"

#include "assets.h"
#include "markdown.h"
#include "mathtex.h"
#include "model.h"
#include "render.h"

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QTextDocument>
#include <QTextList>
#include <QUrl>

namespace jot {

namespace {

namespace KSH = KSyntaxHighlighting;

QString esc(const QString &s)
{
    return s.toHtmlEscaped();
}

// Code as HTML spans, coloured like the editor's light theme.
class HtmlHighlighter : public KSH::AbstractHighlighter
{
public:
    QString render(const QString &lang, const QStringList &lines)
    {
        const KSH::Definition def = syntaxFor(lang);
        if (!def.isValid())
            return esc(lines.join('\n'));
        setDefinition(def);
        setTheme(syntaxRepository().defaultTheme(KSH::Repository::LightTheme));
        KSH::State state;
        QStringList out;
        for (const QString &text : lines) {
            m_line = &text;
            m_html.clear();
            m_at = 0;
            state = highlightLine(QStringView(text), state);
            m_html += esc(text.mid(m_at));
            out << m_html;
        }
        return out.join('\n');
    }

protected:
    void applyFormat(int offset, int length, const KSH::Format &format) override
    {
        if (length <= 0)
            return;
        m_html += esc(m_line->mid(m_at, offset - m_at));
        const QString piece = esc(m_line->mid(offset, length));
        m_at = offset + length;
        QString style;
        if (!format.isDefaultTextStyle(theme())) {
            if (format.hasTextColor(theme()))
                style += "color:" + format.textColor(theme()).name() + ";";
            if (format.isBold(theme()))
                style += "font-weight:bold;";
            if (format.isItalic(theme()))
                style += "font-style:italic;";
        }
        m_html += style.isEmpty() ? piece : "<span style=\"" + style + "\">" + piece + "</span>";
    }

private:
    const QString *m_line = nullptr;
    QString m_html;
    int m_at = 0;
};

struct Html {
    const QTextDocument *doc;
    const AssetStore &store;
    bool embed;
    MarkdownWriter runs;        // only for runsOf()
    QStringList out;

    Html(const QTextDocument *d, const AssetStore &s, bool e) : doc(d), store(s), embed(e), runs(d) {}

    QString imageSrc(const QString &src) const
    {
        const QString path = store.resolve(src);
        if (path.isEmpty())
            return src;         // on the web: leave the address
        if (!embed)
            return QUrl::fromLocalFile(path).toString();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return src;
        // (worked out from the picture itself: a bare system has no file-type database to ask)
        QString kind = QString::fromLatin1(QImageReader::imageFormat(path)).toLower();
        if (kind == "jpg")
            kind = "jpeg";
        else if (kind == "svg" || kind == "svgz")
            kind = "svg+xml";
        return "data:image/" + (kind.isEmpty() ? QString("png") : kind) + ";base64," + QString::fromLatin1(f.readAll().toBase64());
    }

    // The width the editor shows a picture at (plots are saved at twice that).
    static int imageWidth(const QString &path, qreal wanted)
    {
        if (wanted > 0)
            return qRound(wanted);
        QImageReader reader(path);
        const QImage img = reader.read();
        if (img.isNull())
            return 0;
        const qreal dpi = img.dotsPerMeterX() * 0.0254;
        return qRound(img.width() * (dpi >= 120 ? 96.0 / dpi : 1.0));
    }

    QString atom(const Atom &a) const
    {
        if (a.type == Atom::Image) {
            const int w = imageWidth(store.resolve(a.src), a.width);
            return QString("<img src=\"%1\" alt=\"%2\"%3>").arg(esc(imageSrc(a.src)), esc(a.alt),
                                                               w ? QString(" width=\"%1\"").arg(w) : QString());
        }
        MathRenderer *math = MathRenderer::instance();
        math->get(a.src, a.display);
        math->waitForAll(20000);
        const auto &f = math->get(a.src, a.display);
        if (f.state != MathRenderer::Formula::Ready)
            return "<code>$" + esc(a.src) + "$</code>";
        const qreal scale = a.display ? 1.25 : 1.15;
        return QString("<img class=\"math\" alt=\"%1\" src=\"data:image/png;base64,%2\" "
                       "style=\"height:%3em;vertical-align:%4em\">")
            .arg(esc(a.src), QString::fromLatin1(MathRenderer::png(f).toBase64()))
            .arg(f.height * scale, 0, 'f', 3).arg(a.display ? 0.0 : -f.depth * scale, 0, 'f', 3);
    }

    QString inlineHtml(const QTextBlock &block) const
    {
        QString html;
        for (const Run &r : runs.runsOf(block)) {
            QString piece;
            if (r.isAtom()) {
                piece = atom(r.atom);
            } else if (r.styles & RawSpan) {
                piece = r.text;
            } else {
                piece = esc(r.text);
                piece.replace(QChar::LineSeparator, QLatin1String("<br>"));
                if (r.styles & CodeSpan)
                    piece = "<code>" + piece + "</code>";
                if (r.styles & Underline)
                    piece = "<u>" + piece + "</u>";
                if (r.styles & Strike)
                    piece = "<del>" + piece + "</del>";
                if (r.styles & Italic)
                    piece = "<em>" + piece + "</em>";
                if (r.styles & Bold)
                    piece = "<strong>" + piece + "</strong>";
            }
            if (!r.href.isEmpty())
                piece = "<a href=\"" + esc(r.href) + "\">" + piece + "</a>";
            html += piece;
        }
        return html;
    }

    void paragraph(const QTextBlock &b)
    {
        const QString html = inlineHtml(b);
        if (html.isEmpty())
            return;
        QTextCharFormat only;
        QString cls;
        if (loneObject(b, &only))
            cls = only.objectType() == ObjMath ? (only.boolProperty(PDisplay) ? " class=\"math\"" : "") : " class=\"figure\"";
        out << "<p" + cls + ">" + html + "</p>";
    }

    void table(QTextTable *t)
    {
        out << "<table>";
        for (int r = 0; r < t->rows(); ++r) {
            QString row = "<tr>";
            for (int c = 0; c < t->columns(); ++c) {
                const QTextTableCell cell = t->cellAt(r, c);
                const int last = cell.lastCursorPosition().block().blockNumber();
                QStringList parts;
                for (QTextBlock b = cell.firstCursorPosition().block(); b.isValid() && b.blockNumber() <= last; b = b.next())
                    parts << inlineHtml(b);
                const QString tag = r == 0 ? "th" : "td";
                row += "<" + tag + ">" + parts.join("<br>") + "</" + tag + ">";
            }
            out << (r == 0 ? "<thead>" : r == 1 ? "<tbody>" : "") + row + "</tr>" + (r == 0 ? "</thead>" : "");
        }
        out << (t->rows() > 1 ? "</tbody></table>" : "</table>");
    }

    // Consecutive list items become nested <ul>/<ol>.
    int list(const QVector<QTextBlock> &blocks, int i)
    {
        QVector<QString> open;      // closing tags of the lists currently open
        bool itemOpen = false;
        auto closeTo = [&](int depth) {
            while (open.size() > depth) {
                if (itemOpen)
                    out << "</li>";
                out << open.takeLast();
                itemOpen = !open.isEmpty();
            }
        };
        for (; i < blocks.size(); ++i) {
            const QTextBlock b = blocks[i];
            const QTextList *lst = b.textList();
            if (kindOf(b) != Para || tableOf(b) || (!lst && !(b.blockFormat().indent() > 0 && !open.isEmpty())))
                break;
            if (!lst) {             // a further paragraph inside the current item
                out << "<p>" + inlineHtml(b) + "</p>";
                continue;
            }
            const int level = qMax(1, lst->format().indent());
            const auto style = lst->format().style();
            const bool ordered = style != QTextListFormat::ListDisc && style != QTextListFormat::ListCircle
                && style != QTextListFormat::ListSquare;
            const QString closing = ordered ? "</ol>" : "</ul>";
            closeTo(level);
            if (open.size() == level && open.last() != closing)
                closeTo(level - 1);     // a different kind of list at this depth
            if (open.size() == level && itemOpen)
                out << "</li>";
            while (open.size() < level) {
                out << (ordered ? "<ol>" : "<ul>");
                open.append(closing);
                itemOpen = false;
            }
            const auto marker = b.blockFormat().marker();
            if (marker == QTextBlockFormat::MarkerType::NoMarker)
                out << "<li>" + inlineHtml(b);
            else
                out << QString("<li class=\"task\"><input type=\"checkbox\" disabled%1>")
                           .arg(marker == QTextBlockFormat::MarkerType::Checked ? " checked" : "") + inlineHtml(b);
            itemOpen = true;
        }
        closeTo(0);
        return i;
    }

    void walk(QTextFrame *frame)
    {
        // Gather the frame's direct content, then go through it in runs.
        struct Item { QTextBlock block; QTextTable *table; };
        QVector<Item> items;
        for (auto it = frame->begin(); !it.atEnd(); ++it) {
            if (QTextFrame *child = it.currentFrame()) {
                if (auto *t = qobject_cast<QTextTable *>(child))
                    items.append({QTextBlock(), t});
            } else if (it.currentBlock().isValid()) {
                items.append({it.currentBlock(), nullptr});
            }
        }
        HtmlHighlighter highlighter;
        int i = 0;
        const int n = items.size();
        while (i < n) {
            if (items[i].table) {
                table(items[i++].table);
                continue;
            }
            const QTextBlock b = items[i].block;
            const Kind k = kindOf(b);
            auto run = [&] {            // the blocks of this run that are in this frame
                QVector<QTextBlock> blocks;
                const int last = runLast(b).blockNumber();
                while (i < n && !items[i].table && items[i].block.blockNumber() <= last)
                    blocks.append(items[i++].block);
                return blocks;
            };
            if (k == Code) {
                QStringList lines;
                for (const QTextBlock &x : run())
                    lines << x.text();
                out << "<pre class=\"code\"><code>" + highlighter.render(langName(langOf(b)), lines) + "</code></pre>";
            } else if (k == Output) {
                QStringList lines;
                bool err = false;
                auto flush = [&] {
                    if (!lines.isEmpty())
                        out << QString("<pre class=\"output%1\">").arg(err ? " error" : "") + esc(lines.join('\n')) + "</pre>";
                    lines.clear();
                };
                for (const QTextBlock &x : run()) {
                    const bool xErr = x.blockFormat().boolProperty(PErr);
                    if (hasFigure(x)) {
                        flush();
                        out << "<p class=\"figure\">" + inlineHtml(x) + "</p>";
                        continue;
                    }
                    if (!lines.isEmpty() && xErr != err)
                        flush();
                    err = xErr;
                    lines << x.text();
                }
                flush();
            } else if (k == Raw) {
                QStringList lines;
                for (const QTextBlock &x : run())
                    lines << x.text();
                out << lines.join('\n');
            } else if (k == Quote) {
                out << "<blockquote>";
                for (const QTextBlock &x : run())
                    out << "<p>" + inlineHtml(x) + "</p>";
                out << "</blockquote>";
            } else if (k == Fold) {
                ++i;                    // hidden code stays hidden; its output follows as usual
            } else if (k == Rule) {
                out << "<hr>";
                ++i;
            } else if (k == Heading) {
                const QString tag = "h" + QString::number(qBound(1, b.blockFormat().headingLevel(), 6));
                out << "<" + tag + ">" + inlineHtml(b) + "</" + tag + ">";
                ++i;
            } else if (b.textList()) {
                QVector<QTextBlock> blocks;
                for (int j = i; j < n && !items[j].table; ++j)
                    blocks.append(items[j].block);
                i += list(blocks, 0);
            } else {
                paragraph(b);
                ++i;
            }
        }
    }
};

}  // namespace

QString bodyHtml(const QTextDocument *doc, const AssetStore &store, bool embed)
{
    Html html(doc, store, embed);
    html.walk(doc->rootFrame());
    return html.out.join('\n');
}

QString pageHtml(const QTextDocument *doc, const AssetStore &store, const QString &title)
{
    QFile css(":/export.css");
    css.open(QIODevice::ReadOnly);
    return "<!doctype html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>" + title.toHtmlEscaped()
        + "</title>\n<style>\n" + QString::fromUtf8(css.readAll()) + "</style>\n</head>\n<body>\n<article>\n"
        + bodyHtml(doc, store, true) + "\n</article>\n</body>\n</html>\n";
}

}  // namespace jot
