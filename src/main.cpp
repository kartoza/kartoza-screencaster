#include <QApplication>
#include <QLoggingCategory>
#include <QCommandLineParser>
#include <QTextStream>
#include "gui/mainwindow.h"
#include "config/config.h"

#ifdef HAS_DBUS
#include <QDBusInterface>
#include <QDBusReply>
#include "dbus/dbusservice.h"

static const char *DBUS_SERVICE = "org.kartoza.Screencaster";
static const char *DBUS_PATH = "/Screencaster";
static const char *DBUS_IFACE = "org.kartoza.Screencaster";

/**
 * @brief Send a D-Bus command to a running instance and exit.
 * @return true if the command was handled (caller should exit), false otherwise.
 */
static bool handleRemoteCommand(const QString &command) {
    QDBusInterface iface(DBUS_SERVICE, DBUS_PATH, DBUS_IFACE, QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        QTextStream(stderr) << "No running Kartoza Screencaster instance found.\n";
        return true;
    }

    if (command == "start") {
        iface.call("StartRecording");
    } else if (command == "stop") {
        iface.call("StopRecording");
    } else if (command == "pause") {
        iface.call("PauseRecording");
    } else if (command == "toggle") {
        iface.call("ToggleRecording");
    } else if (command == "status") {
        QDBusReply<QString> reply = iface.call("Status");
        QTextStream(stdout) << (reply.isValid() ? reply.value() : "error") << "\n";
    } else {
        QTextStream(stderr) << "Unknown command: " << command << "\n";
    }
    return true;
}
#endif

int main(int argc, char *argv[]) {
    QLoggingCategory::setFilterRules("qt.text.emojisegmenter=false");
    QApplication app(argc, argv);
    app.setApplicationName("Kartoza Screencaster");
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName("Kartoza");
    app.setOrganizationDomain("kartoza.com");
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription("Screen recording tool with WYSIWYG canvas editor");
    parser.addHelpOption();
    parser.addVersionOption();

#ifdef HAS_DBUS
    QCommandLineOption startOpt("start", "Start recording (sends to running instance)");
    QCommandLineOption stopOpt("stop", "Stop recording (sends to running instance)");
    QCommandLineOption pauseOpt("pause", "Pause/resume recording (sends to running instance)");
    QCommandLineOption toggleOpt("toggle", "Toggle recording state (sends to running instance)");
    QCommandLineOption statusOpt("status", "Print recording status from running instance");
    parser.addOption(startOpt);
    parser.addOption(stopOpt);
    parser.addOption(pauseOpt);
    parser.addOption(toggleOpt);
    parser.addOption(statusOpt);
#endif

    parser.process(app);

#ifdef HAS_DBUS
    // Handle remote commands — send D-Bus message to running instance and exit
    if (parser.isSet(startOpt)) { handleRemoteCommand("start"); return 0; }
    if (parser.isSet(stopOpt)) { handleRemoteCommand("stop"); return 0; }
    if (parser.isSet(pauseOpt)) { handleRemoteCommand("pause"); return 0; }
    if (parser.isSet(toggleOpt)) { handleRemoteCommand("toggle"); return 0; }
    if (parser.isSet(statusOpt)) { handleRemoteCommand("status"); return 0; }
#endif

    Config::instance().load();

    MainWindow window(APP_VERSION);

#ifdef HAS_DBUS
    // Register D-Bus service for remote control
    new DBusService(window.recordPage(), &window);
#endif

    return app.exec();
}
