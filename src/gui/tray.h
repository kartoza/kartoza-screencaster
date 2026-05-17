/**
 * @file tray.h
 * @brief System tray icon and context menu for Kartoza Screencaster.
 */
#pragma once
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>
#include <QSvgRenderer>
#include "gui/recordpage.h"

class MainWindow;

/**
 * @class Tray
 * @brief System tray integration providing quick recording controls.
 *
 * Uses a single persistent QMenu with all possible actions. Actions
 * that aren't relevant for the current state are disabled and their
 * text is updated. This approach works on Wayland compositors (COSMIC)
 * that cache the D-Bus menu structure.
 */
class Tray : public QObject {
    Q_OBJECT
public:
    Tray(MainWindow *mainWindow, RecordPage *recordPage);

private:
    enum State { Idle, Countdown, Recording, Paused, RoomNoise, Processing };

    void setState(State s);
    void updateMenuState();
    void startCountdown();
    void onCountdownTick();
    void refreshPresetMenu();
    /** @brief Build icon from SVG with a number overlay in the center. */
    QIcon buildCountdownIcon(int number);
    /** @brief Build icon from SVG with a pause symbol in the center. */
    QIcon buildPausedIcon();
    /** @brief Build icon from SVG with a colored center dot, optionally rotated. */
    QIcon buildDotIcon(const QColor &dotColor, int rotationDeg = 0);

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_menu = nullptr;
    QAction *m_recordAction = nullptr; // Start / Pause / Resume
    QAction *m_stopAction = nullptr;
    QMenu *m_presetMenu = nullptr;
    QAction *m_openAction = nullptr;

    MainWindow *m_mainWindow;
    RecordPage *m_recordPage;
    State m_state = Idle;

    QTimer *m_countdownTimer = nullptr;
    QTimer *m_rotationTimer = nullptr;
    int m_countdownVal = 0;
    int m_rotationAngle = 0;
    QSvgRenderer *m_svgRenderer = nullptr;
};
