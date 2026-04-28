/**
 * @file monitor.h
 * @brief Detection and enumeration of connected display monitors.
 */

#pragma once

#include <QString>
#include <QList>

/**
 * @brief Describes a single connected display monitor.
 */
struct MonitorInfo {
    /** Compositor output name, e.g. "DP-9" or "eDP-1". */
    QString name;
    /** Human-readable description, e.g. "HP Inc. HP 32f". */
    QString description;
    /** Horizontal resolution in pixels. */
    int width = 0;
    /** Vertical resolution in pixels. */
    int height = 0;
    /** Horizontal offset in the virtual desktop layout. */
    int x = 0;
    /** Vertical offset in the virtual desktop layout. */
    int y = 0;
    /** True if this monitor currently has keyboard focus. */
    bool focused = false;
};

/**
 * @brief Utility class for detecting monitors across Wayland compositors.
 *
 * Supports Hyprland, Sway, and COSMIC desktop environments.
 */
class Monitor {
public:
    /**
     * @brief Detects all connected monitors using the active compositor.
     * @return List of MonitorInfo structs, one per display.
     */
    static QList<MonitorInfo> listMonitors();

    /**
     * @brief Strips ANSI escape sequences from a string.
     * @param s Input string potentially containing ANSI codes.
     * @return Cleaned string with escape sequences removed.
     */
    static QString stripAnsi(const QString &s);

private:
    /** @brief Lists monitors via the COSMIC compositor interface. */
    static QList<MonitorInfo> listMonitorsCosmic();
    /** @brief Lists monitors via hyprctl (Hyprland). */
    static QList<MonitorInfo> listMonitorsHyprland();
    /** @brief Lists monitors via swaymsg (Sway / wlroots). */
    static QList<MonitorInfo> listMonitorsSway();
};
