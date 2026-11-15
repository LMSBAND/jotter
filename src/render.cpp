#include "render.h"

#include "model.h"

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextCodec>
#include <hunspell.h>
#include <functional>

namespace jot {

namespace KSH = KSyntaxHighlighting;

KSH::Repository &syntaxRepository()
{
    static KSH::Repository repo;
    return repo;
}

KSH::Definition syntaxFor(const QString &lang)
{
    // Fence names are whatever people type: a definition name ("python"), a
    // file extension ("py", "rs") or one of a few habits ("shell", "js").
    static QHash<QString, KSH::Definition> cache;
    static const QHash<QString, QString> alias = {
        {"sh", "Bash"}, {"shell", "Bash"}, {"zsh", "Zsh"}, {"js", "JavaScript"}, {"node", "JavaScript"},
        {"ts", "TypeScript"}, {"py", "Python"}, {"python3", "Python"}, {"ipython", "Python"}, {"c++", "C++"},
        {"cpp", "C++"}, {"h", "C"}, {"lean4", "Lean"}, {"graphviz", "dot"}, {"yml", "YAML"}, {"md", "Markdown"},
        {"tex", "LaTeX"}, {"rb", "Ruby"}, {"rs", "Rust"}, {"jsfx", "JavaScript"}, {"eel", "JavaScript"}};
    const QString key = lang.toLower();
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return *it;
    KSH::Definition def = syntaxRepository().definitionForName(alias.value(key, key));
    if (!def.isValid()) {
        for (const KSH::Definition &d : syntaxRepository().definitions()) {
            if (d.name().compare(key, Qt::CaseInsensitive) == 0) {
                def = d;
                break;
            }
        }
    }
    if (!def.isValid() && !key.isEmpty())
        def = syntaxRepository().definitionForFileName("x." + key);
    cache.insert(key, def);
    return def;
}

namespace {

class LineHighlighter : public KSH::AbstractHighlighter
{
public:
    std::function<void(int, int, const KSH::Format &)> sink;
    KSH::State run(const QString &text, const KSH::State &state) { return highlightLine(QStringView(text), state); }

protected:
    void applyFormat(int offset, int length, const KSH::Format &format) override
    {
        if (sink)
            sink(offset, length, format);
    }
};

// What the syntax engine knew at the end of a line; the next line starts from it.
struct LineState : QTextBlockUserData {
    KSH::State state;
};

const QRegularExpression &wordPattern()
{
    static const QRegularExpression rx(QStringLiteral(R"([^\W\d_]+(?:['\x{2019}][^\W\d_]+)*)"),
                                       QRegularExpression::UseUnicodePropertiesOption);
    return rx;
}

bool glue(QChar c)      // a "word" touching these is an identifier, path or handle
{
    return c == '_' || c == '@' || c == '/' || c == '\\' || c.isDigit();
}

}  // namespace

struct Renderer::Lines {
    LineHighlighter highlighter;
};

Renderer::Renderer(QTextDocument *doc, const Theme &theme) : QSyntaxHighlighter(doc), m_theme(theme), m_lines(new Lines)
{
    setTheme(theme);
}

Renderer::~Renderer()
{
    setSpellCheck(false);
    delete m_lines;
}

void Renderer::setTheme(const Theme &theme)
{
    m_theme = theme;
    const Theme &th = m_theme;
    QTextCharFormat mono;
    mono.setFontFamily(th.mono);
    mono.setFontFixedPitch(true);
    mono.setFontPointSize(th.monoPt());
    mono.setFontWeight(QFont::Normal);
    mono.setFontItalic(false);
    mono.setFontUnderline(false);
    mono.setFontStrikeOut(false);
    mono.setForeground(th.text);
    m_code = mono;
    m_output = mono;
    m_output.setForeground(th.muted);
    m_error = mono;
    m_error.setForeground(th.error);
    m_tokens.clear();
    m_lines->highlighter.setTheme(
        syntaxRepository().defaultTheme(th.dark ? KSH::Repository::DarkTheme : KSH::Repository::LightTheme));

    for (int level = 1; level <= 6; ++level) {
        QTextCharFormat f;
        f.setFontPointSize(th.pt() * Theme::headingScale(level));
        f.setFontWeight(level <= 2 ? QFont::Bold : QFont::DemiBold);
        m_heading[level] = f;
    }
    m_quote = QTextCharFormat();
    m_quote.setForeground(th.muted);
    m_th = QTextCharFormat();
    m_th.setFontWeight(QFont::DemiBold);
    m_link = QTextCharFormat();
    m_link.setForeground(th.link);
    m_link.setFontUnderline(true);
    m_icode = QTextCharFormat();
    m_icode.setFontFamily(th.mono);
    m_icode.setBackground(th.inlineBg);
    rehighlight();
}

void Renderer::rehighlightRun(const QTextBlock &block)
{
    const int last = runLast(block).blockNumber();
    for (QTextBlock b = runFirst(block); b.isValid() && b.blockNumber() <= last; b = b.next())
        rehighlightBlock(b);
}

// ---- prose --------------------------------------------------------------------

void Renderer::highlightBlock(const QString &text)
{
    const QTextBlock block = currentBlock();
    const Kind kind = kindOf(block);
    if (kind == Code) {
        code(block, text);
        return;
    }
    if (currentBlockUserData())
        setCurrentBlockUserData(nullptr);       // was a code line once
    if (kind == Output || kind == Raw) {
        setFormat(0, text.size(), block.blockFormat().boolProperty(PErr) ? m_error : m_output);
        return;
    }
    if (kind == Fold || kind == Rule || text.isEmpty())
        return;

    const QTextCharFormat *base = nullptr;
    qreal scale = 1.0;
    if (kind == Heading) {
        const int level = qBound(1, block.blockFormat().headingLevel(), 6);
        base = &m_heading[level];
        scale = Theme::headingScale(level);
    } else if (kind == Quote) {
        base = &m_quote;
    } else if (QTextTable *table = tableOf(block)) {
        if (table->cellAt(block.position()).row() == 0)
            base = &m_th;
    }
    QVector<QPair<int, int>> skip;
    const int origin = block.position();
    for (const QTextFragment &frag : fragments(block)) {
        const QTextCharFormat cf = frag.charFormat();
        const int start = frag.position() - origin, length = frag.length();
        const bool isCode = cf.fontFixedPitch(), isLink = cf.isAnchor(), isRaw = cf.boolProperty(PRaw);
        if (isCode || isLink || isRaw || cf.objectType() != QTextFormat::NoObject)
            skip.append({start, start + length});
        if (!base && !isCode && !isLink && !isRaw)
            continue;
        QTextCharFormat f = base ? *base : QTextCharFormat();
        if (isCode) {
            f.merge(m_icode);
            f.setFontPointSize(m_theme.monoPt() * scale);
        }
        if (isLink)
            f.merge(m_link);
        if (isRaw)
            f.setForeground(m_theme.muted);
        setFormat(start, length, f);
    }
    if (m_dict)
        spell(text, skip);
}

// ---- code ---------------------------------------------------------------------

void Renderer::code(const QTextBlock &block, const QString &text)
{
    setFormat(0, text.size(), m_code);
    const KSH::Definition def = syntaxFor(langName(langOf(block)));
    KSH::State state;
    const QTextBlock prev = block.previous();
    if (prev.isValid() && sameRun(prev, block))
        if (auto *d = static_cast<LineState *>(prev.userData()))
            state = d->state;
    if (def.isValid()) {
        LineHighlighter &hl = m_lines->highlighter;
        if (hl.definition() != def) {
            hl.setDefinition(def);
            m_tokens.clear();       // format ids belong to a definition
        }
        const KSH::Theme theme = hl.theme();
        hl.sink = [&](int offset, int length, const KSH::Format &format) {
            if (length <= 0 || format.isDefaultTextStyle(theme))
                return;
            auto it = m_tokens.find(format.id());
            if (it == m_tokens.end()) {
                QTextCharFormat f = m_code;
                if (format.hasTextColor(theme))
                    f.setForeground(format.textColor(theme));
                f.setFontWeight(format.isBold(theme) ? QFont::Bold : QFont::Normal);
                f.setFontItalic(format.isItalic(theme));
                it = m_tokens.insert(format.id(), f);
            }
            setFormat(offset, length, *it);
        };
        state = hl.run(text, state);
        hl.sink = nullptr;
    }
    auto *data = static_cast<LineState *>(currentBlockUserData());
    if (!data) {
        data = new LineState;
        data->state = state;
        setCurrentBlockUserData(data);
        setCurrentBlockState(currentBlockState() + 1);      // new line: make Qt look at the next one too
    } else if (data->state != state) {
        data->state = state;
        setCurrentBlockState(currentBlockState() + 1);      // e.g. a string now runs on: re-highlight below
    }
}

// ---- spelling -------------------------------------------------------------------

namespace {

// A dictionary for the system language: one shipped beside Jotter, or the system's.
bool findDictionary(QString *aff, QString *dic)
{
    const QStringList dirs = {QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../share/hunspell"),
                              "/usr/share/hunspell", "/usr/share/myspell/dicts", "/usr/share/myspell"};
    const QString locale = QLocale::system().name();
    for (const QString &name : {locale, locale.section('_', 0, 0), QStringLiteral("en_US"), QStringLiteral("en_GB")}) {
        for (const QString &dir : dirs) {
            if (QFile::exists(dir + "/" + name + ".aff") && QFile::exists(dir + "/" + name + ".dic")) {
                *aff = dir + "/" + name + ".aff";
                *dic = dir + "/" + name + ".dic";
                return true;
            }
        }
    }
    return false;
}

QString personalWords()     // words the user chose "Add to Dictionary" for
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + "/words.txt";
}

}  // namespace

