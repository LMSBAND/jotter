#include "theme.h"

#include <QBuffer>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QImageReader>
#include <QPalette>
#include <QScreen>
#include <cmath>

namespace jot {

QIcon icon(const QString &name)
{
    if (name.isEmpty())
        return QIcon();
    if (QIcon::hasThemeIcon(name))
        return QIcon::fromTheme(name);
    QFile file(":/icons/" + name + ".svg");
    if (!file.open(QIODevice::ReadOnly))
        return QIcon();
    // Breeze draws its symbols in one colour: make that the window's text colour.
    QByteArray svg = file.readAll();
    svg.replace("#232629", QGuiApplication::palette().color(QPalette::WindowText).name().toLatin1());
    QIcon out;
    for (const int size : {22, 44, 88}) {
        QBuffer buffer(&svg);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "svg");
        reader.setScaledSize(QSize(size, size));
        const QImage image = reader.read();
        if (!image.isNull())
            out.addPixmap(QPixmap::fromImage(image));
    }
    return out;
}

static QString pick(const QStringList &candidates, const QString &fallback)
{
    const QStringList have = QFontDatabase().families();
    for (const QString &name : candidates)
        if (have.contains(name))
            return name;
    return fallback;
}

static qreal pxPerPt()
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    return (screen ? screen->logicalDotsPerInchY() : 96.0) / 72.0;
}

// ---- faces --------------------------------------------------------------------

const QVector<Face> &faces()
{
    auto files = [](const QString &stem, const QStringList &styles, const QString &ext = QStringLiteral(".ttf")) {
        QStringList out;
        for (const QString &style : styles)
            out << stem + style + ext;
        return out;
    };
    const QStringList four = {"Regular", "Italic", "Bold", "BoldItalic"};
    // (the sizes: x-heights brought to about half an em, which is where the system fonts have theirs)
    static const QVector<Face> all = {
        {"Default", QString(), {}, 1.0, QString()},
        {"Roboto", "Roboto", files("roboto/Roboto-", four), 1.0, "sans-serif"},
        {"Atkinson Hyperlegible", "Atkinson Hyperlegible Next", files("atkinson/AtkinsonHyperlegibleNext-", four), 1.03, "sans-serif"},
        {"EB Garamond", "EB Garamond", files("ebgaramond/EBGaramond-", four), 1.16, "serif"},
        {"Source Serif", "Source Serif 4", files("sourceserif/SourceSerif4-", {"Regular", "It", "Bold", "BoldIt"}), 1.06, "serif"},
        // (Computer Modern is what formulas are set in: at this size the text is exactly as big as they are)
        {"Computer Modern", "CMU Serif", files("cmu/cmun", {"rm", "ti", "bx", "bi"}, ".otf"), 1.15, "serif"},
        {"Courier Prime", "Courier Prime", files("courierprime/CourierPrime-", four), 1.04, "monospace"},
        {"Comic Relief", "Comic Relief", files("comicrelief/ComicRelief-", {"Regular", "Bold"}), 0.97, "cursive"},
    };
    return all;
}

const Face &faceNamed(const QString &name)
{
    const QString wanted = name.trimmed();
    for (const Face &face : faces())
        if (face.builtIn() && face.name.compare(wanted, Qt::CaseInsensitive) == 0)
            return face;
    return faces().first();
}

// The family Qt has a built-in face under. Its files are handed to Qt the first
// time it is asked for, so starting up costs nothing for faces not in use.
static QString familyOf(const Face &face)
{
    static QHash<QString, QString> known;
    auto it = known.constFind(face.name);
    if (it == known.constEnd()) {
        QString family;
        for (const QString &file : face.files) {
            const int id = QFontDatabase::addApplicationFont(":/fonts/" + file);
            if (family.isEmpty() && id >= 0)
                family = QFontDatabase::applicationFontFamilies(id).value(0);
        }
        it = known.insert(face.name, family);
    }
    return *it;
}

