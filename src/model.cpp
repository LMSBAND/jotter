#include "model.h"

#include <QFont>
#include <QRegularExpression>
#include <QTextList>
#include <cmath>

namespace jot {

// ---- reading meaning ------------------------------------------------------

Kind kindOf(const QTextBlock &block)
{
    const QTextBlockFormat f = block.blockFormat();
    if (f.headingLevel())
        return Heading;
    return Kind(f.intProperty(PKind));
}

QString langOf(const QTextBlock &block)
{
    return block.blockFormat().stringProperty(PLang);
}

QString langName(const QString &info)
{
    static const QRegularExpression rx(QStringLiteral(R"(^\s*\{?\s*\.?([\w+#.-]+))"));
    const auto m = rx.match(info);
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}

QTextTable *tableOf(const QTextBlock &block)
{
    return QTextCursor(block).currentTable();
}

QVector<QTextFragment> fragments(const QTextBlock &block)
{
    QVector<QTextFragment> out;
    for (auto it = block.begin(); !it.atEnd(); ++it)
        if (it.fragment().isValid())
            out.append(it.fragment());
    return out;
}

bool objectFormat(const QTextBlock &block, int objectType, QTextCharFormat *out)
{
    if (!block.text().contains(QChar::ObjectReplacementCharacter))
        return false;
    for (const QTextFragment &frag : fragments(block)) {
        if (frag.charFormat().objectType() == objectType) {
            if (out)
                *out = frag.charFormat();
            return true;
        }
    }
    return false;
}

bool hasFigure(const QTextBlock &block)
{
    return objectFormat(block, ObjFigure) || objectFormat(block, QTextFormat::ImageObject);
}

bool loneObject(const QTextBlock &block, QTextCharFormat *out)
{
    if (block.text() != QString(QChar::ObjectReplacementCharacter))
        return false;
    const auto frags = fragments(block);
    if (frags.isEmpty())
        return false;
    *out = frags.first().charFormat();
    return true;
}

Styles stylesOf(const QTextCharFormat &cf, QString *href)
{
    // Tolerant of formats that came from pasted HTML rather than from us.
    Styles s;
    if (cf.fontWeight() >= QFont::DemiBold)
        s |= Bold;
    if (cf.fontItalic())
        s |= Italic;
    if (cf.fontStrikeOut())
        s |= Strike;
    const QString link = cf.isAnchor() ? cf.anchorHref() : QString();
    if (cf.fontUnderline() && link.isEmpty())
        s |= Underline;
    const QString family = cf.fontFamily().toLower();
    bool mono = cf.fontFixedPitch();
    for (const char *hint : {"mono", "courier", "consolas", "hack", "menlo"})
        mono = mono || family.contains(QLatin1String(hint));
    if (mono)
        s |= CodeSpan;
    if (cf.boolProperty(PRaw))
        s |= RawSpan;
    if (href)
        *href = link;
    return s;
}

QString faceOf(const QTextDocument *doc)
{
    return doc->rootFrame()->frameFormat().stringProperty(PFace);
}

void setFace(QTextDocument *doc, const QString &name)
{
    QTextFrameFormat format = doc->rootFrame()->frameFormat();
    if (format.stringProperty(PFace) == name)
        return;
    if (name.isEmpty())
        format.clearProperty(PFace);
    else
        format.setProperty(PFace, name);
    doc->rootFrame()->setFrameFormat(format);
}

// ---- making formats -------------------------------------------------------

QTextBlockFormat blockFmt(Kind kind, int level, const QString &lang, bool err)
{
    QTextBlockFormat f;
    if (kind == Heading)
        f.setHeadingLevel(qBound(1, level ? level : 1, 6));
    else if (kind != Para)
        f.setProperty(PKind, int(kind));
    if (kind == Code) {
        static int nextCell = 0;
        f.setProperty(PLang, lang);
        f.setProperty(PCell, ++nextCell);
    }
    if (kind == Output && err)
        f.setProperty(PErr, true);
    return f;
}

QTextCharFormat charFmt(Styles styles, const QString &href)
{
    QTextCharFormat f;
    if (styles & Bold)
        f.setFontWeight(QFont::Bold);
    if (styles & Italic)
        f.setFontItalic(true);
    if (styles & Strike)
        f.setFontStrikeOut(true);
    if (styles & Underline)
        f.setFontUnderline(true);
    if (styles & CodeSpan)
        f.setFontFixedPitch(true);
    if (styles & RawSpan)
        f.setProperty(PRaw, true);
    if (!href.isEmpty()) {
        f.setAnchor(true);
        f.setAnchorHref(href);
    }
    return f;
}

QTextCharFormat figureFmt(const QString &src, qreal width, const QString &alt)
{
    QTextCharFormat f;
    f.setObjectType(ObjFigure);
    f.setProperty(PSrc, src);
    if (width > 0)
        f.setProperty(PWidth, width);
    if (!alt.isEmpty())
        f.setProperty(PAlt, alt);
    return f;
}

QTextCharFormat mathFmt(const QString &tex, bool display)
{
    QTextCharFormat f;
    f.setObjectType(ObjMath);
    f.setProperty(PTex, tex);
    if (display)
        f.setProperty(PDisplay, true);
    else
        f.setVerticalAlignment(QTextCharFormat::AlignMiddle);
    return f;
}

QTextCharFormat foldFmt(const QString &lang, const QString &code)
{
    QTextCharFormat f;
    f.setObjectType(ObjFold);
    f.setProperty(PLang, lang);
    f.setProperty(PCode, code);
    return f;
}

QTextTableFormat tableFmt(int columns)
{
    QTextTableFormat f;
    f.setBorder(1);
    f.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    f.setBorderCollapse(true);
    f.setBorderBrush(QColor(128, 134, 142));    // mid grey: reads on light and dark paper
    f.setCellPadding(5);
    f.setCellSpacing(0);
    f.setTopMargin(4);
    f.setBottomMargin(10);
    f.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    QVector<QTextLength> widths(columns, QTextLength(QTextLength::PercentageLength, 100.0 / columns));
    f.setColumnWidthConstraints(widths);
    return f;
}

QTextTable *insertTable(QTextCursor &cursor, int rows, int columns)
{
    // Qt puts the table after the cursor's block. If that block is an empty
    // paragraph we were only standing in, fold it into the one before.
    QTextDocument *doc = cursor.document();
    const QTextBlock here = cursor.block();
    const bool stray = here.length() <= 1 && here.previous().isValid() && !cursor.currentTable();
    const int strayPos = here.position();
    QTextTable *table = cursor.insertTable(rows, columns, tableFmt(columns));
    if (stray) {
        QTextCursor tc(doc);
        tc.setPosition(strayPos);
        if (!tc.currentTable() && tc.block().length() <= 1)
            tc.deletePreviousChar();
    }
    return table;
}

void removeBlock(const QTextBlock &block)
{
    // Deleting a paragraph break merges two blocks and the first one's format
    // wins. Removing a whole block must not restyle its neighbours.
    QTextCursor c(block);
    const QTextBlock prev = block.previous(), next = block.next();
    if (prev.isValid()) {
        // (Qt lets an empty block take the format of the one merged into it,
        // so say explicitly what the survivor is.)
        const QTextBlockFormat keep = prev.blockFormat();
        const QTextCharFormat keepChar = prev.charFormat();
        c.beginEditBlock();
        c.setPosition(block.position() - 1);
        c.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setBlockFormat(keep);
        c.setBlockCharFormat(keepChar);
        c.endEditBlock();
    } else if (next.isValid()) {
        const QTextBlockFormat keep = next.blockFormat();
        const QTextCharFormat keepChar = next.charFormat();
        c.beginEditBlock();
        c.setPosition(block.position());
        c.setPosition(next.position(), QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setBlockFormat(keep);
        c.setBlockCharFormat(keepChar);
        c.endEditBlock();
    } else {
        c.select(QTextCursor::BlockUnderCursor);
        c.removeSelectedText();
        c.setBlockFormat(QTextBlockFormat());
    }
}

bool emptyParagraph(const QTextBlock &block)
{
    return kindOf(block) == Para && block.length() <= 1 && !block.textList() && !tableOf(block);
}

// ---- runs and cells -------------------------------------------------------

bool sameRun(const QTextBlock &a, const QTextBlock &b)
{
    const Kind ka = kindOf(a);
    if (ka != kindOf(b))
        return false;
    if (ka == Code)
        return langOf(a) == langOf(b) && a.blockFormat().intProperty(PCell) == b.blockFormat().intProperty(PCell);
    return ka == Output || ka == Raw || ka == Quote;
}

QTextBlock runFirst(QTextBlock block)
{
    for (;;) {
        const QTextBlock p = block.previous();
        if (!p.isValid() || !sameRun(p, block))
            return block;
        block = p;
    }
}

QTextBlock runLast(QTextBlock block)
{
    for (;;) {
        const QTextBlock n = block.next();
        if (!n.isValid() || !sameRun(block, n))
            return block;
        block = n;
    }
}

QTextBlock cellHead(QTextBlock block)
{
    if (!block.isValid())
        return QTextBlock();
    Kind k = kindOf(block);
    if (k == Output) {
        block = runFirst(block).previous();
        if (!block.isValid())
            return QTextBlock();
        k = kindOf(block);
    }
    if (k == Code)
        return runFirst(block);
    if (k == Fold)
        return block;
    return QTextBlock();
}

QTextBlock cellTail(const QTextBlock &head)
{
    return kindOf(head) == Code ? runLast(head) : head;
}

void cellSource(const QTextBlock &head, QString *info, QString *code)
{
    if (kindOf(head) == Fold) {
        QTextCharFormat cf;
        if (objectFormat(head, ObjFold, &cf)) {
            *info = cf.stringProperty(PLang);
            *code = cf.stringProperty(PCode);
        }
        return;
    }
    QStringList lines;
    const int last = runLast(head).blockNumber();
    for (QTextBlock b = head; b.isValid() && b.blockNumber() <= last; b = b.next())
        lines << b.text();
    *info = langOf(head);
    *code = lines.join(QLatin1Char('\n'));
}

bool cellOutputs(const QTextBlock &head, QTextBlock *first, QTextBlock *last)
{
    const QTextBlock b = cellTail(head).next();
    if (!b.isValid() || kindOf(b) != Output)
        return false;
    if (first)
        *first = b;
    if (last)
        *last = runLast(b);
    return true;
}

QTextBlock cellEnd(const QTextBlock &head)
{
    QTextBlock last;
    return cellOutputs(head, nullptr, &last) ? last : cellTail(head);
}

QVector<QTextBlock> cellHeads(const QTextDocument *doc)
{
    QVector<QTextBlock> heads;
    for (QTextBlock b = doc->firstBlock(); b.isValid(); b = b.next()) {
        const Kind k = kindOf(b);
        if (k == Code) {
            heads.append(b);
            b = runLast(b);
        } else if (k == Fold) {
            heads.append(b);
        }
    }
    return heads;
}

// ---- the storyboard ----------------------------------------------------------

Drawing parseDrawing(const QString &text)
{
    Drawing drawing;
    for (const QString &word : text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const int colon = word.indexOf(QLatin1Char(':'));
        if (colon < 2 || !QLatin1String(BoardInks).contains(word[0]))
            continue;
        bool ok = false;
        Stroke stroke;
        stroke.ink = word[0];
        stroke.width = word.mid(1, colon - 1).toInt(&ok);
        if (!ok || stroke.width < 1 || stroke.width > 99)
            continue;
        const QStringList numbers = word.mid(colon + 1).split(QLatin1Char(','));
        for (int i = 0; ok && i + 1 < numbers.size(); i += 2) {
            bool okX = false, okY = false;
            const int x = numbers[i].toInt(&okX), y = numbers[i + 1].toInt(&okY);
            ok = okX && okY;
            stroke.points.append(QPoint(qBound(0, x, BoardW), qBound(0, y, BoardH)));
        }
        if (ok && numbers.size() % 2 == 0 && !stroke.points.isEmpty())
            drawing.append(stroke);
    }
    return drawing;
}

QString drawingText(const Drawing &drawing)
{
    QStringList words;
    for (const Stroke &stroke : drawing) {
        QStringList numbers;
        for (const QPoint &p : stroke.points)
            numbers << QString::number(p.x()) << QString::number(p.y());
        if (!numbers.isEmpty())
            words << stroke.ink + QString::number(stroke.width) + QLatin1Char(':') + numbers.join(QLatin1Char(','));
    }
    return words.join(QLatin1Char(' '));
}

bool hasBoard(const QTextBlock &block)
{
    return block.isValid() && block.blockFormat().hasProperty(PBoard);
}

QString boardOf(const QTextBlock &block)
{
    return block.blockFormat().stringProperty(PBoard);
}

QTextBlock boardHost(const QTextBlock &block)
{
    if (!block.isValid() || tableOf(block))
        return QTextBlock();
    const Kind k = kindOf(block);
    if (k == Output) {
        const QTextBlock head = cellHead(block);
        return head.isValid() ? head : runFirst(block);
    }
    return k == Code || k == Raw ? runFirst(block) : block;
}

void setBoard(const QTextBlock &block, const QString &drawing)
{
    QTextBlockFormat f;
    f.setProperty(PBoard, drawing);
    QTextCursor(block).mergeBlockFormat(f);
}

void clearBoard(const QTextBlock &block)
{
    QTextBlockFormat f = block.blockFormat();
    f.clearProperty(PBoard);
    QTextCursor(block).setBlockFormat(f);
}

// ---- spacing --------------------------------------------------------------

namespace {

struct Layout {
    int top = 0, bottom = 0, left = 0, right = 0;
    int lineHeight = 140;       // percent; 0 = single
    int align = Qt::AlignLeft;
    bool operator==(const Layout &o) const
    {
        return top == o.top && bottom == o.bottom && left == o.left && right == o.right
            && lineHeight == o.lineHeight && align == o.align;
    }
};

Layout wanted(const QTextBlock &block, qreal em, qreal leading)
{
    Layout l;
    if (tableOf(block)) {
        l.lineHeight = qRound(125 * leading);
        return l;
    }
    const Kind k = kindOf(block);
    const QTextBlock prev = block.previous(), next = block.next();
    const bool starts = !(prev.isValid() && sameRun(prev, block));
    const bool ends = !(next.isValid() && sameRun(block, next));
    const bool outputNext = next.isValid() && kindOf(next) == Output;
    qreal top = 0, bottom = 0, left = 0, right = 0;
    switch (k) {
    case Heading: {
        static const qreal above[] = {1.5, 1.3, 1.1};
        top = prev.isValid() ? above[qMin(block.blockFormat().headingLevel(), 3) - 1] * em : 0;
        bottom = 0.5 * em;
        l.lineHeight = 120;
        break;
    }
    case Code:
        left = right = PadH * em;
        l.lineHeight = 125;
        if (starts)
            top = (0.45 + PadV) * em;
        if (ends)
            bottom = (PadV + (outputNext ? 0.3 : 0.8)) * em;
        break;
    case Output:
        left = PadH * em;
        l.lineHeight = 125;
        if (ends)
            bottom = 0.8 * em;
        break;
    case Raw:
        l.lineHeight = 125;
        if (ends)
            bottom = 0.8 * em;
        break;
    case Fold:
        top = 0.3 * em;
        bottom = (outputNext ? 0.2 : 0.8) * em;
        l.lineHeight = 0;
        break;
    case Rule:
        top = bottom = 0.3 * em;
        break;
    case Quote:
        left = 1.1 * em;
        bottom = (ends ? 0.8 : 0.5) * em;
        break;
    case Para:
        if (block.textList()) {
            // items of one list (and anything nested in it) sit close together
            bool tight = false;
            if (next.isValid() && kindOf(next) == Para) {
                const QTextList *nextList = next.textList();
                if (!nextList) {
                    tight = next.blockFormat().indent() > 0;
                } else if (nextList->format().indent() > 1) {
                    tight = true;
                } else {
                    QTextBlock top = block;     // the outermost item this block sits under
                    while (top.textList() && top.textList()->format().indent() > 1 && top.previous().isValid()
                           && top.previous().textList())
                        top = top.previous();
                    tight = top.textList() == nextList;
                }
            }
            bottom = (tight ? 0.2 : 0.8) * em;
        } else {
            bottom = 0.8 * em;
        }
        break;
    }
    if (k == Heading || k == Quote || k == Para)
        l.lineHeight = qRound(l.lineHeight * leading);      // lines of the text face (the others are in the code font)
    if (hasFigure(block))
        l.lineHeight = 0;       // proportional spacing would scale with the picture's height
    QTextCharFormat only;
    if (loneObject(block, &only) && (only.objectType() == ObjFigure || only.boolProperty(PDisplay)))
        l.align = Qt::AlignHCenter;
    l.top = qRound(top);
    l.bottom = qRound(bottom);
    l.left = qRound(left);
    l.right = qRound(right);
    return l;
}

Layout current(const QTextBlock &block)
{
    const QTextBlockFormat f = block.blockFormat();
    Layout l;
    l.top = qRound(f.topMargin());
    l.bottom = qRound(f.bottomMargin());
    l.left = qRound(f.leftMargin());
    l.right = qRound(f.rightMargin());
    l.lineHeight = f.lineHeightType() == QTextBlockFormat::ProportionalHeight ? int(f.lineHeight()) : 0;
    l.align = int(f.alignment() & Qt::AlignHorizontal_Mask);
    return l;
}

bool emptyFold(const QTextBlock &block)     // its pill was deleted: it is an ordinary paragraph now
{
    return kindOf(block) == Fold && !objectFormat(block, ObjFold);
}

bool needsTail(const QTextDocument *doc)    // always keep a plain paragraph to type in after the last cell
{
    const Kind k = kindOf(doc->lastBlock());
    return k == Code || k == Output || k == Raw || k == Fold || k == Rule;
}

}  // namespace

bool needsNormalize(const QTextDocument *doc, QTextBlock first, QTextBlock last, qreal em, qreal leading)
{
    const int stop = last.blockNumber();
    for (QTextBlock b = first; b.isValid() && b.blockNumber() <= stop; b = b.next())
        if (emptyFold(b) || !(current(b) == wanted(b, em, leading)))
            return true;
    return needsTail(doc);
}

bool normalize(QTextDocument *doc, QTextBlock first, QTextBlock last, qreal em, qreal leading)
{
    QTextCursor cur(doc);
    bool changed = false;
    auto apply = [&](const QTextBlock &b) {
        if (emptyFold(b)) {
            QTextBlockFormat f = b.blockFormat();
            f.clearProperty(PKind);
            cur.setPosition(b.position());
            cur.setBlockFormat(f);
            changed = true;
        }
        const Layout want = wanted(b, em, leading);
        if (current(b) == want)
            return;
        QTextBlockFormat d;
        d.setTopMargin(want.top);
        d.setBottomMargin(want.bottom);
        d.setLeftMargin(want.left);
        d.setRightMargin(want.right);
        if (want.lineHeight)
            d.setLineHeight(want.lineHeight, QTextBlockFormat::ProportionalHeight);
        else
            d.setLineHeight(0, QTextBlockFormat::SingleHeight);
        d.setAlignment(Qt::Alignment(want.align));
        cur.setPosition(b.position());
        cur.mergeBlockFormat(d);
        changed = true;
    };
    const int stop = last.blockNumber();
    for (QTextBlock b = first; b.isValid() && b.blockNumber() <= stop; b = b.next())
        apply(b);
    if (needsTail(doc)) {
        cur.movePosition(QTextCursor::End);
        cur.insertBlock(QTextBlockFormat(), QTextCharFormat());
        const QTextBlock added = doc->lastBlock();
        apply(added.previous());
        apply(added);
        changed = true;
    }
    return changed;
}

}  // namespace jot