void Renderer::setSpellCheck(bool on)
{
    if (on == (m_dict != nullptr))
        return;
    if (!on) {
        Hunspell_destroy(m_dict);
        m_dict = nullptr;
    } else {
        QString aff, dic;
        if (findDictionary(&aff, &dic)) {
            m_dict = Hunspell_create(QFile::encodeName(aff).constData(), QFile::encodeName(dic).constData());
            m_codec = QTextCodec::codecForName(Hunspell_get_dic_encoding(m_dict));
            if (!m_codec)
                m_codec = QTextCodec::codecForName("UTF-8");
            m_english = QFileInfo(dic).fileName().startsWith("en");
            QFile words(personalWords());
            if (words.open(QIODevice::ReadOnly))
                for (const QByteArray &line : words.readAll().split('\n'))
                    if (!line.trimmed().isEmpty())
                        addWord(QString::fromUtf8(line.trimmed()));
        }
    }
    m_known.clear();
    rehighlight();
}

bool Renderer::misspelled(const QString &word)
{
    if (!m_dict || word.size() < 2 || word == word.toUpper())
        return false;
    auto it = m_known.constFind(word);
    if (it != m_known.constEnd())
        return *it;
    QString plain = word;
    const bool wrong = Hunspell_spell(m_dict, m_codec->fromUnicode(plain.replace(QChar(0x2019), '\'')).constData()) == 0;
    m_known.insert(word, wrong);
    return wrong;
}

