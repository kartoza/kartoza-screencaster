#pragma once

#include <QString>
#include <QList>

struct WebcamInfo {
    QString device; // e.g. "video0"
    QString name;   // e.g. "Logitech Webcam C930e"
};

class Webcam {
public:
    static QList<WebcamInfo> detectAll();
};
