#include "webcam/webcam.h"
#include "platform/platform.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

static QList<WebcamInfo> detectLinux() {
  QList<WebcamInfo> devices;
  for (int i = 0; i < 10; i++) {
    QString dev = QString("video%1").arg(i);
    if (!QFileInfo::exists("/dev/" + dev)) continue;

    QFile indexFile(QString("/sys/class/video4linux/%1/index").arg(dev));
    if (indexFile.open(QIODevice::ReadOnly)) {
      if (indexFile.readAll().trimmed() != "0") continue;
    }

    QString name = dev;
    QFile nameFile(QString("/sys/class/video4linux/%1/name").arg(dev));
    if (nameFile.open(QIODevice::ReadOnly)) {
      QString n = nameFile.readAll().trimmed();
      if (!n.isEmpty()) name = n;
    }
    devices.append({dev, name});
  }
  return devices;
}

static QList<WebcamInfo> detectMacOS() {
  QList<WebcamInfo> devices;
  QProcess proc;
  proc.start("ffmpeg", {"-f", "avfoundation", "-list_devices", "true", "-i", ""});
  proc.waitForFinished(5000);
  QString output = proc.readAllStandardError();

  bool inVideo = false;
  for (const auto &line : output.split('\n')) {
    if (line.contains("AVFoundation video devices")) { inVideo = true; continue; }
    if (line.contains("AVFoundation audio devices")) break;
    if (!inVideo) continue;

    int bracket = line.indexOf('[', line.indexOf(']') + 1);
    if (bracket < 0) continue;
    int close = line.indexOf(']', bracket);
    if (close < 0) continue;
    QString idx = line.mid(bracket + 1, close - bracket - 1);
    QString name = line.mid(close + 2).trimmed();
    if (!name.isEmpty() && !idx.isEmpty()) {
      devices.append({idx, name});
    }
  }
  return devices;
}

static QList<WebcamInfo> detectWindows() {
  QList<WebcamInfo> devices;
  QProcess proc;
  proc.start("ffmpeg", {"-f", "dshow", "-list_devices", "true", "-i", "dummy"});
  proc.waitForFinished(5000);
  QString output = proc.readAllStandardError();

  for (const auto &line : output.split('\n')) {
    if (line.contains("DirectShow audio")) break;
    if (!line.contains("dshow")) continue;

    int q1 = line.indexOf('"');
    int q2 = line.indexOf('"', q1 + 1);
    if (q1 >= 0 && q2 > q1) {
      QString name = line.mid(q1 + 1, q2 - q1 - 1);
      if (!name.isEmpty() && !name.contains("Alternative name")) {
        devices.append({name, name});
      }
    }
  }
  return devices;
}

QList<WebcamInfo> Webcam::detectAll() {
  switch (Platform::os()) {
  case Platform::OS::Linux: return detectLinux();
  case Platform::OS::macOS: return detectMacOS();
  case Platform::OS::Windows: return detectWindows();
  default: return {};
  }
}