void Renderer::spell(const QString &text, const QVector<QPair<int, int>> &skip)
{
    auto it = wordPattern().globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const int start = m.capturedStart(), end = m.capturedEnd();
        if ((start > 0 && glue(text[start - 1])) || (end < text.size() && glue(text[end])))
            continue;
        bool skipped = false;
        for (const auto &range : skip)
            skipped = skipped || (range.first < end && start < range.second);
        if (skipped || !misspelled(m.captured()))
            continue;
        for (int i = start; i < end; ++i) {
            QTextCharFormat f = format(i);
            f.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            f.setUnderlineColor(m_theme.spell);
            setFormat(i, 1, f);
        }
    }
}

bool Renderer::wordAt(const QTextBlock &block, int column, int *start, int *end, QString *word)
{
    if (!m_dict || kindOf(block) == Code || kindOf(block) == Output || kindOf(block) == Raw)
        return false;
    auto it = wordPattern().globalMatch(block.text());
    while (it.hasNext()) {
        const auto m = it.next();
        if (m.capturedStart() <= column && column <= m.capturedEnd()) {
            if (!misspelled(m.captured()))
                return false;
            *start = m.capturedStart();
            *end = m.capturedEnd();
            *word = m.captured();
            return true;
        }
    }
    return false;
}

QStringList Renderer::suggestions(const QString &word)
{
    QStringList out;
    if (!m_dict)
        return out;
    char **list = nullptr;
    const int n = Hunspell_suggest(m_dict, &list, m_codec->fromUnicode(word).constData());
    for (int i = 0; i < n && i < 6; ++i)
        out << m_codec->toUnicode(list[i]);
    Hunspell_free_list(m_dict, &list, n);
    return out;
}

// A learned word brings its plural and possessive with it ("biquad" also
// allows "biquads" and "biquad's"), by borrowing the endings an ordinary noun
// has in the dictionary. Learned as a plural, the singular is fine too.
void Renderer::addWord(const QString &word)
{
    Hunspell_add(m_dict, m_codec->fromUnicode(word).constData());
    if (!m_english)
        return;
    Hunspell_add_with_affix(m_dict, m_codec->fromUnicode(word).constData(), "cat");
    if (word.size() > 3 && word.endsWith('s') && !word.endsWith("ss"))
        Hunspell_add_with_affix(m_dict, m_codec->fromUnicode(word.left(word.size() - 1)).constData(), "cat");
}

void Renderer::learn(const QString &word)
{
    if (!m_dict)
        return;
    addWord(word);
    QFile words(personalWords());
    if (words.open(QIODevice::Append))
        words.write(word.toUtf8() + "\n");
    m_known.clear();
    rehighlight();
}

}  // namespace jot
