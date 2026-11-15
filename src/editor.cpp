#include "editor.h"

#include "exporter.h"
#include "markdown.h"
#include "mathtex.h"
#include "render.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHelpEvent>
#include <QImageReader>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QStyle>
#include <QTextDocumentFragment>
#include <QTextList>
#include <QToolTip>
#include <QUrl>

namespace jot {

using RE = QRegularExpression;

namespace {

const char *kMime = "application/x-jotter-markdown";

// Joining two blocks gives the result the first one's format. That is fine
// between kinds of prose, or within one run; anything else would silently turn
// prose into code (or the reverse).
bool isDocument(const QString &path)    // something Jotter can read as a document
{
    static const QStringList suffixes = {"md", "markdown", "mdown", "mkd", "txt", "text"};
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

bool mergeable(const QTextBlock &a, const QTextBlock &b)
{
    if (sameRun(a, b))
        return true;
    auto prose = [](Kind k) { return k == Para || k == Heading || k == Quote; };
    return prose(kindOf(a)) && prose(kindOf(b));
}

}  // namespace

Editor::Editor(QWidget *parent) : QTextEdit(parent), theme(Theme::systemIsDark()), m_images(&m_store)
{
    setFrameShape(QFrame::NoFrame);
    setAcceptRichText(true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    QTextDocument *doc = document();
    doc->setDocumentMargin(PageMargin);
    auto *layout = doc->documentLayout();
    layout->registerHandler(ObjFigure, new FigureHandler(this));
    layout->registerHandler(ObjMath, new MathHandler(this));
    layout->registerHandler(ObjFold, new FoldHandler(this));
    m_renderer = new Renderer(doc, theme);
    m_kernel = new Kernel(this);

    connect(doc, &QTextDocument::contentsChange, this, &Editor::onContentsChange);
    connect(doc, &QTextDocument::contentsChanged, this, &Editor::onContentsChanged);
    connect(m_kernel, &Kernel::stateChanged, this, [this] {
        viewport()->update();
        emit cellsChanged();
    });
    connect(MathRenderer::instance(), &MathRenderer::changed, this, [this] {
        m_mathImages.clear();       // a formula finished typesetting: its size is now known
        document()->markContentsDirty(0, document()->characterCount());
        viewport()->update();
    });
    // Text typed right after a picture or formula must not inherit its format.
    connect(this, &QTextEdit::cursorPositionChanged, this, [this] {
        const QTextCursor c = textCursor();
        if (!c.hasSelection() && c.position() > 0
            && document()->characterAt(c.position() - 1) == QChar::ObjectReplacementCharacter
            && currentCharFormat().objectType() != QTextFormat::NoObject)
            setCurrentCharFormat(QTextCharFormat());
    });
    applyTheme();
    loadMarkdown(QString());        // an empty document, spaced like any other
}

Editor::~Editor()
{
    m_kernel->shutdown();
}

// ---- looks ------------------------------------------------------------------

void Editor::applyTheme()
{
    QPalette pal = palette();
    pal.setColor(QPalette::Window, theme.canvas);
    pal.setColor(QPalette::Base, theme.paper);
    pal.setColor(QPalette::Text, theme.text);
    setPalette(pal);
    setAutoFillBackground(true);
    setFont(theme.bodyFont());
    document()->setDefaultFont(theme.bodyFont());
    document()->setIndentWidth(theme.layoutEm() * 1.7);
    setTabStopDistance(QFontMetricsF(theme.monoFont()).horizontalAdvance(' ') * 4);
    setCursorWidth(qMax(1, qRound(theme.em() / 10)));
    m_renderer->setTheme(theme);
    m_mathImages.clear();
    layoutPage();
    viewport()->update();
}

// The viewport is the sheet of paper; the margins around it show the canvas.
void Editor::layoutPage()
{
    const int bar = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
    const int side = qMax(0, (width() - bar - PageWidth) / 2);
    setViewportMargins(side, side ? 14 : 0, side, 0);
}

bool Editor::event(QEvent *e)
{
    if (e->type() == QEvent::Resize) {
        layoutPage();
    } else if (e->type() == QEvent::ShortcutOverride) {
        // Qt's text widget claims Ctrl+K ("delete to end of line"); here it is Insert > Link.
        auto *key = static_cast<QKeyEvent *>(e);
        if (key->modifiers() == Qt::ControlModifier && key->key() == Qt::Key_K) {
            e->ignore();
            return false;
        }
    }
    return QTextEdit::event(e);
}

bool Editor::viewportEvent(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {     // hovering a link shows where it goes
        auto *help = static_cast<QHelpEvent *>(e);
        const QString href = anchorAt(help->pos());
        if (href.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), href + tr("\nCtrl+click to open"), viewport());
        return true;
    }
    return QTextEdit::viewportEvent(e);
}

void Editor::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        if (e->angleDelta().y())
            emit zoomRequested(e->angleDelta().y() > 0 ? 1 : -1);
        return;
    }
    QTextEdit::wheelEvent(e);
}

qreal Editor::availWidth(int pos) const
{
    const QTextDocument *doc = document();
    qreal w = (doc->textWidth() > 0 ? doc->textWidth() : PageWidth) - 2 * doc->documentMargin();
    const QTextBlock b = doc->findBlock(pos);
    const QTextBlockFormat f = b.blockFormat();
    if (QTextTable *t = tableOf(b))
        w = w / qMax(1, t->columns()) - 2 * t->format().cellPadding() - 6;
    const int indent = b.textList() ? b.textList()->format().indent() : f.indent();
    return qMax(40.0, w - f.leftMargin() - f.rightMargin() - indent * doc->indentWidth());
}

void Editor::noteObject(int pos, const QRectF &rect, int type)
{
    if (m_objects.size() > 600)
        m_objects.clear();          // stale entries from edits; the next paint refills it
    m_objects.insert(pos, qMakePair(rect, type));
}

