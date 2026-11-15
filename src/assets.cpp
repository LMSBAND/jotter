#include "assets.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

namespace jot {

bool hasScheme(const QString &src)
{
    static const QRegularExpression rx(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
    return rx.match(src).hasMatch();
}

static QString sha1(const QByteArray &data, int length)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().left(length));
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

AssetStore::AssetStore() : m_tmp(QDir::tempPath() + "/jotter-XXXXXX")
{
    m_tmp.setAutoRemove(true);
}

QString AssetStore::docDir() const
{
    return docPath.isEmpty() ? QString() : QFileInfo(docPath).absolutePath();
}

QString AssetStore::dir() const
{
    if (docPath.isEmpty())
        return m_tmp.path();
    const QFileInfo fi(docPath);
    return fi.absolutePath() + "/" + fi.completeBaseName() + ".assets";
}

QString AssetStore::writeDir() const
{
    QDir().mkpath(dir());
    return dir();
}

QString AssetStore::path(const QString &name) const
{
    const QString p = dir() + "/" + name;
    if (!QFile::exists(p)) {
        const QString q = m_tmp.path() + "/" + name;
        if (QFile::exists(q))
            return q;
    }
    return p;
}

QString AssetStore::resolve(const QString &src) const
{
    if (src.isEmpty())
        return QString();
    if (src.startsWith("asset:"))
        return path(src.mid(6));
    if (hasScheme(src))
        return src.startsWith("file:") ? QUrl(src).toLocalFile() : QString();
    if (QDir::isAbsolutePath(src))
        return src;
    const QString base = docPath.isEmpty() ? QDir::currentPath() : docDir();
    return QDir::cleanPath(base + "/" + src);
}

QString AssetStore::toMarkdown(const QString &src, bool absolute) const
{
    if (!src.startsWith("asset:"))
        return src;
    if (absolute || docPath.isEmpty())
        return path(src.mid(6));
    return QFileInfo(dir()).fileName() + "/" + src.mid(6);
}

QString AssetStore::fromMarkdown(const QString &src, bool importExternal)
{
    if (src.isEmpty() || hasScheme(src))
        return src;
    QString p = src;
    if (!QDir::isAbsolutePath(p)) {
        if (docPath.isEmpty())
            return src;
        p = docDir() + "/" + src;
    }
    p = QDir::cleanPath(p);
    const QString parent = QFileInfo(p).absolutePath();
    if (parent == QDir::cleanPath(dir()) || parent == QDir::cleanPath(m_tmp.path()))
        return "asset:" + QFileInfo(p).fileName();
    if (importExternal && QFileInfo(p).isFile())
        return addFile(p);
    return src;
}

QString AssetStore::addBytes(const QByteArray &data, const QString &prefix, const QString &ext)
{
    const QString name = prefix + "-" + sha1(data, 12) + ext;
    const QString dest = writeDir() + "/" + name;
    if (!QFile::exists(dest)) {
        QFile f(dest);
        if (f.open(QIODevice::WriteOnly))
            f.write(data);
        m_created.insert(dest);
    }
    return "asset:" + name;
}

QString AssetStore::addFile(const QString &source)
{
    const QByteArray data = readAll(source);
    const QFileInfo fi(source);
    QString name = fi.fileName();
    QString dest = writeDir() + "/" + name;
    if (QFile::exists(dest) && readAll(dest) != data) {     // same name, different picture
        name = fi.completeBaseName() + "-" + sha1(data, 8) + (fi.suffix().isEmpty() ? "" : "." + fi.suffix());
        dest = dir() + "/" + name;
    }
    if (!QFile::exists(dest)) {
        QFile::copy(source, dest);
        m_created.insert(dest);
    }
    return "asset:" + name;
}

void AssetStore::noteCreated(const QString &name)
{
    m_created.insert(dir() + "/" + name);
}

void AssetStore::saveTo(const QString &newPath, const QSet<QString> &referenced)
{
    // Point the store at newPath, bringing along every asset the document uses.
    const QString oldDir = dir();
    docPath = QFileInfo(newPath).absoluteFilePath();
    const QString newDir = dir();
    for (const QString &name : referenced) {
        const QString dst = newDir + "/" + name;
        if (QFile::exists(dst))
            continue;
        for (const QString &d : {oldDir, m_tmp.path()}) {
            const QString src = d + "/" + name;
            if (QFile::exists(src)) {
                QDir().mkpath(newDir);
                QFile::copy(src, dst);
                m_created.insert(dst);
                break;
            }
        }
    }
    savedRefs = referenced;
}

void AssetStore::tidy()
{
    // Re-running a plot leaves the previous picture behind; remove what this
    // session generated and the saved document no longer uses. Never touches
    // files that were already there.
    if (docPath.isEmpty())
        return;
    for (const QString &p : qAsConst(m_created)) {
        const QFileInfo fi(p);
        if (fi.absolutePath() == dir() && !savedRefs.contains(fi.fileName()))
            QFile::remove(p);
    }
    QDir().rmdir(dir());        // only succeeds when empty
}

}  // namespace jot
