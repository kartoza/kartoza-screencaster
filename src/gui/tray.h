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
 * controls (start, pause, stop). The icon tooltip and menu items update
 * to reflect the current recording state.
 */
class Tray : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Construct the system tray controller.
     * @param mainWindow Pointer to the main window (for show/hide toggling).
     * @param recordPage Pointer to the record page (for recording control).
     */
    Tray(MainWindow *mainWindow, RecordPage *recordPage);

private:
    /** @brief Possible states of the recording workflow. */
    enum State { Idle, Countdown, Recording, Paused, RoomNoise, Processing };

    /**
     * @brief Transition to a new state, updating the tray icon and menu.
     * @param s The new state.
     */
    void setState(State s);
    /** @brief Begin the pre-recording countdown from the tray. */
    void startCountdown();
    /** @brief Handle a countdown timer tick, updating the tray tooltip. */
    void onCountdownTick();

    /** @brief The system tray icon instance. */
    QSystemTrayIcon *m_trayIcon;
    /** @brief Context menu shown on tray icon interaction. */
    QMenu *m_menu = nullptr;
    /** @brief Action to start a recording. */
    QAction *m_startAction = nullptr;
    /** @brief Action to pause/resume a recording. */
    QAction *m_pauseAction = nullptr;
    /** @brief Action to stop a recording. */
    QAction *m_stopAction = nullptr;
    /** @brief Reference to the main application window. */
    MainWindow *m_mainWindow;
    /** @brief Reference to the recording page for control delegation. */
    RecordPage *m_recordPage;
    /** @brief Current recording workflow state. */
    State m_state = Idle;

    /** @brief Timer driving the pre-recording countdown. */
    QTimer *m_countdownTimer = nullptr;
    /** @brief Current countdown value in seconds. */
    int m_countdownVal = 0;
};
