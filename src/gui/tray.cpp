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

    rebuildMenu();

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

    // Track recording state — rebuild menu since Cosmic doesn't update visibility
    connect(m_recordPage, &RecordPage::recordingStarted, this, [this]() {
        qDebug() << "TRAY: recording started, rebuilding menu";
        m_recording = true;
        rebuildMenu();
        QIcon recIcon("resources/icon_recording.png");
        if (!recIcon.isNull()) m_trayIcon->setIcon(recIcon);
        m_trayIcon->setToolTip("Kartoza Screencaster - Recording");
    });
    connect(m_recordPage, &RecordPage::recordingStopped, this, [this]() {
        qDebug() << "TRAY: recording stopped, rebuilding menu";
        m_recording = false;
        rebuildMenu();
        QIcon icon("resources/icon_ready.png");
        if (!icon.isNull()) m_trayIcon->setIcon(icon);
        m_trayIcon->setToolTip("Kartoza Screencaster - Idle");
    });

    m_trayIcon->show();
}

void Tray::rebuildMenu() {
    // Delete old menu and create fresh one — Cosmic doesn't handle visibility changes
    if (m_menu) {
        m_trayIcon->setContextMenu(nullptr);
        delete m_menu;
    }

    m_menu = new QMenu;

    if (m_recording) {
        auto *stopAction = m_menu->addAction("Stop Recording");
        connect(stopAction, &QAction::triggered, this, [this]() {
            m_recordPage->onStopClicked();
        });
    } else {
        auto *startAction = m_menu->addAction("Start Recording");
        connect(startAction, &QAction::triggered, this, [this]() {
            m_mainWindow->show();
            m_mainWindow->raise();
            m_mainWindow->navigateTo(MainWindow::PageRecord);
        });
    }

    m_menu->addSeparator();
    auto *openAction = m_menu->addAction("Open Window");
    connect(openAction, &QAction::triggered, this, [this]() {
        m_mainWindow->show();
        m_mainWindow->raise();
    });
    m_menu->addSeparator();
    auto *quitAction = m_menu->addAction("Quit");
    connect(quitAction, &QAction::triggered, this, []() {
        QApplication::quit();
    });

    m_trayIcon->setContextMenu(m_menu);
}
