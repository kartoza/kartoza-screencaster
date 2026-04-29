#include "monitor/monitor.h"
#include <QProcess>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

QList<MonitorInfo> Monitor::listMonitors() {
    // Try cosmic-randr first
    auto monitors = listMonitorsCosmic();
    if (!monitors.isEmpty()) return monitors;

    // Try hyprctl
    monitors = listMonitorsHyprland();
    if (!monitors.isEmpty()) return monitors;

    // Try swaymsg
    monitors = listMonitorsSway();
    if (!monitors.isEmpty()) return monitors;

    // Fallback
    MonitorInfo fallback;
    fallback.name = "";
    fallback.width = 1920;
    fallback.height = 1080;
    fallback.focused = true;
    return {fallback};
}

QList<MonitorInfo> Monitor::listMonitorsCosmic() {
    QProcess proc;
    proc.start("cosmic-randr", {"list"});
    if (!proc.waitForFinished(3000)) return {};

    QString output = stripAnsi(proc.readAllStandardOutput());
    QList<MonitorInfo> monitors;
    MonitorInfo *current = nullptr;
    bool first = true;

    for (const QString &rawLine : output.split('\n')) {
        QString line = rawLine.trimmed();

        // New output starts with non-indented name
        if (!rawLine.startsWith(' ') && !line.startsWith("Make:") && !line.isEmpty()) {
            QStringList parts = line.split(' ');
            if (!parts.isEmpty() && (parts[0].contains('-') || parts[0].startsWith("HDMI"))) {
                monitors.append(MonitorInfo());
                current = &monitors.last();
                current->name = parts[0];
                current->focused = first;
                first = false;
            }
        }
        if (!current) continue;

        if (line.startsWith("Make:")) {
            current->description = line.mid(5).trimmed();
        } else if (line.startsWith("Model:")) {
            QString model = line.mid(6).trimmed();
            if (!current->description.isEmpty())
                current->description += " " + model;
            else
                current->description = model;
        } else if (line.contains("(current)")) {
            QRegularExpression re("(\\d+)x(\\d+)");
            auto match = re.match(line);
            if (match.hasMatch()) {
                current->width = match.captured(1).toInt();
                current->height = match.captured(2).toInt();
            }
        } else if (line.startsWith("Position:")) {
            QString pos = line.mid(9).trimmed();
            QStringList xy = pos.split(',');
            if (xy.size() >= 2) {
                current->x = xy[0].trimmed().toInt();
                current->y = xy[1].trimmed().toInt();
            }
        }
    }

    return monitors;
}

QList<MonitorInfo> Monitor::listMonitorsHyprland() {
    QProcess proc;
    proc.start("hyprctl", {"monitors", "-j"});
    if (!proc.waitForFinished(3000)) return {};

    QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    if (!doc.isArray()) return {};

    QList<MonitorInfo> monitors;
    for (const auto &val : doc.array()) {
        QJsonObject obj = val.toObject();
        MonitorInfo mon;
        mon.name = obj["name"].toString();
        mon.description = obj["description"].toString();
        mon.width = obj["width"].toInt();
        mon.height = obj["height"].toInt();
        mon.x = obj["x"].toInt();
        mon.y = obj["y"].toInt();
        mon.focused = obj["focused"].toBool();
        if (mon.description.isEmpty()) {
            mon.description = obj["make"].toString() + " " + obj["model"].toString();
        }
        monitors.append(mon);
    }
    return monitors;
}

QList<MonitorInfo> Monitor::listMonitorsSway() {
    QProcess proc;
    proc.start("swaymsg", {"-t", "get_outputs", "-r"});
    if (!proc.waitForFinished(3000)) return {};

    QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    if (!doc.isArray()) return {};

    QList<MonitorInfo> monitors;
    for (const auto &val : doc.array()) {
        QJsonObject obj = val.toObject();
        MonitorInfo mon;
        mon.name = obj["name"].toString();
        mon.description = obj["make"].toString() + " " + obj["model"].toString();
        mon.focused = obj["focused"].toBool();

        auto rect = obj["rect"].toObject();
        mon.x = rect["x"].toInt();
        mon.y = rect["y"].toInt();

        auto mode = obj["current_mode"].toObject();
        mon.width = mode["width"].toInt();
        mon.height = mode["height"].toInt();

        monitors.append(mon);
    }
    return monitors;
}

QString Monitor::stripAnsi(const QString &s) {
    return QString(s).remove(QRegularExpression("\x1b\\[[0-9;]*m"));
}
