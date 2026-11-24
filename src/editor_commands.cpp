// What menus, the toolbar and shortcuts ask the editor to do.
#include "editor.h"

#include "markdown.h"
#include "mathtex.h"
#include "render.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextList>
#include <QTimer>
#include <QVBoxLayout>

namespace jot {

namespace {

bool isOrdered(const QTextList *list)
{
    const auto s = list->format().style();
    return s != QTextListFormat::ListDisc && s != QTextListFormat::ListCircle && s != QTextListFormat::ListSquare;
}

QTextListFormat::Style bulletFor(int level)
{
    static const QTextListFormat::Style bullets[] = {QTextListFormat::ListDisc, QTextListFormat::ListCircle,
                                                     QTextListFormat::ListSquare};
    return bullets[qBound(1, level, 3) - 1];
}

bool verbatim(Kind k)
{
    return k == Code || k == Output || k == Raw || k == Fold;
}

// What a cell printed, made fit for a document: no terminal colour codes, the
// last state of any progress bar, and not thousands of lines.
QStringList cleanOutput(QString text)
{
    static const QRegularExpression ansi(QStringLiteral(R"(\x1b\[[0-9;?]*[A-Za-z])"));
    text.remove(ansi);
    while (text.endsWith('\n') || text.endsWith('\r'))
        text.chop(1);
    QStringList lines = text.split('\n');
    for (QString &line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
        const int cr = line.lastIndexOf('\r');
        if (cr >= 0)
            line = line.mid(cr + 1);
    }
    const int limit = 500;
    if (lines.size() > limit) {
        const int cut = lines.size() - limit;
        lines = lines.mid(0, limit - 50) + QStringList(QString::fromUtf8("\xE2\x80\xA6 %1 more lines \xE2\x80\xA6").arg(cut))
            + lines.mid(lines.size() - 50);
    }
    return lines;
}

// A small box to type LaTeX in, with the typeset result underneath.
class MathDialog : public QDialog
{
public:
    MathDialog(QWidget *parent, const Theme &theme, const QString &tex, bool display) : QDialog(parent), m_theme(theme)
    {
        setWindowTitle(tr("Formula"));
        auto *layout = new QVBoxLayout(this);
        m_edit = new QPlainTextEdit(tex, this);
        m_edit->setFont(theme.monoFont());
        m_edit->setPlaceholderText(tr("LaTeX, for example  \\frac{a}{b}  or  e^{i\\pi} = -1"));
        m_edit->setTabChangesFocus(true);
        m_edit->setMinimumSize(420, 90);
        m_preview = new QLabel(this);
        m_preview->setAlignment(Qt::AlignCenter);
        m_preview->setMinimumHeight(70);
        m_preview->setWordWrap(true);
        m_display = new QCheckBox(tr("On its own line, centred"), this);
        m_display->setChecked(display);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        layout->addWidget(m_edit);
        layout->addWidget(m_preview, 1);
        layout->addWidget(m_display);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(250);
        connect(m_edit, &QPlainTextEdit::textChanged, timer, QOverload<>::of(&QTimer::start));
        connect(timer, &QTimer::timeout, this, [this] { refresh(); });
        connect(m_display, &QCheckBox::toggled, this, [this] { refresh(); });
        connect(MathRenderer::instance(), &MathRenderer::changed, this, [this] { refresh(); });
        refresh();
        m_edit->selectAll();
    }
    QString tex() const { return m_edit->toPlainText(); }
    bool display() const { return m_display->isChecked(); }

private:
    void refresh()
    {
        const QString t = tex().trimmed();
        m_preview->setStyleSheet(QString());
        if (t.isEmpty()) {
            m_preview->setText(QString());
            return;
        }
        const auto &f = MathRenderer::instance()->get(t, display());
        if (f.state == MathRenderer::Formula::Pending) {
            m_preview->setText(QString::fromUtf8("\xE2\x80\xA6"));
        } else if (f.state == MathRenderer::Formula::Failed) {
            m_preview->setStyleSheet(QString("color: %1").arg(m_theme.error.name()));
            m_preview->setText(f.error);
        } else {
            const qreal em = 20, dpr = devicePixelRatioF();
            QImage img = MathRenderer::tinted(f, palette().color(QPalette::WindowText), qRound(f.width * em * dpr),
                                              qRound(f.height * em * dpr));
            img.setDevicePixelRatio(dpr);
            m_preview->setPixmap(QPixmap::fromImage(img));
        }
    }
    Theme m_theme;
    QPlainTextEdit *m_edit;
    QLabel *m_preview;
    QCheckBox *m_display;
};

}  // namespace

// ---- what the cursor is in ---------------------------------------------------

Kind Editor::currentKind() const
{
    return kindOf(textCursor().block());
}

int Editor::currentHeading() const
{
    return textCursor().block().blockFormat().headingLevel();
}

bool Editor::hasStyle(Style style) const
{
    const QTextCursor c = textCursor();
    const QTextCharFormat f = c.hasSelection() ? formatAt(c.selectionStart()) : currentCharFormat();
    return stylesOf(f) & style;
}

int Editor::listState() const
{
    const QTextBlock block = textCursor().block();
    const QTextList *list = block.textList();
    if (!list)
        return 0;
    if (block.blockFormat().marker() != QTextBlockFormat::MarkerType::NoMarker)
        return 3;
    return isOrdered(list) ? 2 : 1;
}

QVector<QTextBlock> Editor::selectedBlocks(const QTextCursor &c) const
{
    QVector<QTextBlock> out;
    const QTextDocument *doc = document();
    QTextBlock b = doc->findBlock(c.selectionStart());
    const QTextBlock endBlock = doc->findBlock(c.selectionEnd());
    int last = endBlock.blockNumber();
    if (c.hasSelection() && endBlock.position() == c.selectionEnd() && last > b.blockNumber())
        --last;         // a selection ending at the very start of a line does not include that line
    for (; b.isValid() && b.blockNumber() <= last; b = b.next())
        out.append(b);
    return out;
}

// ---- character formatting ------------------------------------------------------

void Editor::toggleStyle(Style style)
{
    if (verbatim(currentKind()))
        return;
    const bool on = !hasStyle(style);
    QTextCharFormat d;
    switch (style) {
    case Bold: d.setFontWeight(on ? QFont::Bold : QFont::Normal); break;
    case Italic: d.setFontItalic(on); break;
    case Strike: d.setFontStrikeOut(on); break;
    case Underline: d.setFontUnderline(on); break;
    case CodeSpan: d.setFontFixedPitch(on); break;
    default: return;
    }
    mergeCurrentCharFormat(d);
}

void Editor::clearFormatting()
{
    QTextCursor c = textCursor();
    if (!c.hasSelection()) {
        setCurrentCharFormat(QTextCharFormat());
        return;
    }
    const int from = c.selectionStart(), to = c.selectionEnd();
    QTextCursor edit(document());
    edit.beginEditBlock();
    for (const QTextBlock &b : selectedBlocks(c)) {
        for (const QTextFragment &frag : fragments(b)) {
            if (frag.charFormat().objectType() != QTextFormat::NoObject)
                continue;       // pictures and formulas stay what they are
            const int a = qMax(from, frag.position()), z = qMin(to, frag.position() + frag.length());
            if (a >= z)
                continue;
            edit.setPosition(a);
            edit.setPosition(z, QTextCursor::KeepAnchor);
            edit.setCharFormat(QTextCharFormat());
        }
    }
    edit.endEditBlock();
}

void Editor::editLink()
{
    QTextCursor c = textCursor();
    if (verbatim(currentKind()))
        return;
    QString href;
    if (c.hasSelection()) {
        href = formatAt(c.selectionStart()).anchorHref();
    } else if (c.charFormat().isAnchor() || formatAt(c.position()).isAnchor()) {
        // inside a link: act on all of it
        href = c.charFormat().isAnchor() ? c.charFormat().anchorHref() : formatAt(c.position()).anchorHref();
        int a = c.position(), z = c.position();
        const QTextBlock b = c.block();
        while (a > b.position() && formatAt(a - 1).anchorHref() == href)
            --a;
        while (z < b.position() + b.length() - 1 && formatAt(z).anchorHref() == href)
            ++z;
        c.setPosition(a);
        c.setPosition(z, QTextCursor::KeepAnchor);
    }
    bool ok = false;
    const QString url = QInputDialog::getText(this, tr("Link"), tr("Address (empty removes the link):"), QLineEdit::Normal,
                                              href, &ok).trimmed();
    if (!ok)
        return;
    if (!c.hasSelection()) {
        if (!url.isEmpty())
            c.insertText(url, charFmt(stylesOf(currentCharFormat()), url));
        return;
    }
    QTextCharFormat d;
    d.setAnchor(!url.isEmpty());
    d.setAnchorHref(url);
    c.mergeCharFormat(d);
}

// ---- paragraph formatting --------------------------------------------------------

// Pictures and formulas can't live in code: put a formula's source back, and
// drop the character formatting.
void Editor::flatten(const QTextBlock &block)
{
    QTextCursor c(block);
    const auto frags = fragments(block);
    for (int i = frags.size() - 1; i >= 0; --i) {
        const QTextCharFormat cf = frags[i].charFormat();
        if (cf.objectType() == QTextFormat::NoObject)
            continue;
        c.setPosition(frags[i].position());
        c.setPosition(frags[i].position() + frags[i].length(), QTextCursor::KeepAnchor);
        c.insertText(cf.objectType() == ObjMath ? "$" + cf.stringProperty(PTex) + "$" : QString(), QTextCharFormat());
    }
    c.setPosition(block.position());
    c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    c.setCharFormat(QTextCharFormat());
}

void Editor::setBlockKind(Kind kind, int level)
{
    QTextCursor c = textCursor();
    QVector<QTextBlock> blocks;
    for (const QTextBlock &b : selectedBlocks(c))
        if (!tableOf(b) && kindOf(b) != Fold)
            blocks.append(b);
    if (blocks.isEmpty())
        return;
    if (kind == Quote || kind == Code) {    // these two buttons toggle
        bool all = true;
        for (const QTextBlock &b : blocks)
            all = all && kindOf(b) == kind;
        if (all)
            kind = Para;
    }
    const QTextBlockFormat fmt = blockFmt(kind, level, m_lastLang);    // one format: selected lines become one cell
    c.beginEditBlock();
    for (const QTextBlock &b : blocks) {
        if (kind == Code && kindOf(b) != Code)
            flatten(b);
        QTextCursor bc(b);
        const bool wasParen = kindOf(b) == Paren;
        bc.setBlockFormat(fmt);
        bc.setBlockCharFormat(QTextCharFormat());
        // A script's parenthetical is written in its brackets; they come and go with it.
        const QString text = b.text();
        if (kind == Paren && !wasParen) {
            if (!text.endsWith(')')) {
                bc.movePosition(QTextCursor::EndOfBlock);
                bc.insertText(QStringLiteral(")"));
            }
            if (!text.startsWith('(')) {
                bc.setPosition(b.position());
                bc.insertText(QStringLiteral("("));
            }
        } else if (wasParen && kind != Paren && text == "()") {
            bc.select(QTextCursor::LineUnderCursor);
            bc.removeSelectedText();
        }
    }
    c.endEditBlock();
    if (kind == Paren && !c.hasSelection()) {       // ...and are typed between
        QTextCursor in = textCursor();
        if (in.atBlockEnd() && in.block().text().endsWith(')')) {
            in.movePosition(QTextCursor::Left);
            setTextCursor(in);
        }
    }
    viewport()->update();
}

void Editor::toggleList(bool ordered, bool checklist)
{
    QTextCursor c = textCursor();
    QVector<QTextBlock> blocks;
    for (const QTextBlock &b : selectedBlocks(c)) {
        const Kind k = kindOf(b);
        if ((k == Para || k == Heading || k == Quote) && !tableOf(b))
            blocks.append(b);
    }
    if (blocks.isEmpty())
        return;
    auto isIt = [&](const QTextBlock &b) {
        const QTextList *list = b.textList();
        if (!list)
            return false;
        const bool box = b.blockFormat().marker() != QTextBlockFormat::MarkerType::NoMarker;
        return checklist ? box : (!box && isOrdered(list) == ordered);
    };
    bool all = true;
    for (const QTextBlock &b : blocks)
        all = all && isIt(b);

    c.beginEditBlock();
    if (all) {
        for (const QTextBlock &b : blocks)
            QTextCursor(b).setBlockFormat(QTextBlockFormat());
    } else {
        QTextList *list = nullptr;
        // carry on a list of the same kind directly above, so numbering continues
        const QTextBlock prev = blocks.first().previous();
        if (prev.isValid() && prev.textList() && prev.textList()->format().indent() == 1
            && isOrdered(prev.textList()) == (ordered && !checklist))
            list = prev.textList();
        for (const QTextBlock &b : blocks) {
            QTextBlockFormat fmt;
            if (checklist) {
                const auto was = b.blockFormat().marker();
                fmt.setMarker(was == QTextBlockFormat::MarkerType::NoMarker ? QTextBlockFormat::MarkerType::Unchecked : was);
            }
            QTextCursor bc(b);
            bc.setBlockFormat(fmt);
            if (!list) {
                QTextListFormat lf;
                lf.setIndent(1);
                lf.setStyle(ordered && !checklist ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
                list = bc.createList(lf);
            } else {
                list->add(b);
            }
        }
    }
    c.endEditBlock();
}

void Editor::indentList(int delta)
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    for (const QTextBlock &b : selectedBlocks(c)) {
        QTextList *list = b.textList();
        if (!list)
            continue;
        const QTextListFormat fmt = list->format();
        const int level = qBound(1, fmt.indent() + delta, 6);
        if (level == fmt.indent())
            continue;
        // join the neighbouring list at that depth (so siblings share numbering), else start one
        QTextList *target = nullptr;
        for (QTextBlock p = b.previous(); p.isValid() && p.textList(); p = p.previous()) {
            const QTextListFormat pf = p.textList()->format();
            if (pf.indent() == level && isOrdered(p.textList()) == isOrdered(list)) {
                target = p.textList();
                break;
            }
            if (pf.indent() < level)
                break;
        }
        if (target) {
            target->add(b);
        } else {
            QTextListFormat nf = fmt;
            nf.setIndent(level);
            if (!isOrdered(list))
                nf.setStyle(bulletFor(level));
            QTextCursor(b).createList(nf);
        }
    }
    c.endEditBlock();
}

// ---- inserting -----------------------------------------------------------------

void Editor::insertCell(const QString &code, const QString &lang, bool run)
{
    QTextCursor c = textCursor();
    const QString info = lang.isEmpty() ? m_lastLang : lang;
    c.beginEditBlock();
    c.clearSelection();
    if (QTextTable *table = c.currentTable())
        c.setPosition(table->lastPosition() + 1);
    const QTextBlock block = c.block();
    if (emptyParagraph(block)) {
        c.setBlockFormat(blockFmt(Code, 0, info));
        c.setBlockCharFormat(QTextCharFormat());
    } else {
        // below the block the cursor is in, or below the whole cell or run it belongs to
        const QTextBlock head = cellHead(block);
        const Kind k = kindOf(block);
        const QTextBlock tail = head.isValid() ? cellEnd(head) : (k == Para || k == Heading) ? block : runLast(block);
        c.setPosition(tail.position() + tail.length() - 1);
        c.insertBlock(blockFmt(Code, 0, info), QTextCharFormat());
    }
    c.insertText(code, QTextCharFormat());
    commit(c);
    ensureCursorVisible();
    setFocus();
    if (run)
        runCell(false);
}

void Editor::insertImageFile(const QString &path)
{
    if (verbatim(currentKind()))
        return;
    textCursor().insertText(QString(QChar::ObjectReplacementCharacter), figureFmt(m_store.addFile(path)));
    ensureCursorVisible();
}

void Editor::insertImage(const QImage &image)
{
    if (image.isNull() || verbatim(currentKind()))
        return;
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    textCursor().insertText(QString(QChar::ObjectReplacementCharacter), figureFmt(m_store.addBytes(data)));
    ensureCursorVisible();
}

void Editor::insertTable(int rows, int columns)
{
    QTextCursor c = textCursor();
    if (c.currentTable())
        return;
    c.beginEditBlock();
    c.clearSelection();
    if (!emptyParagraph(c.block())) {
        const QTextBlock block = c.block(), head = cellHead(block);
        const Kind k = kindOf(block);
        const QTextBlock tail = head.isValid() ? cellEnd(head) : (k == Para || k == Heading) ? block : runLast(block);
        c.setPosition(tail.position() + tail.length() - 1);
        c.insertBlock(QTextBlockFormat(), QTextCharFormat());
    }
    QTextTable *table = jot::insertTable(c, qMax(1, rows), qMax(1, columns));
    c.endEditBlock();
    setTextCursor(table->cellAt(0, 0).firstCursorPosition());
    ensureCursorVisible();
}

void Editor::tableOp(const QString &op)
{
    QTextCursor c = textCursor();
    QPointer<QTextTable> table = c.currentTable();
    if (!table)
        return;
    const QTextTableCell cell = table->cellAt(c);
    const int row = cell.row(), col = cell.column();
    c.beginEditBlock();
    if (op == "row-above")
        table->insertRows(row, 1);
    else if (op == "row-below")
        table->insertRows(row + 1, 1);
    else if (op == "col-left")
        table->insertColumns(col, 1);
    else if (op == "col-right")
        table->insertColumns(col + 1, 1);
    else if (op == "del-row")
        table->removeRows(row, 1);
    else if (op == "del-col")
        table->removeColumns(col, 1);
    else if (op == "del-table")
        table->removeRows(0, table->rows());
    if (table && op.contains("col")) {      // keep the columns equal
        QTextTableFormat f = table->format();
        f.setColumnWidthConstraints(QVector<QTextLength>(table->columns(),
                                                         QTextLength(QTextLength::PercentageLength, 100.0 / table->columns())));
        table->setFormat(f);
    }
    c.endEditBlock();
}

void Editor::editMath(bool display)
{
    QTextCursor c = textCursor();
    if (verbatim(kindOf(c.block())))
        return;
    const QTextDocument *doc = document();
    auto isMath = [&](int p) {
        return p >= 0 && doc->characterAt(p) == QChar::ObjectReplacementCharacter && formatAt(p).objectType() == ObjMath;
    };
    int pos = -1;       // an existing formula: selected, or right next to the cursor
    if (c.hasSelection() && c.selectionEnd() - c.selectionStart() == 1 && isMath(c.selectionStart()))
        pos = c.selectionStart();
    else if (!c.hasSelection() && isMath(c.position()))
        pos = c.position();
    else if (!c.hasSelection() && isMath(c.position() - 1))
        pos = c.position() - 1;

    QString tex;
    if (pos >= 0) {
        const QTextCharFormat f = formatAt(pos);
        tex = f.stringProperty(PTex);
        display = f.boolProperty(PDisplay);
    } else if (c.hasSelection()) {
        tex = c.selectedText();
        tex.remove(QChar::ObjectReplacementCharacter);
    }
    MathDialog dialog(this, theme, tex, display);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString out = dialog.tex().simplified();
    c.beginEditBlock();
    if (pos >= 0) {
        c.setPosition(pos);
        c.setPosition(pos + 1, QTextCursor::KeepAnchor);
    }
    c.removeSelectedText();
    if (!out.isEmpty()) {
        if (dialog.display() && c.block().length() > 1) {       // gets a paragraph to itself
            if (!c.atBlockEnd()) {
                c.insertBlock();
                c.movePosition(QTextCursor::PreviousBlock);
                c.movePosition(QTextCursor::EndOfBlock);
            }
            c.insertBlock(QTextBlockFormat(), QTextCharFormat());
        }
        c.insertText(QString(QChar::ObjectReplacementCharacter), mathFmt(out, dialog.display()));
    }
    c.endEditBlock();
    setTextCursor(c);
}

void Editor::insertRule()
{
    QTextCursor c = textCursor();
    if (c.currentTable())
        return;
    c.beginEditBlock();
    c.clearSelection();
    if (emptyParagraph(c.block())) {
        c.setBlockFormat(blockFmt(Rule));
    } else {
        const QTextBlock block = c.block(), head = cellHead(block);
        const QTextBlock tail = head.isValid() ? cellEnd(head) : runLast(block);
        c.setPosition(tail.position() + tail.length() - 1);
        c.insertBlock(blockFmt(Rule), QTextCharFormat());
    }
    c.insertBlock(QTextBlockFormat(), QTextCharFormat());
    c.endEditBlock();
    setTextCursor(c);
}

// A fresh paragraph after whatever the cursor is in: the way out of a cell or table.
void Editor::paragraphBelow()
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    c.clearSelection();
    if (QTextTable *table = c.currentTable()) {
        c.setPosition(table->lastPosition() + 1);
        if (!emptyParagraph(c.block())) {
            c.insertBlock(c.blockFormat(), c.blockCharFormat());
            c.movePosition(QTextCursor::PreviousBlock);
            c.setBlockFormat(QTextBlockFormat());
        }
    } else {
        const QTextBlock block = c.block(), head = cellHead(block);
        const QTextBlock tail = head.isValid() ? cellEnd(head) : runLast(block);
        c.setPosition(tail.position() + tail.length() - 1);
        c.insertBlock(QTextBlockFormat(), QTextCharFormat());
    }
    c.endEditBlock();
    setTextCursor(c);
    ensureCursorVisible();
}

