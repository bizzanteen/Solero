#include "app/Application.h"
#include "app/SingleInstance.h"
#include "core/AppConfig.h"
#include "core/Log.h"
#include "ui/MainWindow.h"
#include "ui/ThemeAdapter.h"
#include <QTimer>
#include <QCoreApplication>

int main(int argc, char* argv[]) {
    // Install the file sink + crash handler first, so even the app ctor's messages
    // (and an early crash) are captured to ~/.local/share/solero/logs/solero.log.
    solero::installLogging();
    // QtWebEngine widgets require shared OpenGL contexts set before QApplication.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    Application app(argc, argv);
    solero::ThemeAdapter::apply(app);
    qCInfo(lcApp) << "Solero" << QCoreApplication::applicationVersion()
                  << "starting; data dir" << solero::AppConfig::dataRoot();

    // An nxm:// URL may be passed by the browser (the .desktop Exec gets %u).
    QString nxmArg;
    for (int i = 1; i < argc; ++i) {
        QString a = QString::fromLocal8Bit(argv[i]);
        if (a.startsWith("nxm://", Qt::CaseInsensitive)) { nxmArg = a; break; }
    }

    const QString kKey = "solero-singleton";
    if (solero::SingleInstance::isAnotherRunning(kKey)) {
        if (!nxmArg.isEmpty()) solero::SingleInstance::sendToRunning(kKey, nxmArg);
        return 0; // a Solero is already running; it will handle the URL
    }

    auto* single = new solero::SingleInstance(&app);
    single->listen(kKey);

    MainWindow w;
    QObject::connect(single, &solero::SingleInstance::messageReceived,
                     &w, &MainWindow::handleNxmUrl);
    w.showMaximized();
    if (!nxmArg.isEmpty())
        QTimer::singleShot(0, &w, [&w, nxmArg]{ w.handleNxmUrl(nxmArg); });
    return app.exec();
}
