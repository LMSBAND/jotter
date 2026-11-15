#include "mathtex.h"

#include "kernel.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

namespace jot {

namespace {

const qreal kPtPerEm = 10.0;        // the LaTeX batch document is set in 10pt
const qreal kPadPt = 1.00375;       // 1bp of margin around each formula, in TeX points
const int kDpi = 360;               // 10pt at 360dpi = 50px per em

// Real LaTeX where the machine has it; otherwise matplotlib's formula renderer,
// which needs nothing Jotter does not already use for plots.
enum Engine { NoEngine, Latex, Mathtext };

Engine engine()
{
    static const Engine e = [] {
        if (!QStandardPaths::findExecutable("pdflatex").isEmpty() && !QStandardPaths::findExecutable("pdftocairo").isEmpty())
            return Latex;
        const QString python = defaultPython();
        if (QFile::exists(python) || !QStandardPaths::findExecutable(python).isEmpty())
            return Mathtext;
        return NoEngine;
    }();
    return e;
}

bool balanced(const QString &tex)
{
    int depth = 0;
    for (int i = 0; i < tex.size(); ++i) {
        if (tex[i] == '\\') {
            ++i;
        } else if (tex[i] == '{') {
            ++depth;
        } else if (tex[i] == '}') {
            if (--depth < 0)
                return false;
        }
    }
    return depth == 0;
}

}  // namespace

MathRenderer::MathRenderer(QObject *parent) : QObject(parent) {}

MathRenderer::~MathRenderer()
{
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(500);
    }
    delete m_dir;
}

MathRenderer *MathRenderer::instance()
{
    static MathRenderer *self = new MathRenderer(QCoreApplication::instance());
    return self;
}

bool MathRenderer::available()
{
    return engine() != NoEngine;
}

QString MathRenderer::cacheFile(const QString &key)
{
    // (the two engines draw differently, so they do not share cached pictures)
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/jotter/math";
    QDir().mkpath(dir);
    const QByteArray id = (engine() == Latex ? "latex|" : "mathtext|") + key.toUtf8();
    return dir + "/" + QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex()) + ".png";
}

const MathRenderer::Formula &MathRenderer::get(const QString &tex, bool display)
{
    const QString key = (display ? "D" : "I") + tex.simplified();
    auto it = m_cache.find(key);
    if (it != m_cache.end())
        return *it;
    Formula f;
    QImage cached(cacheFile(key));
    const QStringList metrics = cached.text("jotter").split(' ');
    if (!cached.isNull() && metrics.size() == 3) {
        f.state = Formula::Ready;
        f.mask = cached.convertToFormat(QImage::Format_Alpha8);
        f.width = metrics[0].toDouble();
        f.height = metrics[1].toDouble();
        f.depth = metrics[2].toDouble();
    } else if (engine() == NoEngine) {
        f.state = Formula::Failed;
        f.error = "formulas need LaTeX (pdflatex, pdftocairo) or Python with matplotlib";
    } else if (!balanced(tex)) {
        f.state = Formula::Failed;
        f.error = "unbalanced braces";
    } else {
        m_pending << key;
        if (!m_scheduled) {
            m_scheduled = true;
            QTimer::singleShot(30, this, [this] {
                m_scheduled = false;
                startBatch();
            });
        }
    }
    return *m_cache.insert(key, f);
}

