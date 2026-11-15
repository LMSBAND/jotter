// Running code for the editor.
//
// Python cells go to one persistent kernel process per document
// (data/kernel_worker.py), so variables carry over between cells like in a
// notebook. Other languages are one-shot: write the cell to a temp file, run the
// tool, collect what it prints (and the picture, for diagram languages).
//
// Either way the result is a list of output items, in order.
#pragma once

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QVector>
#include <functional>

class QTextDecoder;

namespace jot {

// The Python that runs cells (and typesets formulas where there is no LaTeX):
// the machine's own if it can plot, otherwise the one shipped beside Jotter.
QString defaultPython();
QString bundledPython();            // "" unless Jotter was packaged with one
QProcessEnvironment pythonEnvironment(const QString &python);
// Helper scripts ship inside the binary; this gives one a file name.
QString scriptPath(const QString &name);

class Kernel : public QObject
{
    Q_OBJECT
public:
    struct Item {
        enum Type { Text, Error, Image } type;
        QString text;           // what was printed, or (Image) the file name written into figdir
    };
    using Done = std::function<void(const QVector<Item> &items, bool ok)>;

    explicit Kernel(QObject *parent = nullptr);
    ~Kernel() override;

    QString python;                                 // interpreter for Python cells; empty = defaultPython()

    static bool canRun(const QString &info);
    // Queue a cell. done(items, ok) is called when it finishes.
    void run(const QString &info, const QString &code, const QString &cwd, const QString &figdir, Done done);
    bool busy() const { return m_job != nullptr; }
    QString state() const { return m_state; }       // "off" | "idle" | "busy"

    void interrupt();       // stop the running cell and forget anything queued behind it
    void restart();         // throw away the Python session; the next cell starts a fresh one
    void shutdown();

signals:
    void stateChanged(const QString &state);

private:
    struct Job {
        QString info, code, cwd, figdir;
        Done done;
        QVector<Item> items;
        QString tmp;
        void add(const QString &s, Item::Type type = Item::Text);
    };
    void setState(const QString &state);
    void pump();
    void finish(bool ok);
    void runPython(Job *job);
    void runOnce(Job *job);
    void send(const QJsonObject &msg);
    void stopPython();
    void readPython();

    QString m_state = QStringLiteral("off");
    QQueue<Job *> m_queue;
    Job *m_job = nullptr;
    QProcess *m_py = nullptr;       // the kernel process
    QString m_pyCwd;
    QProcess *m_once = nullptr;     // a one-shot process
    QByteArray m_buf;
    QTextDecoder *m_decoder = nullptr;
    int m_seq = 0;
};

}  // namespace jot