QImage Editor::mathImage(const QString &tex, bool display, const QColor &color, int width, int height)
{
    const QString key = QString("%1|%2|%3x%4|%5").arg(display).arg(color.rgba()).arg(width).arg(height).arg(tex);
    auto it = m_mathImages.constFind(key);
    if (it != m_mathImages.constEnd())
        return *it;
    if (m_mathImages.size() > 300)
        m_mathImages.clear();
    const QImage img = MathRenderer::tinted(MathRenderer::instance()->get(tex, display), color, width, height);
    m_mathImages.insert(key, img);
    return img;
}

QString Editor::cellState(const QTextBlock &block) const
{
    const QTextBlock head = cellHead(block);
    if (!head.isValid())
        return QString();
    auto same = [&](const QTextCursor &c) {
        const QTextBlock other = cellHead(c.block());
        return other.isValid() && other.blockNumber() == head.blockNumber();
    };
    if (m_isRunning && same(m_running))
        return QStringLiteral("running");
    for (const QTextCursor &c : m_queue)
        if (same(c))
            return QStringLiteral("queued");
    return QString();
}

QTextCharFormat Editor::formatAt(int pos) const
{
    QTextCursor c(document());
    c.setPosition(qMin(pos + 1, document()->characterCount() - 1));
    return c.charFormat();
}

// ---- the document -----------------------------------------------------------

void Editor::loadMarkdown(const QString &text, const QString &path, bool importImages)
{
    m_queue.clear();
    QTextDocument *doc = document();
    m_store.docPath = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
    m_normalizing = true;
    doc->setUndoRedoEnabled(false);
    frontMatter = jot::loadMarkdown(doc, text, &m_store, importImages);
    normalize(doc, doc->firstBlock(), doc->lastBlock(), theme.layoutEm());
    doc->setUndoRedoEnabled(true);
    doc->setModified(false);
    m_normalizing = false;
    m_dirtyFrom = m_dirtyTo = -1;
    m_objects.clear();
    QSet<QString> refs;
    toMarkdown(false, &refs);
    m_store.savedRefs = refs;
    setTextCursor(QTextCursor(doc));
    verticalScrollBar()->setValue(0);
}

QString Editor::toMarkdown(bool absolute, QSet<QString> *assets) const
{
    MarkdownWriter writer(document(), &m_store, absolute, frontMatter);
    const QString text = writer.text();
    if (assets)
        *assets = writer.assets;
    return text;
}

bool Editor::save(const QString &path, QString *error)
{
    QSet<QString> assets;
    toMarkdown(false, &assets);
    m_store.saveTo(path, assets);       // pictures first: the text refers to where they end up
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(toMarkdown().toUtf8()) < 0 || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    document()->setModified(false);
    return true;
}

// The selection as Markdown, with pictures as absolute paths so it can be
// pasted into another document.
QString Editor::selectionMarkdown(const QTextCursor &cursor) const
{
    QTextDocument tmp;
    QTextCursor tc(&tmp);
    tc.insertFragment(cursor.selection());
    const QTextBlock first = document()->findBlock(cursor.selectionStart());
    const QTextBlock last = document()->findBlock(cursor.selectionEnd());
    if (first.blockNumber() != last.blockNumber() && !tableOf(first)) {
        // A fragment forgets what its first block was; a selection that spans
        // blocks should keep it (the first line of a code block is still code).
        QTextBlockFormat fmt = first.blockFormat();
        fmt.setObjectIndex(-1);
        QTextCursor fc(tmp.firstBlock());
        fc.setBlockFormat(fmt);
        if (QTextList *list = first.textList()) {
            const QTextBlock second = tmp.firstBlock().next();
            if (second.isValid() && second.textList() && first.next().textList() == list)
                second.textList()->add(tmp.firstBlock());
            else
                fc.createList(list->format());
        }
    }
    return MarkdownWriter(&tmp, &m_store, true).text();
}

// ---- keeping spacing in step with structure -----------------------------------
// Margins are derived from structure (see model.cpp). After every edit the
// blocks around it are checked and corrected, as part of the same undo step.

void Editor::onContentsChange(int pos, int, int added)
{
    if (m_normalizing)
        return;
    m_dirtyFrom = m_dirtyFrom < 0 ? pos : qMin(m_dirtyFrom, pos);
    m_dirtyTo = qMax(m_dirtyTo, pos + added);
}

void Editor::dirtyBlocks(QTextBlock *first, QTextBlock *last) const
{
    const QTextDocument *doc = document();
    *first = doc->findBlock(qMax(0, m_dirtyFrom));
    *last = doc->findBlock(qMin(m_dirtyTo, doc->characterCount() - 1));
    if (!first->isValid())
        *first = doc->firstBlock();
    if (!last->isValid())
        *last = doc->lastBlock();
    if (first->previous().isValid())
        *first = first->previous();
    if (last->next().isValid())
        *last = last->next();
}

void Editor::onContentsChanged()
{
    if (m_normalizing || m_replaying || m_dirtyFrom < 0)
        return;
    QTextBlock first, last;
    dirtyBlocks(&first, &last);
    m_dirtyFrom = m_dirtyTo = -1;
    normalizeRange(first, last, true);
}

void Editor::normalizeRange(const QTextBlock &first, const QTextBlock &last, bool join)
{
    QTextDocument *doc = document();
    if (!needsNormalize(doc, first, last, theme.layoutEm()))
        return;
    const QTextCursor user = textCursor();
    const int pos = user.position(), anchor = user.anchor();
    m_normalizing = true;
    QTextCursor cur(doc);
    if (join)
        cur.joinPreviousEditBlock();
    else
        cur.beginEditBlock();
    normalize(doc, first, last, theme.layoutEm());
    cur.endEditBlock();
    m_normalizing = false;
    QTextCursor now = textCursor();     // appending the trailing paragraph must not drag the cursor along
    if (now.position() != pos || now.anchor() != anchor) {
        const int max = doc->characterCount() - 1;
        now.setPosition(qMin(anchor, max));
        now.setPosition(qMin(pos, max), QTextCursor::KeepAnchor);
        setTextCursor(now);
    }
}

