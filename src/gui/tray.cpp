#include "gui/tray.h"
#include "gui/mainwindow.h"
#include "config/config.h"
#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QDebug>

Tray::Tray(MainWindow *mainWindow, RecordPage *recordPage)
    : QObject(mainWindow), m_mainWindow(mainWindow), m_recordPage(recordPage) {

    m_trayIcon = new QSystemTrayIcon(this);
    m_svgRenderer = new QSvgRenderer(QString(":/icons/ready.svg"), this);
    m_countdownTimer = new QTimer(this);
    connect(m_countdownTimer, &QTimer::timeout, this, &Tray::onCountdownTick);

    // Build a single persistent menu — COSMIC caches D-Bus menus
    // so we can't rebuild. Instead we update text/enabled state.
    m_menu = new QMenu;

    // Primary action: Start / Pause / Resume (changes text per state)
    m_recordAction = m_menu->addAction("Start Recording");
    connect(m_recordAction, &QAction::triggered, this, [this]() {
        auto *rec = m_recordPage->recorder();
        if (rec->isRecording()) {
            m_recordPage->onPauseClicked();
        } else if (rec->isPaused()) {
            m_recordPage->onPauseClicked();
        } else {
            m_recordPage->onStartClicked();
        }
    });

    // Stop action
    m_stopAction = m_menu->addAction("Stop Recording");
    connect(m_stopAction, &QAction::triggered, this, [this]() {
        auto *rec = m_recordPage->recorder();
        if (rec->isRecording() || rec->isPaused()) {
            m_recordPage->onStopClicked();
        }
    });

    m_menu->addSeparator();

    // Presets submenu
    m_presetMenu = m_menu->addMenu("Presets");
    refreshPresetMenu();

    m_menu->addSeparator();

    // Open Window
    m_openAction = m_menu->addAction("Open Window");
    connect(m_openAction, &QAction::triggered, this, [this]() {
        QTimer::singleShot(50, m_mainWindow, [this]() {
            m_mainWindow->showFromTray();
        });
    });

    m_menu->addSeparator();

    // Quit
    auto *quitAction = m_menu->addAction("Quit");
    connect(quitAction, &QAction::triggered, this, [this]() {
        m_trayIcon->hide();
        m_mainWindow->setProperty("quitting", true);
        QApplication::quit();
    });

    m_trayIcon->setContextMenu(m_menu);

    // Single click behavior — always check recorder state directly
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Trigger) return;
        auto *rec = m_recordPage->recorder();
        if (rec->isRecording()) {
            m_recordPage->onPauseClicked();
        } else if (rec->isPaused()) {
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
    connect(m_recordPage, &RecordPage::presetChanged, this, [this]() {
        refreshPresetMenu();
    });

    // Countdown numbers in systray icon
    connect(m_recordPage, &RecordPage::countdownStarted, this, [this]() {
        m_state = Countdown;
        updateMenuState();
    });
    connect(m_recordPage, &RecordPage::countdownTick, this, [this](int secs) {
        m_trayIcon->setIcon(buildCountdownIcon(secs));
        m_trayIcon->setToolTip(QString("Starting in %1...").arg(secs));
    });

    setState(Idle);
    m_trayIcon->show();
}

void Tray::updateMenuState() {
    auto *rec = m_recordPage->recorder();
    bool recording = rec->isRecording();
    bool paused = rec->isPaused();
    bool busy = (m_state == RoomNoise || m_state == Processing);

    // Update primary action text and enabled state
    if (recording) {
        m_recordAction->setText("Pause");
        m_recordAction->setEnabled(true);
    } else if (paused) {
        m_recordAction->setText("Resume");
        m_recordAction->setEnabled(true);
    } else if (busy) {
        m_recordAction->setText("Processing...");
        m_recordAction->setEnabled(false);
    } else {
        m_recordAction->setText("Start Recording");
        m_recordAction->setEnabled(true);
    }

    // Stop action: only enabled during recording or paused
    m_stopAction->setEnabled(recording || paused);
}

void Tray::setState(State s) {
    m_state = s;

    // Stop rotation timer if leaving Processing state
    if (s != Processing && m_rotationTimer) {
        m_rotationTimer->stop();
    }

    QString tooltip;

    switch (s) {
    case Idle:
        tooltip = "Kartoza Screencaster - Click to record";
        m_trayIcon->setIcon(QIcon(":/icons/ready.png"));
        break;
    case Countdown:
        tooltip = QString("Starting in %1...").arg(m_countdownVal);
        m_trayIcon->setIcon(QIcon(":/icons/ready.png"));
        break;
    case Recording:
        tooltip = "Recording - Click to pause";
        m_trayIcon->setIcon(QIcon(":/icons/recording.png"));
        break;
    case Paused:
        tooltip = "Paused - Click to resume";
        m_trayIcon->setIcon(buildPausedIcon());
        break;
    case RoomNoise:
        tooltip = "Recording room noise - Please keep quiet!";
        m_trayIcon->setIcon(buildDotIcon(QColor(86, 159, 198))); // Kartoza blue
        break;
    case Processing:
        tooltip = "Processing video...";
        m_rotationAngle = 0;
        if (!m_rotationTimer) {
            m_rotationTimer = new QTimer(this);
            connect(m_rotationTimer, &QTimer::timeout, this, [this]() {
                m_rotationAngle = (m_rotationAngle + 10) % 360;
                m_trayIcon->setIcon(buildDotIcon(QColor(232, 232, 236), m_rotationAngle)); // white dot, rotating
            });
        }
        m_trayIcon->setIcon(buildDotIcon(QColor(232, 232, 236)));
        m_rotationTimer->start(100);
        break;
    }

    m_trayIcon->setToolTip(tooltip);
    updateMenuState();
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

QIcon Tray::buildCountdownIcon(int number) {
    QPixmap pix(64, 64);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // Render the base SVG
    m_svgRenderer->render(&p, QRectF(0, 0, 64, 64));

    // Draw the number in the center
    QFont font;
    font.setPixelSize(28);
    font.setBold(true);
    p.setFont(font);
    p.setPen(QColor("#e8e8ec"));
    p.drawText(QRect(0, 0, 64, 64), Qt::AlignCenter, QString::number(number));

    p.end();
    return QIcon(pix);
}

QIcon Tray::buildPausedIcon() {
    QPixmap pix(64, 64);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // Render the base SVG
    m_svgRenderer->render(&p, QRectF(0, 0, 64, 64));

    // Draw pause bars (two vertical rectangles) in the center
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#e8e8ec"));
    int barW = 7, barH = 24;
    int gap = 5;
    int cx = 32, cy = 32;
    p.drawRoundedRect(cx - gap - barW, cy - barH/2, barW, barH, 2, 2);
    p.drawRoundedRect(cx + gap, cy - barH/2, barW, barH, 2, 2);

    p.end();
    return QIcon(pix);
}

QIcon Tray::buildDotIcon(const QColor &dotColor, int rotationDeg) {
    QPixmap pix(64, 64);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // Apply rotation around center
    if (rotationDeg != 0) {
        p.translate(32, 32);
        p.rotate(rotationDeg);
        p.translate(-32, -32);
    }

    // Render the base SVG
    m_svgRenderer->render(&p, QRectF(0, 0, 64, 64));

    // Reset transform for the dot (always centered, not rotated)
    p.resetTransform();

    // Draw center dot
    p.setPen(Qt::NoPen);
    p.setBrush(dotColor);
    p.drawEllipse(QPoint(32, 32), 8, 8);

    p.end();
    return QIcon(pix);
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
