#include "kernel.h"

#include "model.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextCodec>
#include <csignal>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

// The worker script is compiled in as a Qt resource (data/resources.qrc).
static void initResources()
{
    Q_INIT_RESOURCE(resources);
}

namespace jot {

namespace {

const char kMark = 0x1e;    // starts a control record in the kernel's output stream

bool isPython(const QString &name)
{
    return name == "python" || name == "python3" || name == "py" || name == "ipython";
}

struct Runner { QString ext, command, prelude = QString(); };

// language -> how to run it. {file} is the cell's source, {out} a .png the tool
// may write, {exe} the program a compiler is to produce. A prelude is put in
// front of the cell's source.
const QHash<QString, Runner> &runners()
{
    static const QHash<QString, Runner> table = {
#ifdef Q_OS_WIN
        {"powershell", {".ps1", "powershell -NoProfile -ExecutionPolicy Bypass -File {file}"}},
        {"ps1", {".ps1", "powershell -NoProfile -ExecutionPolicy Bypass -File {file}"}},
        {"pwsh", {".ps1", "pwsh -NoProfile -File {file}"}},
        {"batch", {".bat", "cmd /D /C {file}"}},
        {"bat", {".bat", "cmd /D /C {file}"}},
        {"cmd", {".bat", "cmd /D /C {file}"}},
#endif
        {"bash", {".sh", "bash {file}"}},
        {"sh", {".sh", "sh {file}"}},
        {"zsh", {".sh", "zsh {file}"}},
        {"javascript", {".js", "node {file}"}},
        {"js", {".js", "node {file}"}},
        {"node", {".js", "node {file}"}},
        {"lua", {".lua", "lua {file}"}},
        {"ruby", {".rb", "ruby {file}"}},
        {"perl", {".pl", "perl {file}"}},
        {"r", {".R", "Rscript {file}"}},
        {"c", {".c", "gcc {file} -o {exe} -lm && {exe}"}},
        {"cpp", {".cpp", "g++ {file} -o {exe} && {exe}"}},
        {"c++", {".cpp", "g++ {file} -o {exe} && {exe}"}},
        {"rust", {".rs", "rustc {file} -o {exe} && {exe}"}},
        {"go", {".go", "go run {file}"}},
        {"lean", {".lean", "lean {file}"}},
        {"lean4", {".lean", "lean {file}"}},
        {"dot", {".dot", "dot -Tpng -Gdpi=192 -o {out} {file}"}},
        {"graphviz", {".dot", "dot -Tpng -Gdpi=192 -o {out} {file}"}},
        {"gnuplot", {".gp", "gnuplot {file}",
                     "set terminal pngcairo size 1228,920 fontscale 2 linewidth 2\nset output '{out}'\n"}},
        {"mermaid", {".mmd", "mmdc -i {file} -o {out} -s 2 -b transparent"}},
    };
    return table;
}

#ifdef Q_OS_WIN
const char *const kSystemPython = "python";     // (python3.exe there is the Store's "install Python" stub)

QString shellQuote(const QString &path)
{
    return '"' + QDir::toNativeSeparators(path) + '"';
}

// What a tool printed: UTF-8 from most, the console's code page from the
// system's own commands.
QString decodeOutput(const QByteArray &bytes)
{
    QTextCodec::ConverterState state;
    const QString text = QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
    if (state.invalidChars == 0 && state.remainingChars == 0)
        return text;
    QString wide(MultiByteToWideChar(CP_OEMCP, 0, bytes.constData(), bytes.size(), nullptr, 0), Qt::Uninitialized);
    MultiByteToWideChar(CP_OEMCP, 0, bytes.constData(), bytes.size(), reinterpret_cast<wchar_t *>(wide.data()), wide.size());
    return wide;
}
#else
const char *const kSystemPython = "python3";

QString shellQuote(QString s)
{
    return "'" + s.replace("'", "'\\''") + "'";
}

QString decodeOutput(const QByteArray &bytes)
{
    return QString::fromUtf8(bytes);
}
#endif

// A Lean cell inside a Lake project should see the project's imports.
QString lakeRoot(const QString &start)
{
    QDir d(start);
    do {
        if (d.exists("lakefile.lean") || d.exists("lakefile.toml"))
            return d.absolutePath();
    } while (d.cdUp());
    return QString();
}

}  // namespace

void Kernel::Job::add(const QString &s, Item::Type type)
{
    if (s.isEmpty())
        return;
    if (!items.isEmpty() && items.last().type == type && type != Item::Image)
        items.last().text += s;
    else
        items.append(Item{type, s});
}

Kernel::Kernel(QObject *parent) : QObject(parent) {}

QString bundledPython()
{
#ifdef Q_OS_WIN
    const QString path = QCoreApplication::applicationDirPath() + "/python/python.exe";
#else
    const QString path = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../python/bin/python3");
#endif
    return QFile::exists(path) ? path : QString();
}

QString defaultPython()
{
    static QString found;
    if (!found.isEmpty())
        return found;
    found = QString::fromLatin1(kSystemPython);
    const QString bundled = bundledPython();
    if (!bundled.isEmpty()) {
        // The machine's own Python has the user's other packages, so it wins if it can plot.
        QProcess probe;
        probe.start(found, {"-c", "import numpy, matplotlib"});
        const bool ok = probe.waitForFinished(10000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
        if (!ok) {
            probe.kill();
            probe.waitForFinished(500);
            found = bundled;
        }
    }
    return found;
}

QProcessEnvironment pythonEnvironment(const QString &python)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUNBUFFERED", "1");
    if (python == bundledPython()) {
        env.insert("PYTHONNOUSERSITE", "1");    // the shipped Python uses the packages shipped with it, nothing else
#ifdef Q_OS_WIN
        // ...and is the one that "!python" and "!pip" in a cell mean.
        const QString dir = QDir::toNativeSeparators(QFileInfo(python).absolutePath());
        env.insert("PATH", dir + ";" + dir + "\\Scripts;" + env.value("PATH"));
#endif
    }
    return env;
}

QString scriptPath(const QString &name)
{
    static QTemporaryDir dir(QDir::tempPath() + "/jotter-scripts-XXXXXX");
    initResources();
    const QString path = dir.path() + "/" + name;
    if (!QFile::exists(path))
        QFile::copy(":/" + name, path);
    return path;
}

Kernel::~Kernel()
{
    shutdown();
    delete m_decoder;
}

bool Kernel::canRun(const QString &info)
{
    const QString name = langName(info);
    if (isPython(name))
        return true;
    const auto it = runners().constFind(name);
    if (it == runners().constEnd())
        return false;
    return !QStandardPaths::findExecutable(it->command.section(' ', 0, 0)).isEmpty();
}

void Kernel::run(const QString &info, const QString &code, const QString &cwd, const QString &figdir, Done done)
{
    m_queue.enqueue(new Job{info, code, cwd, figdir, std::move(done), {}, QString()});
    pump();
}

void Kernel::interrupt()
{
    qDeleteAll(m_queue);
    m_queue.clear();
    if (!m_job)
        return;
    if (m_once)
        m_once->kill();
    else if (m_py && m_py->state() == QProcess::Running) {
#ifdef Q_OS_WIN
        send({{"op", "interrupt"}});        // no signals there: the worker raises it in itself
#else
        ::kill(pid_t(m_py->processId()), SIGINT);
#endif
    }
}

void Kernel::restart()
{
    qDeleteAll(m_queue);
    m_queue.clear();
    stopPython();
    if (m_job && !m_once) {
        m_job->add("Kernel restarted.", Item::Error);
        finish(false);
    }
    setState(m_job ? "busy" : "off");
}

void Kernel::shutdown()
{
    qDeleteAll(m_queue);
    m_queue.clear();
    delete m_job;
    m_job = nullptr;
    stopPython();
    if (m_once) {
        m_once->disconnect(this);
        m_once->kill();
        m_once->waitForFinished(500);
        delete m_once;
        m_once = nullptr;
    }
}

void Kernel::setState(const QString &state)
{
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void Kernel::pump()
{
    if (m_job || m_queue.isEmpty())
        return;
    m_job = m_queue.dequeue();
    setState("busy");
    if (isPython(langName(m_job->info)))
        runPython(m_job);
    else
        runOnce(m_job);
}

void Kernel::finish(bool ok)
{
    Job *job = m_job;
    m_job = nullptr;
    if (!job)
        return;
    if (!job->tmp.isEmpty())
        QDir(job->tmp).removeRecursively();
    setState(m_py ? "idle" : "off");
    job->done(job->items, ok);
    delete job;
    pump();
}

// ---- Python kernel ----------------------------------------------------------

void Kernel::runPython(Job *job)
{
    if (!m_py || m_py->state() == QProcess::NotRunning) {
        delete m_py;
        m_py = new QProcess(this);
        m_py->setProcessChannelMode(QProcess::MergedChannels);
        const QString exe = python.isEmpty() ? defaultPython() : python;
        m_py->setProcessEnvironment(pythonEnvironment(exe));
        m_py->setWorkingDirectory(job->cwd);
        connect(m_py, &QProcess::readyReadStandardOutput, this, &Kernel::readPython);
        connect(m_py, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
            m_py->deleteLater();
            m_py = nullptr;
            if (m_job && !m_once) {
                m_job->add(QString("The Python kernel stopped (exit code %1). It will restart on the next run.").arg(code), Item::Error);
                finish(false);
            } else {
                setState("off");
            }
        });
        connect(m_py, &QProcess::errorOccurred, this, [this, exe](QProcess::ProcessError err) {
            if (err != QProcess::FailedToStart)
                return;
            m_py->deleteLater();
            m_py = nullptr;
            if (m_job) {
                m_job->add(QString("Could not start Python (%1).").arg(exe), Item::Error);
                finish(false);
            }
        });
        m_pyCwd = job->cwd;
        m_buf.clear();
        delete m_decoder;
        m_decoder = QTextCodec::codecForName("UTF-8")->makeDecoder();
        m_py->start(exe, {"-u", scriptPath("kernel_worker.py")});
    } else if (job->cwd != m_pyCwd) {
        m_pyCwd = job->cwd;
        send({{"op", "chdir"}, {"path", job->cwd}});
    }
    send({{"op", "exec"}, {"id", ++m_seq}, {"code", job->code}, {"figdir", job->figdir}});
}

void Kernel::send(const QJsonObject &msg)
{
    if (m_py)
        m_py->write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n");
}

void Kernel::stopPython()
{
    if (!m_py)
        return;
    QProcess *p = m_py;
    m_py = nullptr;
    p->disconnect(this);
    p->kill();
    p->waitForFinished(1000);
    delete p;
}

void Kernel::readPython()
{
    // Everything the cell prints arrives verbatim; control records are
    // \x1e{json}\n and may start anywhere, even mid-line.
    if (!m_py)
        return;
    m_buf += m_py->readAllStandardOutput();
    auto text = [this](const QByteArray &bytes) {
        if (m_job)
            m_job->add(m_decoder->toUnicode(bytes));
    };
    while (!m_buf.isEmpty()) {
        const int mark = m_buf.indexOf(kMark);
        if (mark < 0) {
            text(m_buf);
            m_buf.clear();
            return;
        }
        if (mark > 0) {
            text(m_buf.left(mark));
            m_buf.remove(0, mark);
        }
        const int end = m_buf.indexOf('\n');
        if (end < 0)
            return;         // control record not complete yet
        const QByteArray record = m_buf.mid(1, end - 1);
        m_buf.remove(0, end + 1);
        const QJsonObject msg = QJsonDocument::fromJson(record).object();
        const QString type = msg.value("type").toString();
        if (!m_job) {
            continue;
        } else if (type == "image") {
            m_job->items.append(Item{Item::Image, msg.value("name").toString()});
        } else if (type == "error") {
            m_job->add(msg.value("text").toString(), Item::Error);
        } else if (type == "done" && msg.value("id").toInt() == m_seq) {
            finish(msg.value("ok").toBool());
            if (!m_py)
                return;
        }
    }
}

// ---- everything else --------------------------------------------------------

void Kernel::runOnce(Job *job)
{
    const QString name = langName(job->info);
    const Runner runner = runners().value(name);
    QTemporaryDir dir(QDir::tempPath() + "/jotter-run-XXXXXX");
    dir.setAutoRemove(false);
    job->tmp = dir.path();
    const QString src = job->tmp + "/cell" + runner.ext, out = job->tmp + "/out.png";
    QFile f(src);
    if (f.open(QIODevice::WriteOnly)) {
        if (runner.ext == ".ps1")
            f.write("\xEF\xBB\xBF");        // Windows PowerShell reads a script as UTF-8 only if it says so
        f.write(QString(runner.prelude).replace("{out}", QString(out).replace("'", "''")).toUtf8());
        f.write(job->code.toUtf8() + "\n");
    }
    f.close();

    QString cwd = job->cwd, command = runner.command;
    if (name.startsWith("lean")) {
        const QString root = lakeRoot(cwd);
        if (!root.isEmpty()) {
            cwd = root;
            command = "lake env " + command;
        }
    }
#ifdef Q_OS_WIN
    const QString exe = job->tmp + "/a.exe";
#else
    const QString exe = job->tmp + "/a.out";
#endif
    command.replace("{file}", shellQuote(src)).replace("{out}", shellQuote(out)).replace("{exe}", shellQuote(exe));

    QProcess *p = m_once = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    p->setWorkingDirectory(cwd);
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, p, out](int code, QProcess::ExitStatus status) {
        if (p != m_once)
            return;
        m_once = nullptr;
        p->deleteLater();
        Job *job = m_job;
        if (!job)
            return;
        const bool ok = status == QProcess::NormalExit && code == 0;
        const QString printed = decodeOutput(p->readAllStandardOutput());
        if (!printed.trimmed().isEmpty())
            job->add(printed, ok ? Item::Text : Item::Error);
        QFile pic(out);
        if (pic.open(QIODevice::ReadOnly)) {
            const QByteArray data = pic.readAll();
            const QString picName = "out-" + QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().left(12)) + ".png";
            QDir().mkpath(job->figdir);
            QFile dest(job->figdir + "/" + picName);
            if (!dest.exists() && dest.open(QIODevice::WriteOnly))
                dest.write(data);
            job->items.append(Item{Item::Image, picName});
        }
        if (!ok && job->items.isEmpty())
            job->add(status != QProcess::NormalExit ? QString("Stopped.") : QString("Exit code %1.").arg(code), Item::Error);
        finish(ok);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError err) {
        if (p != m_once || err != QProcess::FailedToStart)
            return;
        m_once = nullptr;
        p->deleteLater();
        if (m_job) {
            m_job->add("Could not start the shell.", Item::Error);
            finish(false);
        }
    });
#ifdef Q_OS_WIN
    p->setNativeArguments("/D /S /C \"" + command + "\"");     // (/S: the command line is taken as written)
    p->start("cmd.exe", QStringList());
#else
    p->start("sh", {"-c", command});
#endif
    p->closeWriteChannel();
}

}  // namespace jot