// Qt may keep our spacing fix-up as a separate undo step from the edit that
// caused it. One press of Undo should never stop half-way, so keep stepping
// until the document is consistent again.
void Editor::replay(bool undo)
{
    QTextDocument *doc = document();
    m_replaying = true;
    for (int i = 0; i < 8 && (undo ? doc->isUndoAvailable() : doc->isRedoAvailable()); ++i) {
        m_dirtyFrom = m_dirtyTo = -1;
        if (undo)
            QTextEdit::undo();
        else
            QTextEdit::redo();
        if (m_dirtyFrom < 0)
            break;
        QTextBlock first, last;
        dirtyBlocks(&first, &last);
        if (!needsNormalize(doc, first, last, theme.layoutEm()))
            break;
    }
    m_dirtyFrom = m_dirtyTo = -1;
    m_replaying = false;
    viewport()->update();
}

// End a compound edit and leave the cursor where the edit put it. Closing the
// edit block is when spacing is corrected, and if that appends the paragraph
// kept after a final cell, a cursor sitting at the end of the document would be
// carried into it - so note the position first.
void Editor::commit(QTextCursor &c)
{
    const int pos = c.position(), anchor = c.anchor();
    c.endEditBlock();
    QTextCursor placed(document());
    placed.setPosition(anchor);
    placed.setPosition(pos, QTextCursor::KeepAnchor);
    setTextCursor(placed);
}

void Editor::undoStep() { replay(true); }
void Editor::redoStep() { replay(false); }

// ---- typing -----------------------------------------------------------------

void Editor::keyPressEvent(QKeyEvent *e)
{
    if (e->matches(QKeySequence::Undo)) {
        undoStep();
        return;
    }
    if (e->matches(QKeySequence::Redo)) {
        redoStep();
        return;
    }
    const int key = e->key();
    const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
    QTextCursor c = textCursor();
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (mods == Qt::AltModifier) {
            paragraphBelow();
            return;
        }
        if (mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
            runAll();
            return;
        }
        if ((mods == Qt::ControlModifier || mods == Qt::ShiftModifier) && cellHead(c.block()).isValid()) {
            runCell(mods == Qt::ShiftModifier);     // as in a notebook: Shift+Enter moves on, Ctrl+Enter stays
            return;
        }
        if (mods == Qt::ControlModifier)
            return;
        if (mods == Qt::NoModifier && handleEnter(c))
            return;
    } else if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
        if (!(mods & (Qt::ControlModifier | Qt::AltModifier))) {
            handleTab(c, key == Qt::Key_Backtab || (mods & Qt::ShiftModifier));
            return;
        }
    } else if (key == Qt::Key_Backspace && mods == Qt::NoModifier && !c.hasSelection()) {
        if (handleBackspace(c))
            return;
    } else if (key == Qt::Key_Delete && mods == Qt::NoModifier && !c.hasSelection()) {
        if (handleDelete(c))
            return;
    } else if (key == Qt::Key_Space && mods == Qt::NoModifier) {
        if (handleSpace(c))
            return;
    }
    QTextEdit::keyPressEvent(e);
    const QString typed = e->text();
    if (typed.size() == 1 && QStringLiteral("`*$~").contains(typed))
        inlineAutoformat();
}

bool Editor::handleEnter(QTextCursor &c)
{
    if (c.hasSelection())
        return false;
    const QTextBlock block = c.block();
    if (tableOf(block))
        return false;
    const QString text = block.text();
    auto plainBelow = [&] {
        c.insertBlock(QTextBlockFormat(), QTextCharFormat());
        setTextCursor(c);
        return true;
    };
    switch (kindOf(block)) {
    case Code: {    // keep the indentation; one level deeper after an opener
        const int col = c.positionInBlock();
        int n = 0;
        while (n < text.size() && (text[n] == ' ' || text[n] == '\t'))
            ++n;
        QString indent = text.left(qMin(n, col));
        const QString before = text.left(col).trimmed();
        if (!before.isEmpty() && QStringLiteral(":{[(").contains(before.right(1)))
            indent += QStringLiteral("    ");
        c.beginEditBlock();
        c.insertBlock();
        c.insertText(indent);
        c.endEditBlock();
        setTextCursor(c);
        return true;
    }
    case Output:
        if (c.atBlockEnd() && !(block.next().isValid() && sameRun(block, block.next())))
            return plainBelow();
        return false;
    case Fold:
    case Rule:
        c.movePosition(QTextCursor::EndOfBlock);
        return plainBelow();
    case Heading:
        if (c.atBlockEnd())
            return plainBelow();
        if (c.atBlockStart()) {         // open a plain line above the heading
            c.beginEditBlock();
            c.insertBlock();
            QTextCursor above(c.block().previous());
            above.setBlockFormat(QTextBlockFormat());
            c.endEditBlock();
            setTextCursor(c);
            return true;
        }
        return false;
    case Quote:
        if (text.isEmpty()) {           // Enter on an empty line leaves the quote
            c.setBlockFormat(QTextBlockFormat());
            return true;
        }
        return false;
    case Raw:
        return false;
    case Para:
        break;
    }
    if (QTextList *list = block.textList()) {
        if (text.trimmed().isEmpty()) {     // Enter on an empty item steps out one level
            if (list->format().indent() > 1)
                indentList(-1);
            else
                c.setBlockFormat(QTextBlockFormat());
            return true;
        }
        c.beginEditBlock();
        c.insertBlock();
        QTextBlockFormat f = c.blockFormat();
        if (f.marker() == QTextBlockFormat::MarkerType::Checked) {
            f.setMarker(QTextBlockFormat::MarkerType::Unchecked);   // a new to-do starts unticked
            c.setBlockFormat(f);
        }
        c.endEditBlock();
        setTextCursor(c);
        return true;
    }
    if (block.blockFormat().indent() > 0 && text.isEmpty()) {
        c.setBlockFormat(QTextBlockFormat());
        return true;
    }
    if (!c.atBlockEnd())
        return false;
    // Markdown habits: ``` or ```lang opens a code cell, --- draws a rule.
    static const RE fence(QStringLiteral(R"(^```+\s*([^`]*)$)"));
    const auto m = fence.match(text);
    const QString trimmed = text.trimmed();
    if (m.hasMatch()) {
        if (!m.captured(1).trimmed().isEmpty())
            m_lastLang = m.captured(1).trimmed();
        c.beginEditBlock();
        c.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setBlockFormat(blockFmt(Code, 0, m_lastLang));
        c.setBlockCharFormat(QTextCharFormat());
        commit(c);
        return true;
    }
    if (trimmed == "---" || trimmed == "***" || trimmed == "___") {
        c.beginEditBlock();
        c.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setBlockFormat(blockFmt(Rule));
        c.insertBlock(QTextBlockFormat(), QTextCharFormat());
        c.endEditBlock();
        setTextCursor(c);
        return true;
    }
    return false;
}