QFont fontOf(const Face &face, qreal pointSize)
{
    const QString family = face.builtIn() ? familyOf(face) : QString();
    QFont font = family.isEmpty() ? QGuiApplication::font() : QFont(family);
    font.setPointSizeF(pointSize * (family.isEmpty() ? 1.0 : face.scale));
    return font;
}

void Theme::setFace(const Face &face)
{
    const QString family = face.builtIn() ? familyOf(face) : QString();
    body = family.isEmpty() ? ui : family;
    bodyScale = family.isEmpty() ? 1.0 : face.scale;
    bodyHint = QFont::AnyStyle;
    leading = 1.0;
    if (family.isEmpty())
        return;
    // (for the letters a face lacks: borrowed from a font of the same kind)
    bodyHint = face.generic == "serif" ? QFont::Serif : face.generic == "monospace" ? QFont::Monospace : QFont::SansSerif;
    // Fonts differ a lot in how tall they say a line is. Every built-in face is
    // given the line of one that says 1.2 em, so that changing face changes the
    // letters and not how much room the text takes. (Measured at zoom 1, in
    // whole pixels as the layout does: spacing is kept in the document, and
    // must not follow zoom.)
    QFont probe(family);
    probe.setPointSizeF(size * bodyScale);
    const QFontMetricsF fm(probe);
    const qreal natural = std::ceil(fm.ascent() + fm.descent() + fm.leading());
    if (natural > 0)
        leading = std::round(1.2 * size * pxPerPt()) / natural;
}

bool Theme::systemIsDark()
{
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

Theme::Theme(bool isDark) : dark(isDark)
{
    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    ui = body = pick({"Inter", "Noto Sans", "Ubuntu"}, QGuiApplication::font().family());
#ifdef Q_OS_WIN
    // (what Windows itself answers is Courier New)
    mono = pick({"Cascadia Mono", "Consolas", "Lucida Console", "Liberation Mono", "DejaVu Sans Mono"}, fixed.family());
#else
    mono = QFontInfo(fixed).fixedPitch()
        ? fixed.family()
        : pick({"Hack", "JetBrains Mono", "DejaVu Sans Mono", "Ubuntu Mono", "Liberation Mono"}, "monospace");
#endif

    const QColor highlight = QGuiApplication::palette().color(QPalette::Highlight);
    if (dark) {
        canvas = "#141414";
        paper = "#1e1e1e";
        text = "#dcdcdc";
        muted = "#8f959e";
        codeBg = "#272727";
        codeBorder = "#383838";
        inlineBg = "#2e2e2e";
        error = "#f28b82";
        link = "#8ab4f8";
        accent = highlight.lightness() > 110 ? highlight : QColor("#8ab4f8");
    } else {
        canvas = "#e6e7ea";
        paper = "#ffffff";
        text = "#1f2328";
        muted = "#6b7280";
        codeBg = "#f5f6f8";
        codeBorder = "#dcdfe4";
        inlineBg = "#eef0f2";
        error = "#c5221f";
        link = "#1a5fb4";
        accent = highlight.lightness() < 150 ? highlight : QColor("#1a73e8");
    }
    rule = codeBorder;
    spell = "#e5534b";
}

qreal Theme::em() const
{
    return pt() * pxPerPt();
}

qreal Theme::layoutEm() const
{
    return zoom > 0 ? em() / zoom : em();
}

QFont Theme::bodyFont(qreal scale) const
{
    QFont f(body);
    if (bodyHint != QFont::AnyStyle)
        f.setStyleHint(bodyHint);
    f.setPointSizeF(bodyPt() * scale);
    return f;
}

QFont Theme::uiFont(qreal scale) const
{
    QFont f(ui);
    f.setPointSizeF(pt() * scale);
    return f;
}

QFont Theme::monoFont(qreal scale) const
{
    QFont f(mono);
    f.setStyleHint(QFont::Monospace);
    f.setPointSizeF(monoPt() * scale);
    return f;
}

qreal Theme::headingScale(int level)
{
    static const qreal scales[] = {2.0, 1.5, 1.25, 1.1, 1.0, 1.0};
    return scales[qBound(1, level, 6) - 1];
}

}  // namespace jot
