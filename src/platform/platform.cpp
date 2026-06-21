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

Compositor compositor() {
#if defined(Q_OS_LINUX)
  auto env = QProcessEnvironment::systemEnvironment();
  // XDG_CURRENT_DESKTOP is colon-separated on some distros (e.g. "ubuntu:GNOME")
  QString desktop = env.value("XDG_CURRENT_DESKTOP").toLower();
  // XDG_SESSION_DESKTOP is single-token, often more reliable
  QString session = env.value("XDG_SESSION_DESKTOP").toLower();
  QString combined = desktop + ":" + session;

  if (combined.contains("gnome") || combined.contains("unity")) {
    return Compositor::Mutter;
  }
  if (combined.contains("kde") || combined.contains("plasma")) {
    return Compositor::KWin;
  }
  if (combined.contains("cosmic")) {
    return Compositor::Cosmic;
  }
  if (combined.contains("hyprland") || combined.contains("sway") ||
      combined.contains("wayfire") || combined.contains("river") ||
      combined.contains("niri") || combined.contains("labwc")) {
    return Compositor::Wlroots;
  }
  return Compositor::Unknown;
#else
  return Compositor::Unknown;
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
