// The document as HTML: for the clipboard, for "Export > HTML", and as the
// input to PDF export. Always the light look, whatever the editor shows.
#pragma once

#include <QString>

class QTextDocument;

namespace jot {

class AssetStore;

// Just the content. embed = pictures inline as data: URIs (self-contained).
QString bodyHtml(const QTextDocument *doc, const AssetStore &store, bool embed);

// A complete, self-contained page.
QString pageHtml(const QTextDocument *doc, const AssetStore &store, const QString &title);

}  // namespace jot
