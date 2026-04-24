#include "webcam/webcam.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

QList<WebcamInfo> Webcam::detectAll() {
    QList<WebcamInfo> devices;

    for (int i = 0; i < 10; i++) {
        QString dev = QString("video%1").arg(i);
        QString path = "/dev/" + dev;
        QFileInfo info(path);
        if (!info.exists()) continue;

        // Check it's a capture device (index 0), not metadata (index 1+)
        QString indexPath = QString("/sys/class/video4linux/%1/index").arg(dev);
        QFile indexFile(indexPath);
        if (indexFile.open(QIODevice::ReadOnly)) {
            QString idx = indexFile.readAll().trimmed();
            indexFile.close();
            if (idx != "0") continue;
        }

        // Get human-readable name
        QString name = dev;
        QString namePath = QString("/sys/class/video4linux/%1/name").arg(dev);
        QFile nameFile(namePath);
        if (nameFile.open(QIODevice::ReadOnly)) {
            QString n = nameFile.readAll().trimmed();
            nameFile.close();
            if (!n.isEmpty()) name = n;
        }

        devices.append({dev, name});
    }

    return devices;
}
