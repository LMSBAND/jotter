// jotter-pdf: print an HTML file to PDF with Qt WebEngine.
//   jotter-pdf input.html output.pdf [letter|a4]
// A separate program so the editor itself never loads a browser engine.
#include <QApplication>
#include <QPageLayout>
#include <QPageSize>
#include <QTimer>
#include <QUrl>
#include <QWebEnginePage>
#include <cstdio>

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");        // nothing to show
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS"))
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu");
    // The page is a file Jotter itself just wrote, and the browser sandbox cannot
    // start from inside an AppImage on current Ubuntu.
    qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3) {
        std::fprintf(stderr, "usage: jotter-pdf input.html output.pdf [letter|a4]\n");
        return 2;
    }
    const QPageSize size(args.value(3).toLower() == "a4" ? QPageSize::A4 : QPageSize::Letter);
    const QPageLayout layout(size, QPageLayout::Portrait, QMarginsF(22, 22, 22, 24), QPageLayout::Millimeter);
    const QString output = args[2];

    QWebEnginePage page;
    int result = 1;
    QObject::connect(&page, &QWebEnginePage::loadFinished, [&](bool ok) {
        if (!ok) {
            std::fprintf(stderr, "jotter-pdf: could not load %s\n", qPrintable(args[1]));
            app.exit(1);
            return;
        }
        page.printToPdf(output, layout);
    });
    QObject::connect(&page, &QWebEnginePage::pdfPrintingFinished, [&](const QString &, bool ok) {
        result = ok ? 0 : 1;
        app.exit(result);
    });
    QTimer::singleShot(120000, &app, [&] { app.exit(1); });
    page.load(QUrl::fromLocalFile(args[1]));
    return app.exec();
}
