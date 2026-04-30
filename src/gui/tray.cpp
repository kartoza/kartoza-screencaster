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

    // Single click behavior
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Trigger) return;

        if (m_recordPage->recorder()->isRecording()) {
            m_recordPage->onPauseClicked();
        } else if (m_recordPage->recorder()->isPaused()) {
            m_recordPage->onPauseClicked();
        } else if (m_state == Idle) {
            m_recordPage->onStartClicked();
        } else if (m_state == Processing) {
            m_mainWindow->showFromTray();
        }
    });

    // Track recorder state changes
    connect(m_recordPage, &RecordPage::recordingStarted, this, [this]() {
        setState(Recording);
        m_mainWindow->hideToTray();
    });
    connect(m_recordPage, &RecordPage::recordingStopped, this, [this]() {
        setState(Processing);
        m_mainWindow->showFromTray();
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
        rebuildMenu();
    });

    setState(Idle);
    m_trayIcon->show();
}

void Tray::rebuildMenu() {
    // Rebuild the entire menu from scratch on every state change.
    // Wayland compositors (COSMIC, etc.) cache the menu structure and
    // ignore setVisible/setEnabled changes on existing QActions.
    auto *oldMenu = m_menu;
    m_menu = new QMenu;

    bool isRecording = m_recordPage->recorder()->isRecording();
    bool isPaused = m_recordPage->recorder()->isPaused();
    bool isBusy = (m_state == RoomNoise || m_state == Processing);

    // Recording controls — show only what's relevant
    if (!isRecording && !isPaused && !isBusy) {
        m_menu->addAction("Start Recording", this, [this]() {
            m_recordPage->onStartClicked();
        });
    }
    if (isRecording) {
        m_menu->addAction("Pause", this, [this]() {
            m_recordPage->onPauseClicked();
        });
    }
    if (isPaused) {
        m_menu->addAction("Resume", this, [this]() {
            m_recordPage->onPauseClicked();
        });
    }
    if (isRecording || isPaused) {
        m_menu->addAction("Stop Recording", this, [this]() {
            m_recordPage->onStopClicked();
        });
    }

    m_menu->addSeparator();

    // Presets
    auto *presetMenu = m_menu->addMenu("Presets");
    auto &cfg = Config::instance();
    if (cfg.presets.isEmpty()) {
        presetMenu->addAction("(no presets)")->setEnabled(false);
    } else {
        for (const auto &name : cfg.presets.keys()) {
            bool isActive = (name == cfg.activePreset);
            QString display = isActive ? QString::fromUtf8("\u2713 ") + name : "   " + name;
            presetMenu->addAction(display, this, [this, name]() {
                m_recordPage->loadPreset(name);
            });
        }
    }

    m_menu->addSeparator();
    m_menu->addAction("Open Window", this, [this]() {
        QTimer::singleShot(50, m_mainWindow, [this]() {
            m_mainWindow->showFromTray();
        });
    });
    m_menu->addSeparator();
    m_menu->addAction("Quit", this, [this]() {
        m_trayIcon->hide();
        m_mainWindow->setProperty("quitting", true);
        QApplication::quit();
    });

    m_trayIcon->setContextMenu(m_menu);

    if (oldMenu) {
        oldMenu->deleteLater();
    }
}

void Tray::setState(State s) {
    m_state = s;

    QString iconPath;
    QString tooltip;

    switch (s) {
    case Idle:
        iconPath = ":/icons/ready.png";
        tooltip = "Kartoza Screencaster - Click to record";
        break;
    case Countdown:
        iconPath = ":/icons/ready.png";
        tooltip = QString("Starting in %1...").arg(m_countdownVal);
        break;
    case Recording:
        iconPath = ":/icons/recording.png";
        tooltip = "Recording - Click to pause";
        break;
    case Paused:
        iconPath = ":/icons/ready.png";
        tooltip = "Paused - Click to resume";
        break;
    case RoomNoise:
        iconPath = ":/icons/recording.png";
        tooltip = "Recording room noise - Please keep quiet!";
        break;
    case Processing:
        iconPath = ":/icons/ready.png";
        tooltip = "Processing video...";
        break;
    }

    QIcon icon(iconPath);
    if (icon.isNull()) icon = QIcon::fromTheme("camera-video");
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip(tooltip);

    // Rebuild menu with correct actions for new state
    rebuildMenu();
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
    rebuildMenu();
}