// ---- cells ---------------------------------------------------------------------

void Editor::runCell(bool advance)
{
    const QTextBlock head = cellHead(textCursor().block());
    if (!head.isValid()) {
        emit status(tr("Put the cursor in a code cell to run it."));
        return;
    }
    enqueue(head);
    if (advance) {      // as in a notebook: move on to whatever follows the cell
        const QTextBlock after = cellEnd(head).next();
        QTextCursor c(document());
        if (after.isValid())
            c.setPosition(after.position());
        else
            c.movePosition(QTextCursor::End);
        setTextCursor(c);
        ensureCursorVisible();
    }
}

void Editor::runAll()
{
    for (const QTextBlock &head : cellHeads(document())) {
        QString info, code;
        cellSource(head, &info, &code);
        if (Kernel::canRun(info))
            enqueue(head);
    }
}

void Editor::enqueue(const QTextBlock &head)
{
    QString info, code;
    cellSource(head, &info, &code);
    if (!Kernel::canRun(info)) {
        const QString name = langName(info);
        emit status(name.isEmpty() ? tr("Give the cell a language to run it (click its tag).")
                                   : tr("Jotter does not know how to run \"%1\" on this machine.").arg(name));
        return;
    }
    if (!cellState(head).isEmpty())
        return;
    QTextCursor c(document());
    c.setPosition(head.position());
    m_queue.append(c);
    runNext();
}

