#pragma once
#include <QSystemTrayIcon>
#include <QMenu>
#include "gui/recordpage.h"

class MainWindow;

class Tray : public QObject {
    Q_OBJECT
public:
    Tray(MainWindow *mainWindow, RecordPage *recordPage);

private:
    QSystemTrayIcon *m_trayIcon;
    QMenu *m_menu = nullptr;
    QAction *m_startAction;
    QAction *m_pauseAction;
    QAction *m_stopAction;
    MainWindow *m_mainWindow;
    RecordPage *m_recordPage;
    bool m_recording = false;
};
