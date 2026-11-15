// LaTeX math -> pictures. Real LaTeX (pdflatex + pdftocairo) where the machine
// has it; otherwise matplotlib's formula renderer through Python.
//
// Formulas are typeset in batches in the background, kept as alpha masks at a
// fixed size, and cached on disk. Whoever draws them tints and scales the mask,
// so one render serves light paper, dark paper, any zoom level and export.
#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QStringList>

class QProcess;
class QTemporaryDir;

namespace jot {

class MathRenderer : public QObject
{
    Q_OBJECT
public:
    struct Formula {
        enum State { Pending, Ready, Failed } state = Pending;
        QImage mask;                            // Format_Alpha8 coverage
        qreal width = 0, height = 0, depth = 0; // in em; depth = part below the baseline
        QString error;
    };
    static constexpr int RasterEm = 50;         // mask pixels per em: enough for 2x screens and print

    static MathRenderer *instance();
    static bool available();                    // there is something to typeset with

    const Formula &get(const QString &tex, bool display);   // starts typesetting if not seen before
    bool waitForAll(int msecs);                 // for export: block until nothing is pending
    static QImage tinted(const Formula &f, const QColor &color, int width, int height);
    static QByteArray png(const Formula &f);    // black on transparent

signals:
    void changed();                             // something that was pending is now ready or failed

private:
    explicit MathRenderer(QObject *parent = nullptr);
    ~MathRenderer() override;
    void startBatch();
    void latexDone();
    void rasterDone();
    void finishLatex();
    void finishMathtext(const QString &said);
    void fail(const QString &key, const QString &message);
    void store(const QString &key, const QImage &picture, qreal depthEm);
    static QString cacheFile(const QString &key);

    QHash<QString, Formula> m_cache;            // key: 'D' or 'I' + tex
    QStringList m_pending, m_batch;
    QProcess *m_proc = nullptr;
    QTemporaryDir *m_dir = nullptr;
    bool m_scheduled = false;
};

}  // namespace jot