bool Editor::handleBackspace(QTextCursor &c)
{
    const QTextBlock block = c.block();
    const Kind kind = kindOf(block);
    if (!c.atBlockStart()) {
        if (kind == Code) {     // through indentation, back to the previous tab stop
            const int col = c.positionInBlock();
            const QString before = block.text().left(col);
            if (before.trimmed().isEmpty() && !before.contains('\t')) {
                c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, (col - 1) % 4 + 1);
                c.removeSelectedText();
                return true;
            }
        }
        return false;
    }
    if (tableOf(block))
        return false;
    // At the start of a styled block, Backspace first takes the style away.
    if (kind == Heading || kind == Quote || kind == Rule) {
        c.setBlockFormat(QTextBlockFormat());
        c.setBlockCharFormat(QTextCharFormat());
        return true;
    }
    if (kind == Para && (block.textList() || block.blockFormat().indent() > 0)) {
        c.setBlockFormat(QTextBlockFormat());
        return true;
    }
    const QTextBlock prev = block.previous(), next = block.next();
    const bool alone = !(prev.isValid() && sameRun(prev, block)) && !(next.isValid() && sameRun(block, next));
    if (kind == Code && alone && block.text().isEmpty()) {
        c.setBlockFormat(QTextBlockFormat());
        return true;
    }
    if (!prev.isValid() || mergeable(prev, block))
        return false;
    if (kind == Code && kindOf(prev) == Code && langOf(prev) == langOf(block)) {
        // two cells of one language, back to back: make them one
        const int cell = prev.blockFormat().intProperty(PCell);
        QVector<QTextBlock> lines;
        const int last = runLast(block).blockNumber();
        for (QTextBlock b = block; b.isValid() && b.blockNumber() <= last; b = b.next())
            lines.append(b);
        c.beginEditBlock();
        for (const QTextBlock &b : lines) {
            QTextBlockFormat f = b.blockFormat();
            f.setProperty(PCell, cell);
            QTextCursor(b).setBlockFormat(f);
        }
        c.endEditBlock();
        return true;
    }
    if (emptyParagraph(prev)) {
        removeBlock(prev);              // take out the blank line above rather than restyle this block
        return true;
    }
    if (emptyParagraph(block))
        return false;                   // an empty paragraph simply goes away
    c.movePosition(QTextCursor::PreviousCharacter);
    setTextCursor(c);                   // step back without merging
    return true;
}

bool Editor::handleDelete(QTextCursor &c)
{
    if (!c.atBlockEnd())
        return false;
    const QTextBlock block = c.block(), next = block.next();
    if (!next.isValid() || tableOf(block) || tableOf(next) || mergeable(block, next))
        return false;
    if (emptyParagraph(block))
        removeBlock(block);
    else if (emptyParagraph(next))
        removeBlock(next);
    return true;                        // otherwise refuse: joining would restyle one as the other
}

void Editor::handleTab(QTextCursor &c, bool back)
{
    if (QTextTable *table = c.currentTable()) {     // Tab walks the cells, adding a row at the end
        const QTextTableCell cell = table->cellAt(c);
        int row = cell.row(), col = cell.column();
        if (!back) {
            if (++col >= table->columns()) {
                col = 0;
                if (++row >= table->rows())
                    table->appendRows(1);
            }
        } else if (--col < 0) {
            col = table->columns() - 1;
            if (--row < 0)
                return;
        }
        const QTextTableCell target = table->cellAt(row, col);
        QTextCursor n = target.firstCursorPosition();
        n.setPosition(target.lastCursorPosition().position(), QTextCursor::KeepAnchor);
        setTextCursor(n);
        return;
    }
    const QTextBlock block = c.block();
    if (kindOf(block) == Code)
        indentCode(c, back);
    else if (kindOf(block) == Para && block.textList())
        indentList(back ? -1 : 1);
    // elsewhere Tab does nothing: leading tabs have no meaning in the saved file
}

void Editor::indentCode(QTextCursor &c, bool back)
{
    QTextDocument *doc = document();
    c.beginEditBlock();
    if (!c.hasSelection() && !back) {
        c.insertText(QString(4 - c.positionInBlock() % 4, ' '));
    } else {
        const int first = doc->findBlock(c.selectionStart()).blockNumber();
        const QTextBlock endBlock = doc->findBlock(c.selectionEnd());
        int last = endBlock.blockNumber();
        if (c.hasSelection() && endBlock.position() == c.selectionEnd() && last > first)
            --last;
        for (int n = first; n <= last; ++n) {
            const QTextBlock b = doc->findBlockByNumber(n);
            if (kindOf(b) != Code)
                continue;
            QTextCursor bc(b);
            if (!back) {
                bc.insertText(QStringLiteral("    "));
                continue;
            }
            const QString t = b.text();
            int k = 0;
            while (k < 4 && k < t.size() && t[k] == ' ')
                ++k;
            if (!k && t.startsWith('\t'))
                k = 1;
            bc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, k);
            bc.removeSelectedText();
        }
    }
    c.endEditBlock();
    if (!c.hasSelection())
        setTextCursor(c);
}

