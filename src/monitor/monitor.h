#pragma once

#include <QString>
#include <QList>

struct MonitorInfo {
    QString name;        // e.g. "DP-9", "eDP-1"
    QString description; // e.g. "HP Inc. HP 32f"
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    bool focused = false;
};

class Monitor {
public:
    static QList<MonitorInfo> listMonitors();
    static QString stripAnsi(const QString &s);

private:
    static QList<MonitorInfo> listMonitorsCosmic();
    static QList<MonitorInfo> listMonitorsHyprland();
    static QList<MonitorInfo> listMonitorsSway();
};
