// Jotter: a word processor with runnable code cells.
//   jotter [file.md]
#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <cstdio>
#include <cstring>

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {        // answered without needing a display
        if (!std::strcmp(argv[i], "--version")) {
            std::printf("jotter %s\n", JOTTER_VERSION);
            return 0;
        }
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::printf("usage: jotter [file.md]\n");
            return 0;
        }
    }
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("jotter");
    QCoreApplication::setApplicationName("jotter");
    QCoreApplication::setApplicationVersion(JOTTER_VERSION);
    QGuiApplication::setApplicationDisplayName("Jotter");
    QGuiApplication::setDesktopFileName("jotter");
    QApplication::setWindowIcon(QIcon::fromTheme("jotter", QIcon(":/jotter.svg")));

    const QStringList args = app.arguments();
    jot::MainWindow window;
    QString file;
    for (int i = 1; i < args.size(); ++i)
        if (!args[i].startsWith('-'))
            file = args[i];
    window.show();
    if (!file.isEmpty())
        window.open(file);
    else
        window.offerRecovery();
    return app.exec();
}
