#include "storyboard.h"

#include "editor.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

namespace jot {

QColor inkColor(QChar ink, const Theme &theme)
{
    switch (ink.unicode()) {
    case 'r': return QColor("#e5484d");
    case 'b': return QColor("#3b82f6");
    case 'g': return QColor("#2f9e44");
    case 'y': return QColor("#f0a500");
    default: return theme.text;
    }
}

void paintDrawing(QPainter *painter, const QRectF &rect, const Drawing &drawing, const Theme &theme)
{
    if (drawing.isEmpty() || rect.width() <= 0)
        return;
    const qreal scale = rect.width() / BoardW;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setClipRect(rect, Qt::IntersectClip);
    painter->translate(rect.topLeft());
    painter->scale(scale, scale);
    painter->setBrush(Qt::NoBrush);
    for (const Stroke &stroke : drawing) {
        // (drawn small, in the page's margin, a thin line is still a line)
        painter->setPen(QPen(inkColor(stroke.ink, theme), qMax(qreal(stroke.width), 1 / scale), Qt::SolidLine, Qt::RoundCap,
                             Qt::RoundJoin));
        if (stroke.points.size() == 1) {
            painter->drawPoint(stroke.points[0]);
            continue;
        }
        // Through the middle of each stretch, bending at the points: a hand's line rather than a polygon.
        QPainterPath path(stroke.points[0]);
        for (int i = 1; i < stroke.points.size(); ++i) {
            const QPointF a = stroke.points[i - 1], b = stroke.points[i];
            path.quadTo(a, (a + b) / 2);
        }
        path.lineTo(stroke.points.last());
        painter->drawPath(path);
    }
    painter->restore();
}

namespace {

const int Pad = 10;             // px around the frames
const int Gap = 14;             // ...and between them
const int Head = 24;            // height of the line above a frame: its number, its line's text, its x
const int Reach = 14;           // how far the eraser reaches, on the sheet's own scale

qreal distance(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal length2 = QPointF::dotProduct(ab, ab);
    const qreal t = length2 > 0 ? qBound(0.0, QPointF::dotProduct(p - a, ab) / length2, 1.0) : 0;
    return QLineF(p, a + t * ab).length();
}

bool touches(const Stroke &stroke, const QPoint &p)
{
    const qreal reach = Reach + stroke.width / 2.0;
    for (int i = 0; i < stroke.points.size(); ++i)
        if (distance(p, stroke.points[qMax(0, i - 1)], stroke.points[i]) <= reach)
            return true;
    return false;
}

// What a frame's line says, for the line above the frame.
QString lineText(const QTextBlock &block)
{
    QString text = block.text();
    if (kindOf(block) == Fold) {
        QString info, code;
        cellSource(block, &info, &code);
        text = code.section('\n', 0, 0);
    }
    text.remove(QChar::ObjectReplacementCharacter);
    return text.simplified();
}

}  // namespace

// The frames, one under the other, painted on one widget that is as tall as
// they are (the panel scrolls it).
class Sheet : public QWidget
{
public:
    Sheet(Storyboard *board, Editor *editor) : QWidget(board), m_board(board), m_ed(editor)
    {
        setMouseTracking(true);
        setFocusPolicy(Qt::NoFocus);        // the keys stay with the page
    }

    QRect card(int i) const
    {
        const int w = qMax(40, width() - 2 * Pad), h = Head + qRound(w * qreal(BoardH) / BoardW);
        return QRect(Pad, Pad + i * (h + Gap), w, h);
    }
    QRect canvas(int i) const { return card(i).adjusted(0, Head, 0, 0); }
    QRect caption(int i) const { return QRect(card(i).left(), card(i).top(), card(i).width() - Head, Head); }
    QRect closeBox(int i) const { return QRect(card(i).right() + 1 - 18, card(i).top() + (Head - 18) / 2, 18, 18); }

    void arrange()
    {
        const int n = m_ed->boards().size();
        if (m_drawing >= n)
            m_drawing = -1;         // the frame being drawn in went (an undo, say)
        setMinimumHeight(n ? card(n - 1).bottom() + 1 + Pad : 0);
        update();
    }

protected:
    void resizeEvent(QResizeEvent *) override { arrange(); }

