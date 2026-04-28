/**
 * @file webcam.h
 * @brief Detection of Video4Linux webcam devices.
 */

#pragma once

#include <QString>
#include <QList>

/**
 * @brief Describes a single detected webcam device.
 */
struct WebcamInfo {
    /** V4L2 device node name, e.g. "video0". */
    QString device;
    /** Human-readable device name, e.g. "Logitech Webcam C930e". */
    QString name;
};

/**
 * @brief Utility class for enumerating available webcam devices.
 */
class Webcam {
public:
    /**
     * @brief Scans /dev/video* for capture-capable V4L2 devices.
     * @return List of WebcamInfo structs for each detected webcam.
     */
    static QList<WebcamInfo> detectAll();
};
