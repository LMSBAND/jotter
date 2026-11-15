#include "objects.h"

#include "assets.h"
#include "editor.h"
#include "kernel.h"
#include "mathtex.h"
#include "model.h"

#include <QFileInfo>
#include <QFontMetricsF>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>

namespace jot {

// ---- pictures ---------------------------------------------------------------

bool ImageCache::info(const QString &src, QImage *image, QSizeF *natural)
{
    const QString path = m_store->resolve(src);
    if (path.isEmpty())
        return false;
    const QFileInfo fi(path);
    if (!fi.isFile())
        return false;
    auto it = m_loaded.find(path);
    if (it == m_loaded.end() || it->modified != fi.lastModified()) {
        Entry e;
        e.modified = fi.lastModified();
        QImageReader reader(path);
        reader.setAutoTransform(true);
        e.image = reader.read();
        if (!e.image.isNull()) {
            const qreal dpi = e.image.dotsPerMeterX() * 0.0254;
            const qreal scale = dpi >= 120 ? 96.0 / dpi : 1.0;
            e.natural = QSizeF(e.image.width() * scale, e.image.height() * scale);
        }
        it = m_loaded.insert(path, e);
        for (const QString &key : qAsConst(m_scaledOrder))
            if (key.startsWith(path + "|"))
                m_scaled.remove(key);
    }
    if (it->image.isNull())
        return false;
    if (image)
        *image = it->image;
    if (natural)
        *natural = it->natural;
    return true;
}

QImage ImageCache::scaled(const QString &src, int width, int height)
{
    QImage full;
    if (!info(src, &full, nullptr))
        return QImage();
    const QString key = QString("%1|%2x%3").arg(m_store->resolve(src)).arg(width).arg(height);
    auto it = m_scaled.constFind(key);
    if (it != m_scaled.constEnd())
        return *it;
    const QImage img = full.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    m_scaled.insert(key, img);
    m_scaledOrder.append(key);
    while (m_scaledOrder.size() > 48)
        m_scaled.remove(m_scaledOrder.takeFirst());
    return img;
}

// ---- shared -----------------------------------------------------------------

ObjectHandler::ObjectHandler(Editor *editor) : QObject(editor), ed(editor) {}

int ObjectHandler::selected(int pos) const
{
    const QTextCursor c = ed->textCursor();
    if (!c.hasSelection() || pos < c.selectionStart() || pos >= c.selectionEnd())
        return 0;
    return c.selectionEnd() - c.selectionStart() == 1 ? 2 : 1;
}

void ObjectHandler::wash(QPainter *painter, const QRectF &rect) const
{
    QColor c = ed->theme.accent;
    c.setAlpha(70);
    painter->fillRect(rect, c);
}

// ---- figures ----------------------------------------------------------------

QSizeF FigureHandler::intrinsicSize(QTextDocument *, int pos, const QTextFormat &format)
{
    const qreal avail = ed->availWidth(pos);
    QSizeF natural;
    if (!ed->images().info(format.stringProperty(PSrc), nullptr, &natural))
        return QSizeF(qMin(avail, 300.0), 54);
    qreal width = ed->resizePreview.value(pos, 0);
    if (width <= 0)
        width = format.doubleProperty(PWidth);
    if (width <= 0)
        width = natural.width();
    width = qMax(16.0, qMin(width, avail));
    return QSizeF(width, qMax(1.0, width * natural.height() / natural.width()));
}

QRectF FigureHandler::handleRect(const QRectF &rect)
{
    return QRectF(rect.right() - HandleSize, rect.bottom() - HandleSize, HandleSize, HandleSize);
}

void FigureHandler::drawObject(QPainter *painter, const QRectF &rect, QTextDocument *, int pos, const QTextFormat &format)
{
    const Theme &th = ed->theme;
    const QString src = format.stringProperty(PSrc);
    ed->noteObject(pos, rect, ObjFigure);
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const QImage img = ed->images().scaled(src, qMax(1, qRound(rect.width() * dpr)), qMax(1, qRound(rect.height() * dpr)));
    painter->save();
    if (img.isNull()) {
        painter->setPen(QPen(th.muted, 1, Qt::DashLine));
        painter->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        const QFont f = th.bodyFont(0.85);
        painter->setFont(f);
        QString name = src;
        name.remove("asset:");
        const QString what = ed->store().resolve(src).isEmpty() ? "Picture on the web" : "Missing picture";
        painter->drawText(rect, Qt::AlignCenter,
                          QFontMetrics(f).elidedText(what + ": " + name, Qt::ElideMiddle, int(rect.width()) - 16));
    } else {
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawImage(rect, img);
    }
    const int sel = selected(pos);
    if (sel == 1) {
        wash(painter, rect);
    } else if (sel == 2) {
        painter->setPen(QPen(th.accent, 1.5));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(rect.adjusted(0.75, 0.75, -0.75, -0.75));
        painter->fillRect(handleRect(rect), th.accent);
    }
    painter->restore();
}

// ---- math -------------------------------------------------------------------

MathHandler::Geometry MathHandler::geometry(QTextDocument *doc, int pos, const QTextFormat &format) const
{
    const Theme &th = ed->theme;
    const QTextBlock block = doc->findBlock(pos);
    qreal em = th.em() * (kindOf(block) == Heading ? Theme::headingScale(block.blockFormat().headingLevel()) : 1.0);
    const bool display = format.boolProperty(PDisplay);
    const auto &f = MathRenderer::instance()->get(format.stringProperty(PTex), display);
    if (f.state != MathRenderer::Formula::Ready) {
        const QFontMetricsF fm(th.monoFont());
        return {false, em, fm.horizontalAdvance("$" + format.stringProperty(PTex) + "$") + 8, fm.height() + 2, 0};
    }
    // Computer Modern is small on the body next to a screen font: bring the x-heights closer.
    em *= display ? 1.25 : 1.15;
    const qreal w = f.width * em, h = f.height * em, depth = f.depth * em;
    if (display)
        return {true, em, w, h, 0};
    // Qt centres an inline object on a line a quarter x-height above the
    // baseline; pad one side so the formula's own baseline lands on the text's.
    const qreal shift = QFontMetrics(format.toCharFormat().font()).xHeight() / 2.0;
    const qreal above = h - depth, below = depth;
    const qreal padTop = qMax(0.0, below + shift - above), padBottom = qMax(0.0, above - below - shift);
    return {true, em, w, padTop + h + padBottom, padTop};
}

QSizeF MathHandler::intrinsicSize(QTextDocument *doc, int pos, const QTextFormat &format)
{
    const Geometry g = geometry(doc, pos, format);
    return QSizeF(g.width, g.height);
}

void MathHandler::drawObject(QPainter *painter, const QRectF &rect, QTextDocument *doc, int pos, const QTextFormat &format)
{
    const Theme &th = ed->theme;
    ed->noteObject(pos, rect, ObjMath);
    const bool display = format.boolProperty(PDisplay);
    const auto &f = MathRenderer::instance()->get(format.stringProperty(PTex), display);
    const Geometry g = geometry(doc, pos, format);
    painter->save();
    if (!g.ready) {
        // still being typeset (dim), or LaTeX rejected it (red)
        painter->setFont(th.monoFont());
        painter->setPen(f.state == MathRenderer::Formula::Failed && MathRenderer::available() ? th.error : th.muted);
        painter->drawText(rect, Qt::AlignCenter, "$" + format.stringProperty(PTex) + "$");
    } else {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const qreal fw = f.width * g.em, fh = f.height * g.em;
        const QImage img = ed->mathImage(format.stringProperty(PTex), display, th.text, qRound(fw * dpr), qRound(fh * dpr));
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawImage(QRectF(rect.left(), rect.top() + g.padTop, fw, fh), img);
    }
    if (selected(pos))
        wash(painter, rect);
    painter->restore();
}

// ---- folded cells -----------------------------------------------------------

QFont FoldHandler::font() const
{
    return ed->theme.bodyFont(0.82);
}

QString FoldHandler::label(const QTextFormat &format) const
{
    const int lines = format.stringProperty(PCode).count('\n') + 1;
    QString name = langName(format.stringProperty(PLang));
    if (name.isEmpty())
        name = "code";
    return QString::fromUtf8("%1  \xC2\xB7  %2 line%3 hidden").arg(name).arg(lines).arg(lines == 1 ? "" : "s");
}

QSizeF FoldHandler::intrinsicSize(QTextDocument *, int, const QTextFormat &format)
{
    const QFontMetricsF fm(font());
    return QSizeF(fm.horizontalAdvance(label(format)) + fm.height() * 2.2, fm.height() + 8);
}

QRectF FoldHandler::playRect(const QRectF &rect)
{
    return QRectF(rect.left(), rect.top(), rect.height() + 4, rect.height());
}

void FoldHandler::drawObject(QPainter *painter, const QRectF &rect, QTextDocument *doc, int pos, const QTextFormat &format)
{
    const Theme &th = ed->theme;
    ed->noteObject(pos, rect, ObjFold);
    const QString state = ed->cellState(doc->findBlock(pos));
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect.adjusted(0.5, 1.5, -0.5, -1.5);
    QPainterPath path;
    path.addRoundedRect(r, r.height() / 2, r.height() / 2);
    painter->fillPath(path, th.codeBg);
    painter->setPen(QPen(!state.isEmpty() || selected(pos) ? th.accent : th.codeBorder, 1));
    painter->drawPath(path);

    const QRectF pr = playRect(rect);
    const qreal cx = pr.center().x() + 2, cy = pr.center().y(), s = pr.height() * 0.2;
    painter->setPen(Qt::NoPen);
    painter->setBrush(state.isEmpty() ? th.muted : th.accent);
    if (state == "running") {
        painter->drawRect(QRectF(cx - s, cy - s, 2 * s, 2 * s));
    } else if (Kernel::canRun(format.stringProperty(PLang))) {
        painter->drawPolygon(QPolygonF({QPointF(cx - s * 0.8, cy - s), QPointF(cx + s, cy), QPointF(cx - s * 0.8, cy + s)}));
    }
    painter->setFont(font());
    painter->setPen(th.muted);
    QString text = label(format);
    if (!state.isEmpty()) {
        QString name = langName(format.stringProperty(PLang));
        text = QString::fromUtf8("%1  \xC2\xB7  %2\xE2\x80\xA6").arg(name.isEmpty() ? "code" : name, state);
    }
    painter->drawText(QRectF(pr.right(), rect.top(), rect.right() - pr.right(), rect.height()), Qt::AlignVCenter | Qt::AlignLeft, text);
    painter->restore();
}

}  // namespace jot
