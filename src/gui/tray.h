#pragma once
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>
#include "gui/recordpage.h"

class MainWindow;

class Tray : public QObject {
    Q_OBJECT
public:
    Tray(MainWindow *mainWindow, RecordPage *recordPage);

private:
    enum State { Idle, Countdown, Recording, Paused, Processing };

    void setState(State s);
    void startCountdown();
    void onCountdownTick();

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_menu = nullptr;
    QAction *m_startAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_stopAction = nullptr;
    MainWindow *m_mainWindow;
    RecordPage *m_recordPage;
    State m_state = Idle;

    QTimer *m_countdownTimer = nullptr;
    int m_countdownVal = 0;
};
