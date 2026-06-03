#include "app/Application.h"
#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    Application app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
