// Every save of a document is kept. A hidden folder beside the document,
// `.notes.history` for `notes.md`, holds the document and its pictures as they
// were each time it was saved, one after the other in a single line: nothing
// in it is ever changed or taken out.
//
// The folder is a git repository (a bare one, of loose objects, with one
// commit on `main` for each save), so anything that reads git can read it:
//
//     git --git-dir=.notes.history log
//
// Jotter writes and reads it by itself, and needs no git on the machine.
#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace jot {

class History
{
public:
    explicit History(const QString &docPath);
    QString dir() const { return m_dir; }

    // A save is known by its id (git's, of the commit). "" is none.
    QString head() const;                           // the newest
    QString parent(const QString &save) const;      // the one before it
    QString child(const QString &save) const;       // the one after it
    int count() const;
    int number(const QString &save) const;          // 1 for the first save, count() for the newest; 0 if it is not one
    QDateTime when(const QString &save) const;
    QByteArray text(const QString &save) const;     // the document as it was saved then
    bool holds(const QString &save) const;          // ...is really there to be read

    // The save that holds exactly this text: `prefer` if it does, else the newest that does.
    QString find(const QByteArray &text, const QString &prefer = QString()) const;

    // One more save, after the newest: the document's text and the picture files
    // it refers to. Returns it - or the newest there was, if nothing has changed
    // since. "" if the folder cannot be written.
    QString commit(const QByteArray &text, const QStringList &pictures);

    // Put back, in `folder`, the pictures a save had that are no longer there. Returns their names.
    QStringList restorePictures(const QString &save, const QString &folder) const;

private:
    struct Entry { QByteArray mode, name, id; };
    struct Commit { QString tree, parent; QDateTime when; };
    bool prepare() const;
    QString put(const char *type, const QByteArray &body) const;
    QByteArray get(const QString &id) const;
    Commit read(const QString &save) const;
    QVector<Entry> entries(const QString &tree) const;
    QString docBlob(const QString &save) const;
    static QByteArray treeBody(QVector<Entry> entries);

    QString m_doc, m_dir;
    mutable QHash<QString, Commit> m_commits;
};

}  // namespace jot
