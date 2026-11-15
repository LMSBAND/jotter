// Where a document's images live.
//
// A saved document `notes.md` keeps its pictures (pasted screenshots, plots that
// cells produced) in `notes.assets/` beside it. Until the first save they sit in
// a temp folder. Inside the document every such picture is referred to as
// "asset:<name>", so nothing in the document changes when it is saved or moved.
#pragma once

#include <QSet>
#include <QString>
#include <QTemporaryDir>

namespace jot {

bool hasScheme(const QString &src);     // "https:", "file:", "data:"... as opposed to a plain path

class AssetStore
{
public:
    AssetStore();

    QString docPath;                    // absolute path of the .md, empty while untitled
    QSet<QString> savedRefs;            // asset names the on-disk document refers to

    QString docDir() const;
    QString dir() const;                // where "asset:" names live right now
    QString writeDir() const;           // same, created
    QString path(const QString &name) const;
    QString resolve(const QString &src) const;      // local file for a source, or empty if remote

    QString toMarkdown(const QString &src, bool absolute = false) const;
    QString fromMarkdown(const QString &src, bool importExternal = false);

    QString addBytes(const QByteArray &data, const QString &prefix = "pasted", const QString &ext = ".png");
    QString addFile(const QString &path);
    void noteCreated(const QString &name);

    void saveTo(const QString &newPath, const QSet<QString> &referenced);
    void tidy();                        // on close: drop generated files the saved document does not use

private:
    QTemporaryDir m_tmp;
    QSet<QString> m_created;            // files this session wrote
};

}  // namespace jot