// "# ", "- ", "1. ", "> ", "[] " at the start of a line format the line.
bool Editor::handleSpace(QTextCursor &c)
{
    if (c.hasSelection())
        return false;
    const QTextBlock block = c.block();
    if (kindOf(block) != Para || tableOf(block))
        return false;
    const QString before = block.text().left(c.positionInBlock());
    if (before.isEmpty() || before.size() > 6 || before.contains(QChar::ObjectReplacementCharacter))
        return false;
    static const RE box(QStringLiteral(R"(^\[( |x|X)?\]$)")), heading(QStringLiteral("^#{1,6}$")),
        number(QStringLiteral(R"(^\d+[.)]$)"));
    enum { Box, Head, Bullet, Number, QuoteMark } what;
    const auto boxMatch = box.match(before);
    if (boxMatch.hasMatch())
        what = Box;
    else if (block.textList())
        return false;
    else if (heading.match(before).hasMatch())
        what = Head;
    else if (before == "-" || before == "*" || before == "+")
        what = Bullet;
    else if (number.match(before).hasMatch())
        what = Number;
    else if (before == ">")
        what = QuoteMark;
    else
        return false;

    c.beginEditBlock();
    c.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    setTextCursor(c);
    switch (what) {
    case Box: {
        if (!block.textList())
            toggleList(false, true);
        QTextCursor bc(document()->findBlock(c.position()));
        QTextBlockFormat f = bc.blockFormat();
        f.setMarker(boxMatch.captured(1).toLower() == "x" ? QTextBlockFormat::MarkerType::Checked
                                                           : QTextBlockFormat::MarkerType::Unchecked);
        bc.setBlockFormat(f);
        break;
    }
    case Head: c.setBlockFormat(blockFmt(Heading, before.size())); break;
    case Bullet: toggleList(false); break;
    case Number: toggleList(true); break;
    case QuoteMark: c.setBlockFormat(blockFmt(Quote)); break;
    }
    c.endEditBlock();
    return true;
}

// Closing `code`, **bold**, *italic*, ~~struck~~ or $math$ formats it.
void Editor::inlineAutoformat()
{
    QTextCursor c = textCursor();
    const QTextBlock block = c.block();
    const Kind kind = kindOf(block);
    if (c.hasSelection() || !(kind == Para || kind == Heading || kind == Quote))
        return;
    struct Rule { RE rx; int delim; int style; };   // style -1: math
    static const QVector<Rule> rules = {
        {RE(QStringLiteral(R"((?<![`\\])`([^`]+)`$)")), 1, CodeSpan},
        {RE(QStringLiteral(R"((?<![*\\])\*\*([^*\s](?:[^*]*[^*\s])?)\*\*$)")), 2, Bold},
        {RE(QStringLiteral(R"((?<![*\\\w])\*([^*\s](?:[^*]*[^*\s])?)\*$)")), 1, Italic},
        {RE(QStringLiteral(R"((?<![~\\])~~([^~\s](?:[^~]*[^~\s])?)~~$)")), 2, Strike},
        {RE(QStringLiteral(R"((?<![$\\\w])\$([^$\s](?:[^$]*[^$\s])?)\$$)")), 1, -1},
    };
    const int col = c.positionInBlock();
    const QString before = block.text().left(col);
    for (const Rule &rule : rules) {
        const auto m = rule.rx.match(before);
        if (!m.hasMatch())
            continue;
        if (m.captured(0).contains(QChar::ObjectReplacementCharacter))
            return;
        const QString inner = m.captured(1);
        const int start = block.position() + m.capturedStart(0), end = block.position() + col;
        for (int p = start; p < end; ++p)
            if (formatAt(p).fontFixedPitch())
                return;             // already code: asterisks in there are just asterisks
        QTextCharFormat after = formatAt(start);
        c.beginEditBlock();
        if (rule.style < 0) {
            c.setPosition(start);
            c.setPosition(end, QTextCursor::KeepAnchor);
            c.insertText(QString(QChar::ObjectReplacementCharacter), mathFmt(inner, false));
        } else {
            c.setPosition(end - rule.delim);
            c.setPosition(end, QTextCursor::KeepAnchor);
            c.removeSelectedText();
            c.setPosition(start);
            c.setPosition(start + rule.delim, QTextCursor::KeepAnchor);
            c.removeSelectedText();
            c.setPosition(start);
            c.setPosition(start + inner.size(), QTextCursor::KeepAnchor);
            c.mergeCharFormat(charFmt(Style(rule.style)));
            c.setPosition(start + inner.size());
        }
        c.endEditBlock();
        setTextCursor(c);
        setCurrentCharFormat(after);    // typing carries on as it was before the opening mark
        return;
    }
}

// ---- mouse ------------------------------------------------------------------

QPointF Editor::docPoint(const QPoint &viewportPos) const
{
    return QPointF(viewportPos) + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
}

bool Editor::objectAt(const QPointF &point, int *pos, QRectF *rect, int *type) const
{
    for (auto it = m_objects.constBegin(); it != m_objects.constEnd(); ++it) {
        if (!it.value().first.contains(point))
            continue;
        if (document()->characterAt(it.key()) != QChar::ObjectReplacementCharacter
            || formatAt(it.key()).objectType() != it.value().second)
            continue;               // left over from before an edit
        *pos = it.key();
        *rect = it.value().first;
        *type = it.value().second;
        return true;
    }
    return false;
}

void Editor::selectObject(int pos)
{
    QTextCursor c(document());
    c.setPosition(pos);
    c.setPosition(pos + 1, QTextCursor::KeepAnchor);
    setTextCursor(c);
}

void Editor::setFigureWidth(int pos, qreal width)
{
    QTextCharFormat f = formatAt(pos);
    if (f.objectType() != ObjFigure)
        return;
    if (width > 0)
        f.setProperty(PWidth, width);
    else
        f.clearProperty(PWidth);
    QTextCursor c(document());
    c.setPosition(pos);
    c.setPosition(pos + 1, QTextCursor::KeepAnchor);
    c.setCharFormat(f);
}