void Editor::runNext()
{
    while (!m_isRunning && !m_queue.isEmpty()) {
        const QTextCursor c = m_queue.takeFirst();
        const QTextBlock head = cellHead(c.block());
        if (!head.isValid())
            continue;           // deleted while it waited
        QString info, code;
        cellSource(head, &info, &code);
        m_running = c;
        m_isRunning = true;
        const QString cwd = m_store.docPath.isEmpty() ? QDir::homePath() : m_store.docDir();
        m_kernel->run(info, code, cwd, m_store.writeDir(),
                      [this](const QVector<Kernel::Item> &items, bool ok) { finishRun(items, ok); });
    }
    viewport()->update();
    emit cellsChanged();
}

void Editor::finishRun(const QVector<Kernel::Item> &items, bool ok)
{
    const QTextBlock head = cellHead(m_running.block());
    m_isRunning = false;
    m_running = QTextCursor();
    if (head.isValid())
        setOutputs(head, items);
    if (!ok)
        m_queue.clear();        // like a notebook, an error stops the cells queued behind it
    runNext();
}

// Replace whatever the cell printed last time with what it printed now.
void Editor::setOutputs(const QTextBlock &head, const QVector<Kernel::Item> &items)
{
    QTextDocument *doc = document();
    QScrollBar *bar = verticalScrollBar();
    const QTextBlock tail = cellTail(head);
    const int tailEnd = tail.position() + tail.length() - 1;
    const QTextBlockFormat tailFormat = tail.blockFormat();
    const QTextCharFormat tailChar = tail.charFormat();
    const qreal before = doc->size().height();
    const bool above = doc->documentLayout()->blockBoundingRect(tail).bottom() < bar->value();
    const QTextCursor user = textCursor();
    const int userPos = user.position(), userAnchor = user.anchor();

    QTextCursor c(doc);
    c.beginEditBlock();
    QTextBlock first, last;
    if (cellOutputs(head, &first, &last)) {
        c.setPosition(tailEnd);
        c.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setBlockFormat(tailFormat);       // an empty last code line would otherwise become output
        c.setBlockCharFormat(tailChar);
    }
    c.setPosition(tailEnd);
    for (const Kernel::Item &item : items) {
        if (item.type == Kernel::Item::Image) {
            m_store.noteCreated(item.text);
            c.insertBlock(blockFmt(Output), QTextCharFormat());
            c.insertText(QString(QChar::ObjectReplacementCharacter), figureFmt("asset:" + item.text));
            continue;
        }
        const QTextBlockFormat fmt = blockFmt(Output, 0, QString(), item.type == Kernel::Item::Error);
        for (const QString &line : cleanOutput(item.text)) {
            c.insertBlock(fmt, QTextCharFormat());
            c.insertText(line, QTextCharFormat());
        }
    }
    c.endEditBlock();

    // Output arriving must not move the cursor someone is typing with...
    QTextCursor now = textCursor();
    if (userPos <= tailEnd && (now.position() != userPos || now.anchor() != userAnchor)) {
        now.setPosition(userAnchor);
        now.setPosition(userPos, QTextCursor::KeepAnchor);
        setTextCursor(now);
    }
    // ...nor shift the text they are reading further down.
    if (above)
        bar->setValue(bar->value() + qRound(doc->size().height() - before));
}

