#include "app/Application.h"
#include "app/SingleInstance.h"
#include "ui/MainWindow.h"
#include "ui/ThemeAdapter.h"
#include <QTimer>

int main(int argc, char* argv[]) {
    Application app(argc, argv);
    solero::ThemeAdapter::apply(app);

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
    w.show();
    if (!nxmArg.isEmpty())
        QTimer::singleShot(0, &w, [&w, nxmArg]{ w.handleNxmUrl(nxmArg); });
    return app.exec();
}
