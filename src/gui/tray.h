/**
 * @file tray.h
 * @brief System tray icon and context menu for Kartoza Screencaster.
 */

#pragma once
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>
#include "gui/recordpage.h"

class MainWindow;

/**
 * @class Tray
 * @brief System tray integration providing quick recording controls.
 *
 * Shows a tray icon with a context menu that mirrors the main recording
 * controls. The menu is rebuilt from scratch on every state change because
 * Wayland compositors (COSMIC, etc.) cache menu structure and ignore
 * setVisible/setEnabled changes on existing QActions.
 */
class Tray : public QObject {
    Q_OBJECT
public:
    Tray(MainWindow *mainWindow, RecordPage *recordPage);

private:
    enum State { Idle, Countdown, Recording, Paused, RoomNoise, Processing };

    void setState(State s);
    void startCountdown();
    void onCountdownTick();

    /** @brief Rebuild the entire context menu for the current state. */
    void rebuildMenu();
    /** @brief Convenience wrapper that calls rebuildMenu. */
    void refreshPresetMenu();

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_menu = nullptr;
    MainWindow *m_mainWindow;
    RecordPage *m_recordPage;
    State m_state = Idle;

    QTimer *m_countdownTimer = nullptr;
    int m_countdownVal = 0;
};
