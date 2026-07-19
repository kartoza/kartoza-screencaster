#include "webcam/webcam.h"
#include "platform/platform.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <cerrno>
#endif

#ifdef Q_OS_LINUX
// Probe a single /dev/videoN node via the V4L2 API. Returns true and fills
// `name`/`bus` when the node is a real video-capture device.
//
// Why probe instead of trusting the sysfs `index` attribute (the old
// approach): modern UVC cameras expose several /dev/videoN nodes per physical
// camera (a capture node plus metadata nodes), and `index` is a fragile way to
// pick the capture node — valid cameras were being dropped, so they "appeared
// briefly then disappeared". VIDIOC_QUERYCAP asks the driver directly whether
// the node can capture video, which is authoritative. `bus_info` identifies the
// physical device so multiple nodes of one camera collapse to a single entry.
static bool probeCaptureDevice(const QString &dev, QString &name, QString &bus) {
  int fd = ::open(QString("/dev/" + dev).toUtf8().constData(), O_RDWR | O_NONBLOCK);
  if (fd < 0) return false;

  v4l2_capability cap{};
  bool ok = false;
  if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
    // device_caps describes this specific node; capabilities describes the
    // whole physical device. Prefer device_caps when the driver provides it.
    quint32 caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
                       ? cap.device_caps
                       : cap.capabilities;
    if (caps & V4L2_CAP_VIDEO_CAPTURE) {
      ok = true;
      name = QString::fromUtf8(reinterpret_cast<const char *>(cap.card)).trimmed();
      bus = QString::fromUtf8(reinterpret_cast<const char *>(cap.bus_info)).trimmed();
    }
  }
  ::close(fd);
  return ok;
}
#endif

static QList<WebcamInfo> detectLinux() {
  QList<WebcamInfo> devices;
#ifdef Q_OS_LINUX
  QSet<QString> seenBus; // one entry per physical camera (bus_info)
  // Scan a generous range; nodes are sparse and non-contiguous across cameras.
  for (int i = 0; i < 64; i++) {
    QString dev = QString("video%1").arg(i);
    if (!QFileInfo::exists("/dev/" + dev)) continue;

    QString name, bus;
    if (!probeCaptureDevice(dev, name, bus)) continue; // skip metadata/unopenable nodes
    if (!bus.isEmpty()) {
      if (seenBus.contains(bus)) continue; // already have this physical camera
      seenBus.insert(bus);
    }
    if (name.isEmpty()) name = dev;
    devices.append({dev, name});
  }
#endif
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