// Qt 5 draws task-list checkboxes but does not make them clickable.
bool Editor::toggleMarkerAt(const QPoint &viewportPos)
{
    const QTextBlock block = cursorForPosition(viewportPos).block();
    const auto marker = block.blockFormat().marker();
    const QTextLayout *layout = block.layout();
    if (marker == QTextBlockFormat::MarkerType::NoMarker || !block.textList() || !layout || !layout->lineCount())
        return false;
    const QTextLine line = layout->lineAt(0);
    const qreal textX = layout->position().x() + line.naturalTextRect().left();
    const QRectF box(textX - theme.em() * 1.8, layout->position().y() + line.y(), theme.em() * 1.7, line.height());
    if (!box.contains(docPoint(viewportPos)))
        return false;
    QTextBlockFormat f = block.blockFormat();
    f.setMarker(marker == QTextBlockFormat::MarkerType::Checked ? QTextBlockFormat::MarkerType::Unchecked
                                                                : QTextBlockFormat::MarkerType::Checked);
    QTextCursor(block).setBlockFormat(f);
    return true;
}

void Editor::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        const QPointF point = docPoint(e->pos());
        QTextBlock head;
        bool play = false;
        if (tagAt(e->pos(), &head, &play)) {
            if (!play)
                languageMenu(head, e->globalPos());
            else if (cellState(head) == "running")
                interrupt();
            else
                enqueue(head);
            return;
        }
        int pos = 0, type = 0;
        QRectF rect;
        if (objectAt(point, &pos, &rect, &type)) {
            if (type == ObjFold && FoldHandler::playRect(rect).contains(point)) {
                const QTextBlock block = document()->findBlock(pos);
                if (cellState(block) == "running")
                    interrupt();
                else
                    enqueue(block);
                return;
            }
            selectObject(pos);
            if (type == ObjFigure && FigureHandler::handleRect(rect).adjusted(-4, -4, 0, 0).contains(point)) {
                m_dragPos = pos;
                m_dragStartX = point.x();
                m_dragStartWidth = rect.width();
                m_dragCentered = document()->findBlock(pos).blockFormat().alignment() & Qt::AlignHCenter;
            }
            return;
        }
        if (toggleMarkerAt(e->pos()))
            return;
        if (e->modifiers() & Qt::ControlModifier) {
            const QString href = anchorAt(e->pos());
            if (!href.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromUserInput(href, m_store.docDir()));
                return;
            }
        }
    }
    QTextEdit::mousePressEvent(e);
}

void Editor::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF point = docPoint(e->pos());
    if (m_dragPos >= 0) {       // dragging a figure's corner
        const qreal delta = (point.x() - m_dragStartX) * (m_dragCentered ? 2 : 1);
        resizePreview[m_dragPos] = qBound(24.0, m_dragStartWidth + delta, availWidth(m_dragPos));
        document()->markContentsDirty(m_dragPos, 1);
        return;
    }
    Qt::CursorShape shape = Qt::IBeamCursor;
    QTextBlock head;
    bool play = false;
    int pos = 0, type = 0;
    QRectF rect;
    if (tagAt(e->pos(), &head, &play)) {
        shape = Qt::PointingHandCursor;
    } else if (objectAt(point, &pos, &rect, &type)) {
        shape = Qt::ArrowCursor;
        if (type == ObjFigure && FigureHandler::handleRect(rect).adjusted(-4, -4, 0, 0).contains(point))
            shape = Qt::SizeFDiagCursor;
        else if (type == ObjFold)
            shape = Qt::PointingHandCursor;
    } else if ((e->modifiers() & Qt::ControlModifier) && !anchorAt(e->pos()).isEmpty()) {
        shape = Qt::PointingHandCursor;
    }
    if (viewport()->cursor().shape() != shape)
        viewport()->setCursor(shape);
    QTextEdit::mouseMoveEvent(e);
}

void Editor::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragPos >= 0) {
        const int pos = m_dragPos;
        const qreal width = resizePreview.value(pos, 0);
        m_dragPos = -1;
        resizePreview.remove(pos);
        if (width > 0)
            setFigureWidth(pos, width);
        return;
    }
    QTextEdit::mouseReleaseEvent(e);
}

void Editor::mouseDoubleClickEvent(QMouseEvent *e)
{
    int pos = 0, type = 0;
    QRectF rect;
    if (objectAt(docPoint(e->pos()), &pos, &rect, &type)) {
        selectObject(pos);
        if (type == ObjMath)
            editMath();
        else if (type == ObjFold)
            toggleFold();
        return;
    }
    QTextEdit::mouseDoubleClickEvent(e);
}

void Editor::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu menu(this);
    const QTextCursor at = cursorForPosition(e->pos());
    QTextCursor cur = textCursor();
    if (!cur.hasSelection() || at.position() < cur.selectionStart() || at.position() > cur.selectionEnd()) {
        int pos = 0, type = 0;
        QRectF rect;
        if (objectAt(docPoint(e->pos()), &pos, &rect, &type))
            selectObject(pos);
        else
            setTextCursor(at);
        cur = textCursor();
    }

    int start = 0, end = 0;
    QString word;
    if (!cur.hasSelection() && m_renderer->wordAt(at.block(), at.positionInBlock(), &start, &end, &word)) {
        const int base = at.block().position();
        for (const QString &fix : m_renderer->suggestions(word)) {
            menu.addAction(fix, this, [this, base, start, end, fix] {
                QTextCursor c(document());
                c.setPosition(base + start);
                c.setPosition(base + end, QTextCursor::KeepAnchor);
                c.insertText(fix);
            });
        }
        menu.addAction(tr("Add \"%1\" to Dictionary").arg(word), this, [this, word] { m_renderer->learn(word); });
        menu.addSeparator();
    }

    QAction *cutAct = menu.addAction(icon("edit-cut"), tr("Cu&t"), this, &QTextEdit::cut);
    QAction *copyAct = menu.addAction(icon("edit-copy"), tr("&Copy"), this, &QTextEdit::copy);
    cutAct->setEnabled(cur.hasSelection());
    copyAct->setEnabled(cur.hasSelection());
    menu.addAction(icon("edit-paste"), tr("&Paste"), this, &QTextEdit::paste);
    menu.addAction(tr("Paste as Plain Text"), this, &Editor::pastePlain);

    const QString href = anchorAt(e->pos());
    if (!href.isEmpty()) {
        menu.addSeparator();
        menu.addAction(tr("Open Link"), this, [this, href] { QDesktopServices::openUrl(QUrl::fromUserInput(href, m_store.docDir())); });
        menu.addAction(tr("Edit Link…"), this, &Editor::editLink);
    }
    const QTextCharFormat sel = cur.hasSelection() && cur.selectionEnd() - cur.selectionStart() == 1
        ? formatAt(cur.selectionStart()) : QTextCharFormat();
    if (sel.objectType() == ObjFigure) {
        menu.addSeparator();
        const int pos = cur.selectionStart();
        menu.addAction(tr("Original Size"), this, [this, pos] { setFigureWidth(pos, 0); })->setEnabled(sel.doubleProperty(PWidth) > 0);
    } else if (sel.objectType() == ObjMath) {
        menu.addSeparator();
        menu.addAction(tr("Edit Formula…"), this, [this] { editMath(); });
    }
    const QTextBlock head = cellHead(cur.block());
    if (head.isValid()) {
        menu.addSeparator();
        menu.addAction(icon("media-playback-start"), tr("Run Cell"), this, [this] { runCell(false); });
        menu.addAction(kindOf(head) == Fold ? tr("Show Code") : tr("Hide Code"), this, &Editor::toggleFold);
        menu.addAction(tr("Clear Output"), this, [this] { clearOutputs(false); });
    }
    if (cur.currentTable()) {
        menu.addSeparator();
        QMenu *table = menu.addMenu(tr("Table"));
        const QVector<QPair<QString, QString>> ops = {
            {tr("Insert Row Above"), "row-above"}, {tr("Insert Row Below"), "row-below"},
            {tr("Insert Column Left"), "col-left"}, {tr("Insert Column Right"), "col-right"}, {QString(), QString()},
            {tr("Delete Row"), "del-row"}, {tr("Delete Column"), "del-col"}, {tr("Delete Table"), "del-table"}};
        for (const auto &op : ops) {
            if (op.first.isEmpty())
                table->addSeparator();
            else
                table->addAction(op.first, this, [this, op] { tableOp(op.second); });
        }
    }
    menu.exec(e->globalPos());
}

