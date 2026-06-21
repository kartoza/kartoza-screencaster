/**
 * @file platform.h
 * @brief Runtime platform and display server detection.
 */
#pragma once

#include <QString>

namespace Platform {

/** @brief Detected operating system. */
enum class OS { Linux, macOS, Windows, Unknown };

/** @brief Detected display server (Linux only). */
enum class DisplayServer { Wayland, X11, Unknown };

/** @brief Detected Wayland compositor family (Linux only). */
enum class Compositor {
  Wlroots,  // Hyprland, Sway, Wayfire, River, Niri — supports wlr-screencopy
  Cosmic,   // System76 COSMIC — wlroots-based, treated as wlroots
  Mutter,   // GNOME — requires xdg-desktop-portal
  KWin,     // KDE — requires xdg-desktop-portal
  Unknown
};

/** @brief Return the current operating system. */
inline OS os() {
#if defined(Q_OS_LINUX)
  return OS::Linux;
#elif defined(Q_OS_MACOS)
  return OS::macOS;
#elif defined(Q_OS_WIN)
  return OS::Windows;
#else
  return OS::Unknown;
#endif
}

/** @brief Detect the active display server on Linux. */
DisplayServer displayServer();

/** @brief Detect the active Wayland compositor family on Linux. */
Compositor compositor();

/** @brief Return a human-readable platform string. */
QString platformString();

/** @brief True if running on Wayland. */
inline bool isWayland() { return displayServer() == DisplayServer::Wayland; }

/** @brief True if running on X11. */
inline bool isX11() { return displayServer() == DisplayServer::X11; }

/**
 * @brief True if the compositor implements the wlr-screencopy protocol.
 *
 * grim and wl-screenrec only work on wlroots-family compositors. On
 * Mutter (GNOME) and KWin (KDE), capture must go through xdg-desktop-portal.
 */
inline bool supportsWlrCapture() {
  auto c = compositor();
  return c == Compositor::Wlroots || c == Compositor::Cosmic;
}

} // namespace Platform
