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

/** @brief Return a human-readable platform string. */
QString platformString();

/** @brief True if running on Wayland. */
inline bool isWayland() { return displayServer() == DisplayServer::Wayland; }

/** @brief True if running on X11. */
inline bool isX11() { return displayServer() == DisplayServer::X11; }

} // namespace Platform
