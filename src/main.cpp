#include <QApplication>
#include "gui/mainwindow.h"
#include "config/config.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Kartoza Screencaster");
    app.setApplicationVersion("0.9.0");
    app.setOrganizationName("Kartoza");
    app.setOrganizationDomain("kartoza.com");

    Config::instance().load();

    MainWindow window("0.9.0");
    window.show();

    return app.exec();
}
