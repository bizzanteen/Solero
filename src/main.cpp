#include "app/Application.h"
#include "ui/MainWindow.h"
#include "ui/ThemeAdapter.h"

int main(int argc, char* argv[]) {
    Application app(argc, argv);
    solero::ThemeAdapter::apply(app);
    MainWindow w;
    w.show();
    return app.exec();
}
