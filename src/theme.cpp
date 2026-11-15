#include "theme.h"

#include <QBuffer>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QPalette>
#include <QScreen>

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

bool Theme::systemIsDark()
{
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

Theme::Theme(bool isDark) : dark(isDark)
{
    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    body = pick({"Inter", "Noto Sans", "Ubuntu"}, QGuiApplication::font().family());
    mono = QFontInfo(fixed).fixedPitch()
        ? fixed.family()
        : pick({"Hack", "JetBrains Mono", "DejaVu Sans Mono", "Ubuntu Mono", "Liberation Mono"}, "monospace");

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
    const QScreen *screen = QGuiApplication::primaryScreen();
    const qreal dpi = screen ? screen->logicalDotsPerInchY() : 96.0;
    return pt() * dpi / 72.0;
}

qreal Theme::layoutEm() const
{
    return zoom > 0 ? em() / zoom : em();
}

QFont Theme::bodyFont(qreal scale) const
{
    QFont f(body);
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
