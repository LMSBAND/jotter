// The page you type on: a QTextEdit that knows about cells, figures and math.
//
// editor.cpp           what happens as you type, click and paste; painting
// editor_commands.cpp  the things menus and the toolbar ask for
#pragma once

#include "assets.h"
#include "kernel.h"
#include "model.h"
#include "objects.h"
#include "theme.h"

#include <QSet>
#include <QTextEdit>

namespace jot {

class Renderer;

class Editor : public QTextEdit
{
    Q_OBJECT
public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    // ---- the document ------------------------------------------------------
    void loadMarkdown(const QString &text, const QString &path = QString(), bool importImages = false);
    QString toMarkdown(bool absolute = false, QSet<QString> *assets = nullptr) const;
    bool save(const QString &path, QString *error = nullptr);
    QString selectionMarkdown(const QTextCursor &cursor) const;
    AssetStore &store() { return m_store; }
    const AssetStore &store() const { return m_store; }
    QString frontMatter;

    // ---- looks -------------------------------------------------------------
    Theme theme;
    void applyTheme();
    Renderer *renderer() const { return m_renderer; }
    Kernel *kernel() const { return m_kernel; }

    // ---- used by the object handlers ---------------------------------------
    ImageCache &images() { return m_images; }
    qreal availWidth(int pos) const;
    QHash<int, qreal> resizePreview;        // figure position -> width while its grip is dragged
    void noteObject(int pos, const QRectF &rect, int type);
    QImage mathImage(const QString &tex, bool display, const QColor &color, int width, int height);
    QString cellState(const QTextBlock &block) const;   // "", "queued" or "running"

    // ---- what the cursor is in (for the toolbar) ---------------------------
    Kind currentKind() const;
    int currentHeading() const;
    bool hasStyle(Style style) const;
    int listState() const;                  // 0 none, 1 bullets, 2 numbers, 3 checklist
    bool cellBusy() const { return m_isRunning || !m_queue.isEmpty(); }
    bool hasVisibleCode() const;
    bool hasHiddenCode() const;
    QTextCharFormat formatAt(int pos) const;            // of the character at pos

public slots:
    void undoStep();
    void redoStep();
    // formatting
    void toggleStyle(jot::Style style);
    void setBlockKind(jot::Kind kind, int level = 0);
    void toggleList(bool ordered, bool checklist = false);
    void indentList(int delta);
    void clearFormatting();
    void editLink();
    // inserting
    void insertCell(const QString &code = QString(), const QString &lang = QString(), bool run = false);
    void insertImageFile(const QString &path);
    void insertImage(const QImage &image);
    void insertTable(int rows, int columns);
    void tableOp(const QString &op);        // row-above row-below col-left col-right del-row del-col del-table
    void editMath(bool display = false);    // the formula at the cursor, or a new one
    void insertRule();
    void paragraphBelow();
    void pastePlain();
    void pasteMarkdown();
    bool importFile(const QString &path);   // a Markdown or text file, into this document at the cursor
    // cells
    void runCell(bool advance = false);
    void runAll();
    void interrupt();
    void restartKernel();
    void clearOutputs(bool all = false);
    void toggleFold();
    void foldAll(bool hide);                // every cell's code at once

signals:
    void status(const QString &message);
    void cellsChanged();                    // something started or stopped running
    void zoomRequested(int steps);          // Ctrl+wheel
    void filesDropped(const QStringList &paths);    // Markdown/text files dropped or pasted onto the page

protected:
    bool event(QEvent *e) override;
    bool viewportEvent(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    QMimeData *createMimeDataFromSelection() const override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;

private:
    // keeping spacing in step with structure
    void onContentsChange(int pos, int removed, int added);
    void onContentsChanged();
    void dirtyBlocks(QTextBlock *first, QTextBlock *last) const;
    void normalizeRange(const QTextBlock &first, const QTextBlock &last, bool join);
    void replay(bool undo);

    // typing
    bool handleEnter(QTextCursor &c);
    bool handleBackspace(QTextCursor &c);
    bool handleDelete(QTextCursor &c);
    void handleTab(QTextCursor &c, bool back);
    bool handleSpace(QTextCursor &c);
    void indentCode(QTextCursor &c, bool back);
    void inlineAutoformat();

    // helpers
    QVector<QTextBlock> selectedBlocks(const QTextCursor &c) const;
    void flatten(const QTextBlock &block);
    void pasteMarkdownText(const QString &markdown, bool importImages, const QString &baseDir = QString());
    void commit(QTextCursor &c);
    QPointF docPoint(const QPoint &viewportPos) const;
    bool objectAt(const QPointF &point, int *pos, QRectF *rect, int *type) const;
    void selectObject(int pos);
    void setFigureWidth(int pos, qreal width);
    bool toggleMarkerAt(const QPoint &viewportPos);
    void layoutPage();

    // cells
    QRectF bandRect(const QTextBlock &first, const QTextBlock &last) const;
    void tagRects(const QTextBlock &head, QRectF *play, QRectF *label) const;
    bool tagAt(const QPoint &viewportPos, QTextBlock *head, bool *play) const;
    void languageMenu(const QTextBlock &head, const QPoint &globalPos);
    void setCellLanguage(const QTextBlock &head, const QString &lang);
    void enqueue(const QTextBlock &head);
    void runNext();
    void finishRun(const QVector<Kernel::Item> &items, bool ok);
    void setOutputs(const QTextBlock &head, const QVector<Kernel::Item> &items);
    void foldCell(QTextCursor &c, int start);
    void paintUnder(QPainter &p, const QRectF &clip, int firstVisibleY);

    AssetStore m_store;
    ImageCache m_images;
    Renderer *m_renderer = nullptr;
    Kernel *m_kernel = nullptr;
    QHash<int, QPair<QRectF, int>> m_objects;       // where inline objects were last painted (document coords)
    QHash<QString, QImage> m_mathImages;
    bool m_normalizing = false, m_replaying = false;
    int m_dirtyFrom = -1, m_dirtyTo = -1;
    QVector<QTextCursor> m_queue;                   // cells waiting to run
    QTextCursor m_running;
    bool m_isRunning = false;
    QString m_lastLang = QStringLiteral("python");
    int m_dragPos = -1;                             // figure being resized
    qreal m_dragStartX = 0, m_dragStartWidth = 0;
    bool m_dragCentered = false;
};

}  // namespace jot