bool MathRenderer::waitForAll(int msecs)
{
    QElapsedTimer t;
    t.start();
    while ((!m_pending.isEmpty() || m_proc) && t.elapsed() < msecs) {
        if (!m_proc)
            startBatch();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return m_pending.isEmpty() && !m_proc;
}

void MathRenderer::fail(const QString &key, const QString &message)
{
    Formula &f = m_cache[key];
    f.state = Formula::Failed;
    f.error = message;
}

void MathRenderer::store(const QString &key, const QImage &picture, qreal depthEm)
{
    Formula &f = m_cache[key];
    f.state = Formula::Ready;
    f.mask = picture.convertToFormat(QImage::Format_Alpha8);
    f.width = qreal(picture.width()) / RasterEm;
    f.height = qreal(picture.height()) / RasterEm;
    f.depth = depthEm;
    QImage keep = picture;
    keep.setText("jotter", QString("%1 %2 %3").arg(f.width).arg(f.height).arg(f.depth));
    keep.save(cacheFile(key), "PNG");
}

// Every pending formula is typeset in one run of the engine.
void MathRenderer::startBatch()
{
    if (m_proc || m_pending.isEmpty())
        return;
    m_batch = m_pending.mid(0, 60);
    m_pending = m_pending.mid(m_batch.size());
    delete m_dir;
    m_dir = new QTemporaryDir(QDir::tempPath() + "/jotter-math-XXXXXX");

    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(m_dir->path());
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            engine() == Latex ? latexDone() : rasterDone();
    });
    QTimer::singleShot(30000, m_proc, &QProcess::kill);

    if (engine() == Mathtext) {
        QJsonArray batch;
        for (const QString &key : qAsConst(m_batch))
            batch.append(QJsonObject{{"tex", key.mid(1)}, {"display", key.startsWith('D')}});
        QFile f(m_dir->path() + "/batch.json");
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(batch).toJson());
        f.close();
        const QString python = defaultPython();
        m_proc->setProcessEnvironment(pythonEnvironment(python));
        connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MathRenderer::rasterDone);
        m_proc->start(python, {scriptPath("mathtext_worker.py"), m_dir->path()});
        return;
    }

    // LaTeX: one document, each formula on a page cropped to it.
    QString tex = "\\documentclass{article}\n\\usepackage{amsmath,amssymb}\n\\pagestyle{empty}\n"
                  "\\pdfhorigin=1bp \\pdfvorigin=1bp\n\\newsavebox\\jbox\n\\begin{document}\n";
    for (int i = 0; i < m_batch.size(); ++i) {
        const bool display = m_batch[i].startsWith('D');
        tex += QString("\\typeout{JOTTER-BEGIN %1}\n\\setbox\\jbox=\\hbox{$%2%3$}\n"
                       "\\typeout{JOTTER-SIZE %1 \\the\\wd\\jbox\\space\\the\\ht\\jbox\\space\\the\\dp\\jbox}\n"
                       "\\pdfpagewidth=\\dimexpr\\wd\\jbox+2bp\\relax \\pdfpageheight=\\dimexpr\\ht\\jbox+\\dp\\jbox+2bp\\relax\n"
                       "\\shipout\\box\\jbox\n")
                   .arg(i).arg(display ? "\\displaystyle " : "", m_batch[i].mid(1));
    }
    tex += "\\end{document}\n";
    QFile f(m_dir->path() + "/batch.tex");
    if (f.open(QIODevice::WriteOnly))
        f.write(tex.toUtf8());
    f.close();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("openin_any", "p");      // a formula has no business reading or writing other files
    env.insert("openout_any", "p");
    m_proc->setProcessEnvironment(env);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MathRenderer::latexDone);
    m_proc->start("pdflatex", {"-no-shell-escape", "-interaction=nonstopmode", "batch.tex"});
}

void MathRenderer::latexDone()
{
    m_proc->deleteLater();
    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(m_dir->path());
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MathRenderer::rasterDone);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            rasterDone();
    });
    if (!QFile::exists(m_dir->path() + "/batch.pdf")) {
        rasterDone();
        return;
    }
    m_proc->start("pdftocairo", {"-png", "-r", QString::number(kDpi), "-transp", "batch.pdf", "page"});
}

