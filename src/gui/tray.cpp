#include "gui/tray.h"
#include "gui/mainwindow.h"
#include "config/config.h"
#include <QApplication>
#include <QIcon>
#include <QDebug>

Tray::Tray(MainWindow *mainWindow, RecordPage *recordPage)
    : QObject(mainWindow), m_mainWindow(mainWindow), m_recordPage(recordPage) {

    m_trayIcon = new QSystemTrayIcon(this);
    m_countdownTimer = new QTimer(this);
    connect(m_countdownTimer, &QTimer::timeout, this, &Tray::onCountdownTick);

    // Context menu
    m_menu = new QMenu;

    m_startAction = m_menu->addAction("Start Recording", this, [this]() {
        if (m_state == Idle) m_recordPage->onStartClicked();
    });
    m_pauseAction = m_menu->addAction("Pause", this, [this]() {
        if (m_recordPage->recorder()->isRecording() || m_recordPage->recorder()->isPaused()) {
            m_recordPage->onPauseClicked();
        }
    });
    m_stopAction = m_menu->addAction("Stop Recording", this, [this]() {
        // Always try to stop — don't rely on tray state matching
        if (m_recordPage->recorder()->isRecording() || m_recordPage->recorder()->isPaused()) {
            m_recordPage->onStopClicked();
        }
    });
    m_menu->addSeparator();

    // Preset submenu
    m_presetMenu = m_menu->addMenu("Presets");
    refreshPresetMenu();

    m_menu->addSeparator();
    m_menu->addAction("Open Window", this, [this]() {
        QTimer::singleShot(50, m_mainWindow, [this]() {
            m_mainWindow->showFromTray();
        });
    });
    m_menu->addSeparator();
    m_menu->addAction("Quit", this, [this]() {
        m_trayIcon->hide();
        // Tell closeEvent to allow the actual close
        m_mainWindow->setProperty("quitting", true);
        QApplication::quit();
    });

    m_trayIcon->setContextMenu(m_menu);

    // Single click behavior
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Trigger) return;

        switch (m_state) {
        case Idle:
            // Delegate to record page's own countdown (5 seconds)
            m_recordPage->onStartClicked();
            break;
        case Countdown:
            // Not used — record page handles its own countdown
            break;
        case Recording:
            m_recordPage->onPauseClicked();
            break;
        case Paused:
            m_recordPage->onPauseClicked();
            break;
        case RoomNoise:
            // Blocked during room noise capture
            break;
        case Processing:
            m_mainWindow->showFromTray();
            break;
        }
    });

    // Track recorder state changes
    connect(m_recordPage, &RecordPage::recordingStarted, this, [this]() {
        setState(Recording);
        m_mainWindow->hideToTray();
    });
    connect(m_recordPage, &RecordPage::recordingStopped, this, [this]() {
        setState(Processing);
        m_mainWindow->show();
        m_mainWindow->raise();
    });
    connect(m_recordPage->recorder(), &Recorder::recordingPaused, this, [this]() {
        setState(Paused);
    });
    connect(m_recordPage->recorder(), &Recorder::recordingResumed, this, [this]() {
        setState(Recording);
    });
    connect(m_recordPage->recorder(), &Recorder::roomNoiseStarted, this, [this]() {
        setState(RoomNoise);
    });
    connect(m_recordPage->recorder(), &Recorder::roomNoiseProgress, this, [this](int secs) {
        m_trayIcon->setToolTip(QString("Recording room noise - %1s remaining\nPlease keep quiet!").arg(secs));
    });
    connect(m_recordPage->recorder(), &Recorder::roomNoiseFinished, this, [this]() {
        setState(Processing);
    });
    connect(m_recordPage->recorder(), &Recorder::processingFinished, this, [this](bool) {
        setState(Idle);
    });

    // Sync preset menu when canvas preset changes
    connect(m_recordPage, &RecordPage::presetChanged, this, [this]() {
        refreshPresetMenu();
    });

    setState(Idle);
    m_trayIcon->show();
}

void Tray::setState(State s) {
    m_state = s;

    QString iconPath;
    QString tooltip;

    switch (s) {
    case Idle:
        iconPath = ":/icons/ready.png";
        tooltip = "Kartoza Screencaster - Click to record";
        m_startAction->setVisible(true);
        m_pauseAction->setVisible(false);
        m_stopAction->setVisible(false);
        break;
    case Countdown:
        iconPath = ":/icons/ready.png";
        tooltip = QString("Starting in %1...").arg(m_countdownVal);
        m_startAction->setVisible(false);
        m_pauseAction->setVisible(false);
        m_stopAction->setVisible(true);
        break;
    case Recording:
        iconPath = ":/icons/recording.png";
        tooltip = "Recording - Click to pause";
        m_startAction->setVisible(false);
        m_pauseAction->setVisible(true);
        m_pauseAction->setText("Pause");
        m_stopAction->setVisible(true);
        break;
    case Paused:
        iconPath = ":/icons/ready.png";
        tooltip = "Paused - Click to resume";
        m_startAction->setVisible(false);
        m_pauseAction->setVisible(true);
        m_pauseAction->setText("Resume");
        m_stopAction->setVisible(true);
        break;
    case RoomNoise:
        iconPath = ":/icons/recording.png";
        tooltip = "Recording room noise - Please keep quiet!";
        m_startAction->setVisible(false);
        m_pauseAction->setVisible(false);
        m_stopAction->setVisible(false);
        break;
    case Processing:
        iconPath = ":/icons/ready.png";
        tooltip = "Processing video...";
        m_startAction->setVisible(false);
        m_pauseAction->setVisible(false);
        m_stopAction->setVisible(false);
        break;
    }

    QIcon icon(iconPath);
    if (icon.isNull()) icon = QIcon::fromTheme("camera-video");
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip(tooltip);
}

void Tray::startCountdown() {
    m_countdownVal = 5;
    setState(Countdown);
    m_trayIcon->showMessage("Kartoza Screencaster",
        "Recording starts in 5 seconds...", QSystemTrayIcon::Information, 4000);
    m_countdownTimer->start(1000);
}

void Tray::onCountdownTick() {
    m_countdownVal--;
    if (m_countdownVal <= 0) {
        m_countdownTimer->stop();
        m_recordPage->onStartClicked();
    } else {
        m_trayIcon->setToolTip(QString("Starting in %1...").arg(m_countdownVal));
    }
}

void Tray::refreshPresetMenu() {
    m_presetMenu->clear();
    auto &cfg = Config::instance();

    if (cfg.presets.isEmpty()) {
        m_presetMenu->addAction("(no presets)")->setEnabled(false);
        return;
    }

    for (const auto &name : cfg.presets.keys()) {
        bool isActive = (name == cfg.activePreset);
        QString display = isActive ? QString::fromUtf8("\u2713 ") + name : "   " + name;
        m_presetMenu->addAction(display, this, [this, name]() {
            m_recordPage->loadPreset(name);
        });
    }
}