    void paintEvent(QPaintEvent *e) override
    {
        const Theme &theme = m_ed->theme;
        const auto boards = m_ed->boards();
        QPainter p(this);
        p.fillRect(e->rect(), theme.canvas);
        if (boards.isEmpty()) {
            p.setPen(theme.muted);
            p.drawText(rect().adjusted(2 * Pad, 3 * Pad, -2 * Pad, 0), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                       Storyboard::tr("No frames yet.\n\nPut the cursor on a line and press Add Frame: the line gets a frame to draw in."));
            return;
        }
        QFont bold = font();
        bold.setBold(true);
        p.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < boards.size(); ++i) {
            if (!card(i).intersects(e->rect()))
                continue;
            const bool current = i == m_board->current();
            const QRect head = caption(i);
            const QString number = QString::number(i + 1);
            const int numberWidth = QFontMetrics(bold).horizontalAdvance(number) + 8;
            p.setFont(bold);
            p.setPen(current ? theme.accent : theme.muted);
            p.drawText(head, Qt::AlignLeft | Qt::AlignVCenter, number);
            QString text = lineText(boards[i].block);
            p.setFont(font());
            p.setPen(text.isEmpty() ? theme.muted : theme.text);
            if (text.isEmpty())
                text = Storyboard::tr("(no text)");
            const QRect words = head.adjusted(numberWidth, 0, -4, 0);
            p.drawText(words, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(text, Qt::ElideRight, words.width()));
            const QPointF x = QRectF(closeBox(i)).center();
            p.setPen(QPen(theme.muted, 1.4, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(x + QPointF(-3.5, -3.5), x + QPointF(3.5, 3.5));
            p.drawLine(x + QPointF(-3.5, 3.5), x + QPointF(3.5, -3.5));

            const QRect area = canvas(i);
            p.setPen(QPen(current ? theme.accent : theme.rule, current ? 2 : 1));
            p.setBrush(theme.paper);
            p.drawRect(QRectF(area).adjusted(0.5, 0.5, -0.5, -0.5));
            p.setBrush(Qt::NoBrush);
            paintDrawing(&p, area, i == m_drawing ? m_live : parseDrawing(boards[i].drawing), theme);
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        const int i = frameAt(e->pos());
        if (e->button() != Qt::LeftButton || i < 0)
            return;
        if (closeBox(i).contains(e->pos())) {
            m_ed->removeBoard(i);
        } else if (!canvas(i).contains(e->pos())) {
            m_ed->showBoardLine(i);
        } else {
            m_drawing = i;
            m_live = parseDrawing(m_ed->boards()[i].drawing);
            if (!m_board->erasing()) {
                Stroke stroke;
                stroke.ink = m_board->ink();
                stroke.width = m_board->penWidth();
                m_live.append(stroke);
            }
            trace(e->pos());
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_drawing >= 0) {
            trace(e->pos());
            return;
        }
        const int i = frameAt(e->pos());
        setCursor(i < 0 ? Qt::ArrowCursor : canvas(i).contains(e->pos()) ? Qt::CrossCursor : Qt::PointingHandCursor);
    }

    // One stroke of the pen (or one rub of the eraser) is one edit of the document: one step to undo.
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton || m_drawing < 0)
            return;
        const int i = m_drawing;
        m_drawing = -1;
        m_ed->drawBoard(i, drawingText(m_live));
        m_live.clear();
        update();
    }

private:
    int frameAt(const QPoint &pos) const
    {
        for (int i = 0, n = m_ed->boards().size(); i < n; ++i)
            if (card(i).contains(pos))
                return i;
        return -1;
    }

    // The pen (or the eraser) is at pos, in the frame being drawn in.
    void trace(const QPoint &pos)
    {
        const QRect area = canvas(m_drawing);
        const QPoint at(qBound(0, qRound((pos.x() - area.left()) * qreal(BoardW) / area.width()), BoardW),
                        qBound(0, qRound((pos.y() - area.top()) * qreal(BoardH) / area.height()), BoardH));
        if (m_board->erasing()) {
            for (int i = m_live.size() - 1; i >= 0; --i)
                if (touches(m_live[i], at))
                    m_live.remove(i);
        } else if (!m_live.isEmpty()) {
            QVector<QPoint> &points = m_live.last().points;
            if (points.isEmpty() || (points.last() - at).manhattanLength() >= 3)      // (a point for every pixel would be a lot to keep)
                points.append(at);
        }
        update(area);
    }

    Storyboard *m_board;
    Editor *m_ed;
    int m_drawing = -1;         // the frame the pen is down in
    Drawing m_live;             // ...and what is in it, with the stroke being made
};

Storyboard::Storyboard(Editor *editor, QWidget *parent) : QWidget(parent), m_editor(editor)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto *top = new QHBoxLayout, *pens = new QHBoxLayout;
    top->setContentsMargins(6, 4, 6, 0);
    pens->setContentsMargins(6, 2, 6, 4);
    pens->setSpacing(1);
    auto tool = [this](QHBoxLayout *row, const QString &name, const QString &tip, bool checkable) {
        auto *b = new QToolButton(this);
        b->setObjectName(name);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setCheckable(checkable);
        b->setIconSize(QSize(18, 18));
        b->setFocusPolicy(Qt::NoFocus);
        row->addWidget(b);
        return b;
    };
    m_add = tool(top, "add", tr("A frame for the line the cursor is on"), false);
    m_add->setText(tr("Add Frame"));
    m_add->setToolButtonStyle(Qt::ToolButtonTextOnly);
    connect(m_add, &QToolButton::clicked, this, &Storyboard::addFrame);
    top->addStretch(1);

