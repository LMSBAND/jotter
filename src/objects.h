// The things that sit in the text as a single character: figures, math, and
// folded (hidden-code) cells. Each has a handler that tells the layout how big
// it is and paints it.
#pragma once

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QTextObjectInterface>

namespace jot {

class AssetStore;
class Editor;

constexpr int HandleSize = 9;       // px of the resize grip on a selected figure

// Loaded pictures, and copies scaled to the size they are drawn at.
class ImageCache
{
public:
    explicit ImageCache(const AssetStore *store) : m_store(store) {}
    // Natural size in logical px. A picture that says it is high-dpi (plots are
    // saved at 192) is shown at its physical size, so it stays crisp on dense
    // screens and in print.
    bool info(const QString &src, QImage *image, QSizeF *natural);
    QImage scaled(const QString &src, int width, int height);

private:
    struct Entry { QDateTime modified; QImage image; QSizeF natural; };
    const AssetStore *m_store;
    QHash<QString, Entry> m_loaded;
    QHash<QString, QImage> m_scaled;
    QStringList m_scaledOrder;
};

class ObjectHandler : public QObject, public QTextObjectInterface
{
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    explicit ObjectHandler(Editor *editor);

protected:
    int selected(int pos) const;    // 0 no, 1 inside a larger selection, 2 selected on its own
    void wash(QPainter *painter, const QRectF &rect) const;
    Editor *ed;
};

class FigureHandler : public ObjectHandler
{
    Q_OBJECT
public:
    using ObjectHandler::ObjectHandler;
    QSizeF intrinsicSize(QTextDocument *doc, int pos, const QTextFormat &format) override;
    void drawObject(QPainter *painter, const QRectF &rect, QTextDocument *doc, int pos, const QTextFormat &format) override;
    static QRectF handleRect(const QRectF &rect);
};

class MathHandler : public ObjectHandler
{
    Q_OBJECT
public:
    using ObjectHandler::ObjectHandler;
    QSizeF intrinsicSize(QTextDocument *doc, int pos, const QTextFormat &format) override;
    void drawObject(QPainter *painter, const QRectF &rect, QTextDocument *doc, int pos, const QTextFormat &format) override;

private:
    struct Geometry { bool ready; qreal em, width, height, padTop; };
    Geometry geometry(QTextDocument *doc, int pos, const QTextFormat &format) const;
};

// A hidden cell: a small pill, so you can see (and run, and reopen) what is there.
class FoldHandler : public ObjectHandler
{
    Q_OBJECT
public:
    using ObjectHandler::ObjectHandler;
    QSizeF intrinsicSize(QTextDocument *doc, int pos, const QTextFormat &format) override;
    void drawObject(QPainter *painter, const QRectF &rect, QTextDocument *doc, int pos, const QTextFormat &format) override;
    static QRectF playRect(const QRectF &rect);

private:
    QFont font() const;
    QString label(const QTextFormat &format) const;
};

}  // namespace jot
