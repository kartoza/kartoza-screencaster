#include <QApplication>
#include "gui/mainwindow.h"
#include "config/config.h"

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Kartoza Screencaster");
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName("Kartoza");
    app.setOrganizationDomain("kartoza.com");
    app.setQuitOnLastWindowClosed(false);

    Config::instance().load();

    MainWindow window(APP_VERSION);
    window.show();

    return app.exec();
}