    m_inks = new QButtonGroup(this);
    const QStringList names = {tr("Ink"), tr("Red"), tr("Blue"), tr("Green"), tr("Yellow")};
    for (int i = 0; BoardInks[i]; ++i) {
        const QChar ink = QLatin1Char(BoardInks[i]);
        QToolButton *b = tool(pens, QStringLiteral("ink-") + ink, names.value(i), true);
        m_inks->addButton(b);
        connect(b, &QToolButton::clicked, this, [this, ink] {
            m_ink = ink;
            m_erasing = false;
        });
    }
    pens->addSpacing(8);
    m_widths = new QButtonGroup(this);
    const QStringList sizes = {tr("Fine"), tr("Medium"), tr("Broad")};
    int n = 0;
    for (const int width : {3, 6, 12}) {
        QToolButton *b = tool(pens, QStringLiteral("width-%1").arg(width), sizes.value(n++), true);
        m_widths->addButton(b);
        connect(b, &QToolButton::clicked, this, [this, width] { m_width = width; });
    }
    pens->addSpacing(8);
    QToolButton *eraser = tool(pens, "eraser", tr("Rub out the lines it touches"), true);
    eraser->setText(tr("Eraser"));
    eraser->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_inks->addButton(eraser);          // in hand instead of a pen
    connect(eraser, &QToolButton::clicked, this, [this] { m_erasing = true; });
    pens->addStretch(1);
    findChild<QToolButton *>("ink-k")->setChecked(true);
    findChild<QToolButton *>("width-6")->setChecked(true);

    m_sheet = new Sheet(this, editor);
    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);       // (coming and going, it would change the frames' size)
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_scroll->setWidget(m_sheet);
    column->addLayout(top);
    column->addLayout(pens);
    column->addWidget(m_scroll, 1);
    setMinimumWidth(240);

    connect(editor, &Editor::boardsChanged, this, [this] {
        m_sheet->arrange();
        follow();
    });
    connect(editor->document(), &QTextDocument::contentsChanged, m_sheet, QOverload<>::of(&QWidget::update));  // what the lines say
    connect(editor, &QTextEdit::cursorPositionChanged, this, &Storyboard::follow);
    connect(editor, &Editor::themeChanged, this, [this] {
        paintIcons();
        m_sheet->update();
    });
    paintIcons();
    follow();
}

QWidget *Storyboard::sheet() const { return m_sheet; }
QRect Storyboard::canvas(int frame) const { return m_sheet->canvas(frame); }
QRect Storyboard::caption(int frame) const { return m_sheet->caption(frame); }
QRect Storyboard::closeBox(int frame) const { return m_sheet->closeBox(frame); }

// The pens as they will write: each ink on the paper's colour, each width as a dot.
void Storyboard::paintIcons()
{
    const Theme &theme = m_editor->theme;
    const qreal ratio = devicePixelRatioF();
    const int size = 18;
    auto icon = [&](const std::function<void(QPainter &)> &draw) {
        QPixmap picture(QSize(size, size) * ratio);
        picture.setDevicePixelRatio(ratio);
        picture.fill(Qt::transparent);
        QPainter p(&picture);
        p.setRenderHint(QPainter::Antialiasing);
        draw(p);
        return QIcon(picture);
    };
    const QPointF middle(size / 2.0, size / 2.0);
    for (QAbstractButton *b : m_inks->buttons()) {
        if (!b->objectName().startsWith("ink-"))
            continue;
        const QColor ink = inkColor(b->objectName().back(), theme);
        b->setIcon(icon([&](QPainter &p) {
            p.setPen(QPen(theme.muted, 1));
            p.setBrush(theme.paper);
            p.drawRoundedRect(QRectF(0.5, 0.5, size - 1, size - 1), 4, 4);
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            p.drawEllipse(middle, 5, 5);
        }));
    }
    const QColor dot = palette().color(QPalette::ButtonText);
    for (QAbstractButton *b : m_widths->buttons()) {
        const qreal radius = b->objectName().section('-', 1).toInt() / 2.4 + 0.5;
        b->setIcon(icon([&](QPainter &p) {
            p.setPen(Qt::NoPen);
            p.setBrush(dot);
            p.drawEllipse(middle, radius, radius);
        }));
    }
}

// The cursor moved (or the frames changed): which frame is its line's now?
void Storyboard::follow()
{
    const int now = m_editor->boardAtCursor();
    m_add->setEnabled(now < 0 && !m_editor->textCursor().currentTable());
    if (now == m_current)
        return;
    m_current = now;
    m_sheet->update();
    if (now >= 0)
        reveal(now);
}

void Storyboard::addFrame()
{
    reveal(m_editor->addBoard());
}

void Storyboard::reveal(int frame)
{
    QTimer::singleShot(0, this, [this, frame] {     // (once the strip is as tall as its frames)
        if (frame < 0 || frame >= m_editor->boards().size())
            return;
        const QRect card = m_sheet->card(frame);
        m_scroll->ensureVisible(card.center().x(), card.center().y(), 0, card.height() / 2 + Pad);
    });
}

}  // namespace jot