// ---- clipboard --------------------------------------------------------------

QMimeData *Editor::createMimeDataFromSelection() const
{
    const QTextCursor c = textCursor();
    auto *data = new QMimeData;
    QString plain = c.selection().toPlainText();
    plain.remove(QChar::ObjectReplacementCharacter);
    data->setText(plain);
    const QString markdown = selectionMarkdown(c);
    data->setData(kMime, markdown.toUtf8());                // lossless between Jotter windows
    QTextDocument tmp;                                      // formatted, for other applications
    AssetStore loose;
    jot::loadMarkdown(&tmp, markdown, &loose);
    data->setHtml(bodyHtml(&tmp, loose, false));
    return data;
}

bool Editor::canInsertFromMimeData(const QMimeData *source) const
{
    return source->hasImage() || source->hasUrls() || source->hasFormat(kMime) || QTextEdit::canInsertFromMimeData(source);
}

void Editor::pasteMarkdownText(const QString &markdown, bool importImages, const QString &baseDir)
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    c.removeSelectedText();
    MarkdownReader reader(c, &m_store, importImages);
    reader.baseDir = baseDir;
    reader.insert(markdown);
    c = reader.cursor();
    c.endEditBlock();
    setTextCursor(c);
}

void Editor::insertFromMimeData(const QMimeData *source)
{
    QTextCursor c = textCursor();
    const Kind kind = kindOf(c.block());
    const bool verbatim = kind == Code || kind == Output || kind == Raw || c.currentTable();
    if (kind != Code && kind != Output && kind != Raw) {
        if (source->hasImage()) {                   // a screenshot on the clipboard
            insertImage(qvariant_cast<QImage>(source->imageData()));
            return;
        }
        bool any = false;
        QStringList documents;
        for (const QUrl &url : source->urls()) {    // files dropped (or copied) from the file manager
            const QString path = url.toLocalFile();
            if (path.isEmpty())
                continue;
            if (!QImageReader::imageFormat(path).isEmpty()) {
                insertImageFile(path);
                any = true;
            } else if (isDocument(path)) {
                documents << path;
            }
        }
        if (!documents.isEmpty()) {
            // Whether that means "open it" or "put it here" is the window's call,
            // and not one to make in the middle of a drop.
            QMetaObject::invokeMethod(this, [this, documents] { emit filesDropped(documents); }, Qt::QueuedConnection);
            return;
        }
        if (any)
            return;
    }
    if (verbatim) {
        QString text = source->text();
        text.replace("\r\n", "\n");
        if (c.currentTable())
            text = text.trimmed().replace('\n', ' ');
        c.insertText(text, c.currentTable() ? c.charFormat() : QTextCharFormat());
    } else if (source->hasFormat(kMime)) {
        pasteMarkdownText(QString::fromUtf8(source->data(kMime)), true);
    } else if (source->hasHtml()) {
        // Keep what formatting means (bold, headings, lists, links), drop the styling.
        QTextDocument tmp;
        tmp.setHtml(source->html());
        pasteMarkdownText(MarkdownWriter(&tmp).text(), false);
    } else if (source->hasText()) {
        c.insertText(source->text());
    }
    ensureCursorVisible();
}

// A Markdown (or plain text) file's content, formatted, at the cursor. Its
// pictures come along into this document's folder.
bool Editor::importFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit status(tr("Could not read %1: %2").arg(path, file.errorString()));
        return false;
    }
    QString text = QString::fromUtf8(file.readAll()), body;
    splitFrontMatter(text.replace("\r\n", "\n"), &body);
    const Kind kind = kindOf(textCursor().block());
    if (kind == Code || kind == Output || kind == Raw || kind == Fold || textCursor().currentTable())
        paragraphBelow();       // not into the middle of a cell or table
    pasteMarkdownText(body, true, QFileInfo(path).absolutePath());
    ensureCursorVisible();
    emit status(tr("Imported %1").arg(QFileInfo(path).fileName()));
    return true;
}

void Editor::pastePlain()
{
    const QMimeData *data = QApplication::clipboard()->mimeData();
    if (data && data->hasText())
        textCursor().insertText(data->text());
}

void Editor::pasteMarkdown()
{
    const QMimeData *data = QApplication::clipboard()->mimeData();
    if (data && data->hasText())
        pasteMarkdownText(data->text(), true);
}

