// The document as HTML: for the clipboard, for "Export > HTML", and as the
// input to PDF export. Always the light look, whatever the editor shows, and
// in the document's own text face, which a page carries with it.
#pragma once

#include <QString>
#include <functional>

class QObject;
class QTextDocument;

namespace jot {

class AssetStore;

// Just the content. embed = pictures inline as data: URIs (self-contained).
QString bodyHtml(const QTextDocument *doc, const AssetStore &store, bool embed);

// A complete, self-contained page.
QString pageHtml(const QTextDocument *doc, const AssetStore &store, const QString &title);

// The page printed to a PDF file, on "a4" or "letter" paper. The printing is
// done by another program: Jotter's own helper (jotter-pdf, built where Qt
// WebEngine is), otherwise a Chromium-family browser run without a window --
// Windows always has one, Edge. done(ok) is called when it has finished.
// (browserOnly passes the helper over even where it exists: for the tests.)
bool canExportPdf(bool browserOnly = false);
void exportPdf(const QTextDocument *doc, const AssetStore &store, const QString &title, const QString &path,
               const QString &paper, QObject *owner, std::function<void(bool ok)> done, bool browserOnly = false);

}  // namespace jot
