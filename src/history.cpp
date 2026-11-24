#include "history.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace jot {

namespace {

const char *const kFile = "100644", *const kFolder = "40000";      // what git calls a file and a folder in a tree

// An object as git names and stores it: what it is, how long, a zero, the thing itself.
QByteArray framed(const char *type, const QByteArray &body)
{
    return QByteArray(type) + ' ' + QByteArray::number(body.size()) + '\0' + body;
}

QString idOf(const QByteArray &object)
{
    return QString::fromLatin1(QCryptographicHash::hash(object, QCryptographicHash::Sha1).toHex());
}

bool isId(const QString &id)
{
    if (id.size() != 40)
        return false;
    for (const QChar c : id)
        if (!c.isDigit() && (c < 'a' || c > 'f'))
            return false;
    return true;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
}

}  // namespace

History::History(const QString &docPath) : m_doc(QFileInfo(docPath).absoluteFilePath())
{
    const QFileInfo fi(m_doc);
    m_dir = fi.absolutePath() + "/." + fi.completeBaseName() + ".history";
}

// The folder, with what makes it a git repository. Made at the first save.
bool History::prepare() const
{
    if (QFile::exists(m_dir + "/HEAD"))
        return true;
    if (!QDir().mkpath(m_dir + "/objects") || !QDir().mkpath(m_dir + "/refs/heads"))
        return false;
#ifdef Q_OS_WIN
    SetFileAttributesW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(m_dir).utf16()), FILE_ATTRIBUTE_HIDDEN);
#endif
    // (If the document lives in somebody's git repository, this folder is none of that repository's business.)
    return writeFile(m_dir + "/.gitignore", "*\n")
        && writeFile(m_dir + "/config", "[core]\n\trepositoryformatversion = 0\n\tfilemode = false\n\tbare = true\n")
        && writeFile(m_dir + "/HEAD", "ref: refs/heads/main\n");
}

QString History::put(const char *type, const QByteArray &body) const
{
    const QByteArray object = framed(type, body);
    const QString id = idOf(object);
    const QString folder = m_dir + "/objects/" + id.left(2), path = folder + "/" + id.mid(2);
    if (QFile::exists(path))
        return id;          // the same thing was saved before: it is kept once
    // (qCompress puts four bytes of its own in front of what zlib, and git, call the data)
    if (!QDir().mkpath(folder) || !writeFile(path, qCompress(object).mid(4)))
        return QString();
    return id;
}

QByteArray History::get(const QString &id) const
{
    QFile f(m_dir + "/objects/" + id.left(2) + "/" + id.mid(2));
    if (!isId(id) || !f.open(QIODevice::ReadOnly))
        return QByteArray();
    const QByteArray packed = f.readAll();
    QByteArray size(4, '\0');       // qUncompress wants to be told how much to expect; a guess will do
    qToBigEndian<quint32>(quint32(qMax(1024, packed.size() * 4)), size.data());
    const QByteArray object = qUncompress(size + packed);
    const int zero = object.indexOf('\0');
    return zero < 0 ? QByteArray() : object.mid(zero + 1);
}

History::Commit History::read(const QString &save) const
{
    const auto known = m_commits.constFind(save);
    if (known != m_commits.constEnd())
        return *known;
    Commit c;
    for (const QByteArray &line : get(save).split('\n')) {
        if (line.isEmpty())
            break;          // what follows is the message
        if (line.startsWith("tree "))
            c.tree = QString::fromLatin1(line.mid(5));
        else if (line.startsWith("parent ") && c.parent.isEmpty())
            c.parent = QString::fromLatin1(line.mid(7));
        else if (line.startsWith("committer "))     // ... <mail> 1732421640 -0700
            c.when = QDateTime::fromSecsSinceEpoch(line.left(line.lastIndexOf(' ')).mid(line.lastIndexOf('>') + 2).toLongLong());
    }
    if (!c.tree.isEmpty())
        m_commits.insert(save, c);
    return c;
}

QVector<History::Entry> History::entries(const QString &tree) const
{
    QVector<Entry> out;
    const QByteArray body = get(tree);
    for (int at = 0; at < body.size();) {
        const int space = body.indexOf(' ', at), zero = body.indexOf('\0', at);
        if (space < 0 || zero < space || zero + 20 > body.size())
            break;
        out.append({body.mid(at, space - at), body.mid(space + 1, zero - space - 1), body.mid(zero + 1, 20).toHex()});
        at = zero + 21;
    }
    return out;
}