// ---- painting ---------------------------------------------------------------
// Code bands, output and quote bars and rules are drawn under the text from the
// theme, so they never become part of the document.

QRectF Editor::bandRect(const QTextBlock &first, const QTextBlock &last) const
{
    const QTextDocument *doc = document();
    auto *layout = doc->documentLayout();
    const qreal x0 = doc->documentMargin(), x1 = layout->documentSize().width() - x0;
    const qreal pad = PadV * theme.layoutEm();
    const qreal top = layout->blockBoundingRect(first).top() - pad;
    const qreal bottom = layout->blockBoundingRect(last).bottom() + pad;
    return QRectF(x0, top, x1 - x0, bottom - top).adjusted(0.5, 0.5, -0.5, -0.5);
}

void Editor::tagRects(const QTextBlock &head, QRectF *play, QRectF *label) const
{
    const QRectF band = bandRect(head, runLast(head));
    const QFontMetricsF fm(theme.bodyFont(0.78));
    const QString state = cellState(head);
    QString text = langName(langOf(head));
    if (text.isEmpty())
        text = QStringLiteral("text");
    if (!state.isEmpty())
        text = state + QString::fromUtf8("\xE2\x80\xA6");
    const qreal h = fm.height() + 4, w = fm.horizontalAdvance(text) + 12;
    *label = QRectF(band.right() - w - 4, band.top() + 3, w, h);
    *play = QRectF(label->left() - h - 2, label->top(), h, h);
}

bool Editor::tagAt(const QPoint &viewportPos, QTextBlock *head, bool *play) const
{
    // The tag sits in the band's top padding, so the nearest line may be the
    // first code line or the block above it.
    const QPointF point = docPoint(viewportPos);
    const QTextBlock near = cursorForPosition(viewportPos).block();
    for (const QTextBlock &b : {near, near.next(), near.previous()}) {
        if (!b.isValid() || kindOf(b) != Code || tableOf(b))
            continue;
        const QTextBlock first = runFirst(b);
        QRectF playRect, labelRect;
        tagRects(first, &playRect, &labelRect);
        QString info, code;
        cellSource(first, &info, &code);
        const bool runnable = Kernel::canRun(info) || !cellState(first).isEmpty();
        if (runnable && playRect.contains(point)) {
            *head = first;
            *play = true;
            return true;
        }
        if (labelRect.contains(point)) {
            *head = first;
            *play = false;
            return true;
        }
    }
    return false;
}

void Editor::paintEvent(QPaintEvent *e)
{
    {
        QPainter p(viewport());
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF offset(horizontalScrollBar()->value(), verticalScrollBar()->value());
        p.translate(-offset);
        paintUnder(p, QRectF(e->rect()).translated(offset), e->rect().top());
    }
    QTextEdit::paintEvent(e);
}

void Editor::paintUnder(QPainter &p, const QRectF &clip, int firstVisibleY)
{
    const QTextDocument *doc = document();
    auto *layout = doc->documentLayout();
    const qreal x0 = doc->documentMargin(), x1 = layout->documentSize().width() - x0;
    QTextBlock b = runFirst(cursorForPosition(QPoint(0, firstVisibleY)).block());
    if (b.previous().isValid())
        b = runFirst(b.previous());     // a band's padding reaches above its first line
    while (b.isValid()) {
        const QRectF r = layout->blockBoundingRect(b);
        if (r.top() > clip.bottom() + theme.layoutEm() * 2)
            break;
        const Kind k = kindOf(b);
        if ((k == Code || k == Output || k == Quote) && !tableOf(b)) {
            const QTextBlock first = runFirst(b), last = runLast(b);
            const qreal top = layout->blockBoundingRect(first).top(), bottom = layout->blockBoundingRect(last).bottom();
            if (k == Code) {
                const QRectF band = bandRect(first, last);
                const QString state = cellState(first);
                QPainterPath path;
                path.addRoundedRect(band, 6, 6);
                p.fillPath(path, theme.codeBg);
                p.setPen(QPen(state.isEmpty() ? theme.codeBorder : theme.accent, 1));
                p.drawPath(path);

                QRectF play, label;
                tagRects(first, &play, &label);
                QString info, code;
                cellSource(first, &info, &code);
                QString text = langName(info);
                if (text.isEmpty())
                    text = QStringLiteral("text");
                if (!state.isEmpty())
                    text = state + QString::fromUtf8("\xE2\x80\xA6");
                p.fillRect(play.united(label).adjusted(-4, 0, 0, 0), theme.codeBg);     // keep long first lines legible
                p.setFont(theme.bodyFont(0.78));
                p.setPen(state.isEmpty() ? theme.muted : theme.accent);
                p.drawText(label, Qt::AlignCenter, text);
                p.setPen(Qt::NoPen);
                p.setBrush(state.isEmpty() ? theme.muted : theme.accent);
                const QPointF mid = play.center();
                const qreal s = play.height() * 0.22;
                if (state == "running")
                    p.drawRect(QRectF(mid.x() - s, mid.y() - s, 2 * s, 2 * s));
                else if (Kernel::canRun(info))
                    p.drawPolygon(QPolygonF({QPointF(mid.x() - s * 0.8, mid.y() - s), QPointF(mid.x() + s, mid.y()),
                                             QPointF(mid.x() - s * 0.8, mid.y() + s)}));
                p.setBrush(Qt::NoBrush);
            } else {
                bool error = false;
                if (k == Output)
                    for (QTextBlock x = first; x.isValid() && x.blockNumber() <= last.blockNumber(); x = x.next())
                        error = error || x.blockFormat().boolProperty(PErr);
                p.fillRect(QRectF(x0 + 1, top, 3, bottom - top), error ? theme.error : theme.rule);
            }
            b = last.next();
            continue;
        }
        if (k == Rule && !tableOf(b)) {
            p.setPen(QPen(theme.rule, 1.5));
            p.drawLine(QPointF(x0, r.center().y()), QPointF(x1, r.center().y()));
        }
        b = b.next();
    }
}

}  // namespace jot