void MathRenderer::rasterDone()
{
    QString said;
    if (m_proc) {
        said = QString::fromUtf8(m_proc->readAll()).trimmed();
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    if (engine() == Mathtext)
        finishMathtext(said);
    else
        finishLatex();
    m_batch.clear();
    emit changed();
    startBatch();
}

void MathRenderer::finishLatex()
{
    // The log says, per formula, whether TeX complained and how big the box was.
    QFile logFile(m_dir->path() + "/batch.log");
    const QString log = logFile.open(QIODevice::ReadOnly) ? QString::fromUtf8(logFile.readAll()).remove('\r') : QString();
    QStringList pages = QDir(m_dir->path()).entryList({"page-*.png"}, QDir::Files);
    std::sort(pages.begin(), pages.end(), [](const QString &a, const QString &b) {
        return a.mid(5).section('.', 0, 0).toInt() < b.mid(5).section('.', 0, 0).toInt();
    });
    static const QRegularExpression size(QStringLiteral(R"(JOTTER-SIZE (\d+) ([\d.]+)pt ([\d.]+)pt ([\d.]+)pt)"));
    static const QRegularExpression error(QStringLiteral(R"(^! (.*)$)"), QRegularExpression::MultilineOption);

    int page = 0;
    bool derailed = false;      // after a formula TeX could not recover from, the rest must be redone
    for (int i = 0; i < m_batch.size(); ++i) {
        const QString key = m_batch[i];
        if (derailed) {
            m_pending << key;
            continue;
        }
        const int begin = log.indexOf(QString("JOTTER-BEGIN %1\n").arg(i));
        int end = log.indexOf(QString("JOTTER-BEGIN %1\n").arg(i + 1));
        if (end < 0)
            end = log.size();
        const QString section = begin < 0 ? QString() : log.mid(begin, end - begin);
        const auto sm = size.match(section);
        const auto em = error.match(section);
        if (!sm.hasMatch()) {
            fail(key, em.hasMatch() ? em.captured(1).trimmed() : QString("could not be typeset"));
            derailed = true;
            continue;
        }
        const QImage img = page < pages.size() ? QImage(m_dir->path() + "/" + pages[page]) : QImage();
        ++page;
        if (em.hasMatch())
            fail(key, em.captured(1).trimmed());
        else if (img.isNull())
            fail(key, "could not be typeset");
        else
            store(key, img, (sm.captured(4).toDouble() + kPadPt) / kPtPerEm);
    }
}

void MathRenderer::finishMathtext(const QString &said)
{
    QFile out(m_dir->path() + "/out.json");
    const QJsonArray results = out.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(out.readAll()).array() : QJsonArray();
    for (int i = 0; i < m_batch.size(); ++i) {
        const QString key = m_batch[i];
        const QJsonObject r = results.at(i).toObject();
        QFile raw(m_dir->path() + QString("/%1.gray").arg(i));
        const int w = r.value("w").toInt(), h = r.value("h").toInt();
        if (r.isEmpty()) {          // the renderer itself did not run: say why
            fail(key, said.isEmpty() ? QString("could not be typeset") : said.section('\n', -1));
        } else if (r.contains("error")) {
            fail(key, r.value("error").toString());
        } else if (w <= 0 || h <= 0 || !raw.open(QIODevice::ReadOnly) || raw.size() < qint64(w) * h) {
            fail(key, "could not be typeset");
        } else {
            const QByteArray bytes = raw.readAll();
            const QImage mask(reinterpret_cast<const uchar *>(bytes.constData()), w, h, w, QImage::Format_Alpha8);
            QImage picture(w, h, QImage::Format_ARGB32);
            picture.fill(Qt::black);
            picture.setAlphaChannel(mask);
            store(key, picture, r.value("depth").toDouble() / RasterEm);
        }
    }
}

QImage MathRenderer::tinted(const Formula &f, const QColor &color, int width, int height)
{
    // scaled() averages properly when shrinking; drawing the big mask small would thin the strokes
    const QImage mask = f.mask.scaled(qMax(1, width), qMax(1, height), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage img(mask.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(color);
    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.drawImage(0, 0, mask);
    p.end();
    return img;
}

QByteArray MathRenderer::png(const Formula &f)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    tinted(f, Qt::black, f.mask.width(), f.mask.height()).save(&buffer, "PNG");
    return data;
}

}  // namespace jot
