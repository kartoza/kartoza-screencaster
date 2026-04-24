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

    // Create the menu ONCE and keep it — Cosmic DBus tray doesn't like menu replacement
    m_menu = new QMenu;

    // Create all actions upfront, toggle visibility
    m_startAction = m_menu->addAction("Start Recording");
    m_stopAction = m_menu->addAction("Stop Recording");
    m_menu->addSeparator();
    auto *openAction = m_menu->addAction("Open Window");
    m_menu->addSeparator();
    auto *quitAction = m_menu->addAction("Quit");

    // Initial state: show start, hide stop
    m_stopAction->setEnabled(false);
    m_stopAction->setVisible(false);

    connect(m_startAction, &QAction::triggered, this, [this]() {
        qDebug() << "TRAY: Start Recording clicked";
        m_mainWindow->show();
        m_mainWindow->raise();
        m_mainWindow->navigateTo(MainWindow::PageRecord);
    });
    connect(m_stopAction, &QAction::triggered, this, [this]() {
        qDebug() << "TRAY: Stop Recording clicked";
        m_recordPage->onStopClicked();
    });
    connect(openAction, &QAction::triggered, this, [this]() {
        qDebug() << "TRAY: Open Window clicked";
        m_mainWindow->show();
        m_mainWindow->raise();
    });
    connect(quitAction, &QAction::triggered, this, []() {
        QApplication::quit();
    });

    m_trayIcon->setContextMenu(m_menu);

    // Tray icon clicks
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        qDebug() << "TRAY: activated, reason:" << reason << "recording:" << m_recording;
        if (reason == QSystemTrayIcon::Trigger) {
            if (!m_recording) {
                m_mainWindow->setVisible(!m_mainWindow->isVisible());
                if (m_mainWindow->isVisible()) m_mainWindow->raise();
            }
        } else if (reason == QSystemTrayIcon::DoubleClick && m_recording) {
            m_recordPage->onStopClicked();
        }
    });

    // Track recording state
    connect(m_recordPage, &RecordPage::recordingStarted, this, [this]() {
        qDebug() << "TRAY: recording started";
        m_recording = true;
        m_startAction->setText("Recording...");
        m_startAction->setEnabled(false);
        m_stopAction->setVisible(true);
        m_stopAction->setEnabled(true);
        QIcon recIcon("resources/icon_recording.png");
        if (!recIcon.isNull()) m_trayIcon->setIcon(recIcon);
        m_trayIcon->setToolTip("Kartoza Screencaster - Recording");
    });
    connect(m_recordPage, &RecordPage::recordingStopped, this, [this]() {
        qDebug() << "TRAY: recording stopped";
        m_recording = false;
        m_startAction->setText("Start Recording");
        m_startAction->setEnabled(true);
        m_stopAction->setVisible(false);
        m_stopAction->setEnabled(false);
        QIcon icon("resources/icon_ready.png");
        if (!icon.isNull()) m_trayIcon->setIcon(icon);
        m_trayIcon->setToolTip("Kartoza Screencaster - Idle");
    });

    m_trayIcon->show();
}