void Editor::interrupt()
{
    m_queue.clear();
    m_kernel->interrupt();
    viewport()->update();
    emit cellsChanged();
}

void Editor::restartKernel()
{
    m_queue.clear();
    m_kernel->restart();
    viewport()->update();
    emit cellsChanged();
    emit status(tr("Python session restarted: variables are gone."));
}

void Editor::clearOutputs(bool all)
{
    QVector<QTextBlock> heads;
    if (all) {
        heads = cellHeads(document());
    } else {
        const QTextBlock head = cellHead(textCursor().block());
        if (head.isValid())
            heads.append(head);
    }
    QTextCursor c(document());
    c.beginEditBlock();
    for (int i = heads.size() - 1; i >= 0; --i) {
        QTextBlock last;
        if (!cellOutputs(heads[i], nullptr, &last))
            continue;
        const QTextBlock tail = cellTail(heads[i]);
        const QTextBlockFormat tailFormat = tail.blockFormat();
        const QTextCharFormat tailChar = tail.charFormat();
        c.setPosition(tail.position() + tail.length() - 1);
        c.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setBlockFormat(tailFormat);
        c.setBlockCharFormat(tailChar);
    }
    c.endEditBlock();
}

// Take a cell out of the document, with whatever it printed. One step to undo.
void Editor::deleteCell(const QTextBlock &head)
{
    if (!head.isValid() || cellHead(head) != head)
        return;
    auto same = [&](const QTextCursor &c) {
        const QTextBlock other = cellHead(c.block());
        return other.isValid() && other.blockNumber() == head.blockNumber();
    };
    // It may be running or waiting to: nothing of that may land on the cell that takes its place.
    for (int i = m_queue.size() - 1; i >= 0; --i)
        if (same(m_queue[i]))
            m_queue.remove(i);
    if (m_isRunning && same(m_running)) {
        m_running = QTextCursor();      // what it prints from here on has nowhere to go
        m_queue.clear();
        m_kernel->interrupt();
    }

    const QTextBlock last = cellEnd(head), prev = head.previous(), next = last.next();
    const int start = head.position(), end = last.position() + last.length() - 1;
    auto plain = [](const QTextBlock &b) { return b.isValid() && !tableOf(b); };
    QTextCursor c(document());
    c.beginEditBlock();
    if (plain(prev)) {              // from the end of the line above, which stays as it is
        c.setPosition(start - 1);
        c.setPosition(end, QTextCursor::KeepAnchor);
        c.removeSelectedText();
    } else if (plain(next)) {       // nothing above: the line below moves up, as it is
        const QTextBlockFormat format = next.blockFormat();
        const QTextCharFormat chars = next.charFormat();
        c.setPosition(start);
        c.setPosition(end + 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setBlockFormat(format);
        c.setBlockCharFormat(chars);
    } else {                        // the cell is all there is: an empty paragraph is left
        c.setPosition(start);
        c.setPosition(end, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setBlockFormat(QTextBlockFormat());
        c.setBlockCharFormat(QTextCharFormat());
    }
    commit(c);
    viewport()->update();
    emit cellsChanged();
}

// Swap one cell between shown code and the small pill that stands for hidden
// code (its output stays either way). `start` is where the cell begins; the
// caller holds the edit block. Positions, not block handles, because the
// blocks do not survive the edit.
void Editor::foldCell(QTextCursor &c, int start)
{
    const QTextBlock head = document()->findBlock(start);
    QString info, code;
    cellSource(head, &info, &code);
    if (kindOf(head) == Code) {
        const QTextBlock tail = cellTail(head);
        c.setPosition(start);
        c.setPosition(tail.position() + tail.length() - 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setBlockFormat(blockFmt(Fold));
        c.setBlockCharFormat(QTextCharFormat());
        c.insertText(QString(QChar::ObjectReplacementCharacter), foldFmt(info, code));
    } else if (kindOf(head) == Fold) {
        c.setPosition(start);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setBlockFormat(blockFmt(Code, 0, info));
        c.setBlockCharFormat(QTextCharFormat());
        c.insertText(code, QTextCharFormat());
    }
}

void Editor::toggleFold()
{
    const QTextBlock head = cellHead(textCursor().block());
    if (!head.isValid())
        return;
    const int start = head.position();
    QTextCursor c(document());
    c.beginEditBlock();
    foldCell(c, start);
    c.endEditBlock();
    c.setPosition(start);
    setTextCursor(c);
}

// One click: a notebook becomes a report (or back). A single undo step.
void Editor::foldAll(bool hide)
{
    QVector<int> starts;
    for (const QTextBlock &head : cellHeads(document()))
        if ((kindOf(head) == Code) == hide)
            starts.append(head.position());
    if (starts.isEmpty())
        return;
    QTextCursor c(document());
    c.beginEditBlock();
    for (int i = starts.size() - 1; i >= 0; --i)    // last first, so the earlier positions stay put
        foldCell(c, starts[i]);
    c.endEditBlock();
    ensureCursorVisible();
}

bool Editor::hasVisibleCode() const
{
    for (const QTextBlock &head : cellHeads(document()))
        if (kindOf(head) == Code)
            return true;
    return false;
}

bool Editor::hasHiddenCode() const
{
    for (const QTextBlock &head : cellHeads(document()))
        if (kindOf(head) == Fold)
            return true;
    return false;
}

void Editor::setCellLanguage(const QTextBlock &head, const QString &lang)
{
    QVector<QTextBlock> blocks;         // collect first: changing the language changes what a run is
    const int last = cellTail(head).blockNumber();
    for (QTextBlock b = head; b.isValid() && b.blockNumber() <= last; b = b.next())
        blocks.append(b);
    QTextCursor c(document());
    c.beginEditBlock();
    if (kindOf(head) == Fold) {
        QTextCharFormat f;
        if (objectFormat(head, ObjFold, &f)) {
            f.setProperty(PLang, lang);
            c.setPosition(head.position());
            c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            c.setCharFormat(f);
        }
    } else {
        for (const QTextBlock &b : blocks) {
            QTextBlockFormat f = b.blockFormat();
            f.setProperty(PLang, lang);
            c.setPosition(b.position());
            c.setBlockFormat(f);
        }
    }
    c.endEditBlock();
    m_lastLang = lang;
    if (kindOf(head) == Code)
        m_renderer->rehighlightRun(head);
    viewport()->update();
}

void Editor::languageMenu(const QTextBlock &head, const QPoint &globalPos)
{
    QMenu menu(this);
    const QString current = langName(langOf(head));
    QTextCursor at(document());
    at.setPosition(head.position());
    auto choose = [this, at](const QString &lang) {
        const QTextBlock h = cellHead(at.block());
        if (h.isValid())
            setCellLanguage(h, lang);
    };
#ifdef Q_OS_WIN
    const char *const shell = "powershell";
#else
    const char *const shell = "bash";
#endif
    for (const char *lang : {"python", shell, "javascript", "c", "cpp", "lean", "dot", "json", "yaml", "html", "sql"}) {
        QAction *a = menu.addAction(QString::fromLatin1(lang), this, [choose, lang] { choose(QString::fromLatin1(lang)); });
        a->setCheckable(true);
        a->setChecked(current == QLatin1String(lang));
    }
    menu.addAction(tr("plain text"), this, [choose] { choose(QString()); });
    menu.addSeparator();
    menu.addAction(tr("Other…"), this, [this, choose, current] {
        bool ok = false;
        const QString lang = QInputDialog::getText(this, tr("Language"), tr("Language of this cell:"), QLineEdit::Normal, current, &ok);
        if (ok)
            choose(lang.trimmed());
    });
    menu.exec(globalPos);
}

}  // namespace jot
