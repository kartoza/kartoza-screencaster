#include "gui/tray.h"
#include "gui/mainwindow.h"
#include <QApplication>
#include <QIcon>
#include <QDebug>

Tray::Tray(MainWindow *mainWindow, RecordPage *recordPage)
    : QObject(mainWindow), m_mainWindow(mainWindow), m_recordPage(recordPage) {

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setToolTip("Kartoza Screencaster");

    QIcon icon("resources/icon_ready.png");
    if (icon.isNull()) icon = QIcon::fromTheme("camera-video");
    m_trayIcon->setIcon(icon);

    // All items always visible — Cosmic ignores dynamic menu changes.
    // Logic handled in action callbacks based on m_recording state.
    m_menu = new QMenu;

    m_menu->addAction("Start Recording", this, [this]() {
        if (m_recording) return; // already recording
        m_mainWindow->show();
        m_mainWindow->raise();
        m_mainWindow->navigateTo(MainWindow::PageRecord);
    });
    m_menu->addAction("Stop Recording", this, [this]() {
        if (!m_recording) return; // not recording
        m_recordPage->onStopClicked();
    });
    m_menu->addSeparator();
    m_menu->addAction("Open Window", this, [this]() {
        m_mainWindow->show();
        m_mainWindow->raise();
    });
    m_menu->addSeparator();
    m_menu->addAction("Quit", this, []() {
        QApplication::quit();
    });

    m_trayIcon->setContextMenu(m_menu);

    // Tray icon clicks
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            if (m_recording) {
                // During recording, single click stops
                m_recordPage->onStopClicked();
            } else {
                m_mainWindow->setVisible(!m_mainWindow->isVisible());
                if (m_mainWindow->isVisible()) m_mainWindow->raise();
            }
        }
    });

    // Track state for icon changes (these DO work on Cosmic)
    connect(m_recordPage, &RecordPage::recordingStarted, this, [this]() {
        m_recording = true;
        QIcon recIcon("resources/icon_recording.png");
        if (!recIcon.isNull()) m_trayIcon->setIcon(recIcon);
        m_trayIcon->setToolTip("Kartoza Screencaster - Recording (click to stop)");
    });
    connect(m_recordPage, &RecordPage::recordingStopped, this, [this]() {
        m_recording = false;
        QIcon icon("resources/icon_ready.png");
        if (!icon.isNull()) m_trayIcon->setIcon(icon);
        m_trayIcon->setToolTip("Kartoza Screencaster");
    });

    m_trayIcon->show();
}
