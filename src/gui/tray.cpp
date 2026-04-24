#include "gui/tray.h"
#include "gui/mainwindow.h"
#include <QApplication>
#include <QIcon>

Tray::Tray(MainWindow *mainWindow, RecordPage *recordPage)
    : QObject(mainWindow), m_mainWindow(mainWindow), m_recordPage(recordPage) {

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setToolTip("Kartoza Screencaster");

    // Try to load icon
    QIcon icon("resources/icon_ready.png");
    if (icon.isNull()) icon = QIcon::fromTheme("camera-video");
    m_trayIcon->setIcon(icon);

    m_menu = new QMenu;
    m_startAction = m_menu->addAction("Start Recording");
    m_pauseAction = m_menu->addAction("Pause");
    m_stopAction = m_menu->addAction("Stop Recording");
    m_menu->addSeparator();
    auto *openAction = m_menu->addAction("Open Window");
    m_menu->addSeparator();
    auto *quitAction = m_menu->addAction("Quit");

    m_pauseAction->setVisible(false);
    m_stopAction->setVisible(false);

    connect(m_startAction, &QAction::triggered, this, [this]() {
        m_mainWindow->show();
        m_mainWindow->raise();
        m_mainWindow->navigateTo(MainWindow::PageRecord);
    });
    connect(openAction, &QAction::triggered, this, [this]() {
        m_mainWindow->show();
        m_mainWindow->raise();
    });
    connect(quitAction, &QAction::triggered, this, []() {
        QApplication::quit();
    });
    connect(m_stopAction, &QAction::triggered, this, [this]() {
        m_recordPage->onStopClicked();
    });

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            if (!m_recording) {
                // Toggle window when not recording
                m_mainWindow->setVisible(!m_mainWindow->isVisible());
                if (m_mainWindow->isVisible()) m_mainWindow->raise();
            }
            // During recording, single click does nothing (use context menu)
        } else if (reason == QSystemTrayIcon::DoubleClick && m_recording) {
            m_recordPage->onStopClicked();
        }
    });

    // Track recording state via native Qt signals — guaranteed delivery
    connect(m_recordPage, &RecordPage::recordingStarted, this, [this]() {
        qDebug() << "TRAY: recording started, updating menu";
        m_recording = true;
        m_startAction->setVisible(false);
        m_pauseAction->setVisible(true);
        m_stopAction->setVisible(true);
        QIcon recIcon("resources/icon_recording.png");
        if (!recIcon.isNull()) m_trayIcon->setIcon(recIcon);
        m_trayIcon->setToolTip("Kartoza Screencaster - Recording");
    });
    connect(m_recordPage, &RecordPage::recordingStopped, this, [this]() {
        qDebug() << "TRAY: recording stopped, updating menu";
        m_recording = false;
        m_startAction->setVisible(true);
        m_pauseAction->setVisible(false);
        m_stopAction->setVisible(false);
        QIcon icon("resources/icon_ready.png");
        if (!icon.isNull()) m_trayIcon->setIcon(icon);
        m_trayIcon->setToolTip("Kartoza Screencaster - Idle");
    });

    m_trayIcon->setContextMenu(m_menu);
    m_trayIcon->show();
}
