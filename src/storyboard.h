// The storyboard: a strip of frames down the side of the window, one for each
// line that has one, to doodle in. What is drawn belongs to the document (see
// model.h); this is the panel that shows the frames and holds the pens.
#pragma once

#include "model.h"

#include <QWidget>

class QButtonGroup;
class QScrollArea;
class QToolButton;

namespace jot {

class Editor;
class Sheet;
struct Theme;

QColor inkColor(QChar ink, const Theme &theme);
// A drawing, fitted to rect (which has the shape of the sheet it was drawn on).
void paintDrawing(QPainter *painter, const QRectF &rect, const Drawing &drawing, const Theme &theme);

class Storyboard : public QWidget
{
    Q_OBJECT
public:
    explicit Storyboard(Editor *editor, QWidget *parent = nullptr);
    QSize sizeHint() const override { return QSize(320, 480); }

    void addFrame();                    // for the line the cursor is on; brought into view
    void reveal(int frame);

    // what is in hand
    QChar ink() const { return m_ink; }
    int penWidth() const { return m_width; }
    bool erasing() const { return m_erasing; }
    int current() const { return m_current; }       // the frame of the line the cursor is on, or -1

    // where things are, on the widget the frames are painted on
    QWidget *sheet() const;
    QRect canvas(int frame) const;      // the part to draw in
    QRect caption(int frame) const;     // its line's text: a click goes to the line
    QRect closeBox(int frame) const;    // the x that takes the frame away

private:
    void paintIcons();
    void follow();

    Editor *m_editor;
    Sheet *m_sheet;
    QScrollArea *m_scroll;
    QToolButton *m_add;
    QButtonGroup *m_inks, *m_widths;
    QChar m_ink = QLatin1Char('k');
    int m_width = 6;
    bool m_erasing = false;
    int m_current = -1;
};

}  // namespace jot