QByteArray History::treeBody(QVector<Entry> entries)
{
    // git's order: by name, a folder's as if it ended in a slash
    auto key = [](const Entry &e) { return e.mode == kFolder ? e.name + '/' : e.name; };
    std::sort(entries.begin(), entries.end(), [&](const Entry &a, const Entry &b) { return key(a) < key(b); });
    QByteArray body;
    for (const Entry &e : entries)
        body += e.mode + ' ' + e.name + '\0' + QByteArray::fromHex(e.id);
    return body;
}

QString History::docBlob(const QString &save) const
{
    for (const Entry &e : entries(read(save).tree))
        if (e.mode != kFolder)
            return QString::fromLatin1(e.id);       // the one file at the top is the document, whatever its name was then
    return QString();
}

QString History::head() const
{
    QFile f(m_dir + "/refs/heads/main");
    const QString id = f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll().trimmed()) : QString();
    return isId(id) ? id : QString();
}

QString History::parent(const QString &save) const
{
    return read(save).parent;
}

QString History::child(const QString &save) const
{
    for (QString c = head(); !c.isEmpty(); c = parent(c))
        if (parent(c) == save)
            return c;
    return QString();
}

int History::count() const
{
    int n = 0;
    for (QString c = head(); !c.isEmpty(); c = parent(c))
        ++n;
    return n;
}

int History::number(const QString &save) const
{
    int after = 0;
    for (QString c = head(); !c.isEmpty(); c = parent(c), ++after)
        if (c == save)
            return count() - after;
    return 0;
}

QDateTime History::when(const QString &save) const
{
    return read(save).when;
}

QByteArray History::text(const QString &save) const
{
    return get(docBlob(save));
}

bool History::holds(const QString &save) const
{
    const QString blob = docBlob(save);
    return !blob.isEmpty() && QFile::exists(m_dir + "/objects/" + blob.left(2) + "/" + blob.mid(2));
}

QString History::find(const QByteArray &text, const QString &prefer) const
{
    const QString blob = idOf(framed("blob", text));
    if (!prefer.isEmpty() && docBlob(prefer) == blob)
        return prefer;
    for (QString c = head(); !c.isEmpty(); c = parent(c))
        if (docBlob(c) == blob)
            return c;
    return QString();
}

QString History::commit(const QByteArray &text, const QStringList &pictures)
{
    if (!prepare())
        return QString();
    const QFileInfo doc(m_doc);
    QVector<Entry> top, folder;
    top.append({kFile, doc.fileName().toUtf8(), put("blob", text).toLatin1()});
    for (const QString &path : pictures) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            folder.append({kFile, QFileInfo(path).fileName().toUtf8(), put("blob", f.readAll()).toLatin1()});
    }
    if (!folder.isEmpty())
        top.append({kFolder, (doc.completeBaseName() + ".assets").toUtf8(), put("tree", treeBody(folder)).toLatin1()});
    for (const Entry &e : top + folder)
        if (e.id.isEmpty())
            return QString();
    const QString tree = put("tree", treeBody(top)), before = head();
    if (tree.isEmpty())
        return QString();
    if (!before.isEmpty() && read(before).tree == tree)
        return before;      // saved again with nothing changed

    const QDateTime now = QDateTime::currentDateTime();
    const int minutes = now.offsetFromUtc() / 60;
    const QByteArray zone = QByteArray::number(qAbs(minutes) / 60 * 100 + qAbs(minutes) % 60).rightJustified(4, '0');
    const QByteArray who = "Jotter <jotter@localhost> " + QByteArray::number(now.toSecsSinceEpoch()) + (minutes < 0 ? " -" : " +") + zone;
    QByteArray body = "tree " + tree.toLatin1() + "\n";
    if (!before.isEmpty())
        body += "parent " + before.toLatin1() + "\n";
    body += "author " + who + "\ncommitter " + who + "\n\nSaved\n";
    const QString id = put("commit", body);
    if (id.isEmpty() || !writeFile(m_dir + "/refs/heads/main", id.toLatin1() + "\n"))
        return QString();
    return id;
}

QStringList History::restorePictures(const QString &save, const QString &folder) const
{
    QStringList restored;
    for (const Entry &top : entries(read(save).tree)) {
        if (top.mode != kFolder)
            continue;
        for (const Entry &e : entries(QString::fromLatin1(top.id))) {
            const QString name = QString::fromUtf8(e.name), path = folder + "/" + name;
            if (e.mode == kFolder || name.contains('/') || name.contains('\\') || QFile::exists(path))
                continue;
            const QByteArray data = get(QString::fromLatin1(e.id));
            if (!data.isEmpty() && QDir().mkpath(folder) && writeFile(path, data))
                restored << name;
        }
    }
    return restored;
}

}  // namespace jot
