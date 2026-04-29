/**
 * @file platform.cpp
 * @brief Runtime platform and display server detection.
 */
#include "platform/platform.h"
#include <QProcessEnvironment>

namespace Platform {

DisplayServer displayServer() {
#if defined(Q_OS_LINUX)
  auto env = QProcessEnvironment::systemEnvironment();
  QString wayland = env.value("WAYLAND_DISPLAY");
  QString xdgSession = env.value("XDG_SESSION_TYPE");

  if (!wayland.isEmpty() || xdgSession == "wayland") {
    return DisplayServer::Wayland;
  }
  QString display = env.value("DISPLAY");
  if (!display.isEmpty() || xdgSession == "x11") {
    return DisplayServer::X11;
  }
  return DisplayServer::Unknown;
#else
  return DisplayServer::Unknown;
#endif
}

QString platformString() {
  QString s;
  switch (os()) {
  case OS::Linux: s = "Linux"; break;
  case OS::macOS: s = "macOS"; break;
  case OS::Windows: s = "Windows"; break;
  default: s = "Unknown"; break;
  }
#if defined(Q_OS_LINUX)
  switch (displayServer()) {
  case DisplayServer::Wayland: s += "/Wayland"; break;
  case DisplayServer::X11: s += "/X11"; break;
  default: break;
  }
#endif
  return s;
}

} // namespace Platform
