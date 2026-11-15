// Turns document meaning into looks.
//
// A QSyntaxHighlighter lays formats *over* the text without touching the
// document or its undo history, which is exactly what a theme should do:
// headings get their size here, code gets its font and colours, links their
// colour, misspelt words their squiggle. Switching theme or zoom is a
// re-highlight, not an edit.
#pragma once

#include "theme.h"

#include <QHash>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

struct Hunhandle;
class QTextCodec;

namespace KSyntaxHighlighting {
class Definition;
class Repository;
}

namespace jot {

KSyntaxHighlighting::Repository &syntaxRepository();
KSyntaxHighlighting::Definition syntaxFor(const QString &lang);     // by fence name: "python", "sh", "cpp"...

class Renderer : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    Renderer(QTextDocument *doc, const Theme &theme);
    ~Renderer() override;

    void setTheme(const Theme &theme);
    void rehighlightRun(const QTextBlock &block);

    void setSpellCheck(bool on);
    bool spellCheck() const { return m_dict != nullptr; }
    bool misspelled(const QString &word);
    bool wordAt(const QTextBlock &block, int column, int *start, int *end, QString *word);  // a misspelt word there?
    QStringList suggestions(const QString &word);
    void learn(const QString &word);

protected:
    void highlightBlock(const QString &text) override;

private:
    void code(const QTextBlock &block, const QString &text);
    void spell(const QString &text, const QVector<QPair<int, int>> &skip);
    void addWord(const QString &word);

    Theme m_theme;
    QTextCharFormat m_code, m_output, m_error, m_heading[7], m_quote, m_th, m_link, m_icode;
    QHash<int, QTextCharFormat> m_tokens;
    struct Lines;
    Lines *m_lines;
    Hunhandle *m_dict = nullptr;
    QTextCodec *m_codec = nullptr;      // the dictionary's own encoding
    bool m_english = false;
    QHash<QString, bool> m_known;
};

}  // namespace jot
