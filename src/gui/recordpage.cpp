#include "gui/recordpage.h"
#include "config/config.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QMenu>
#include <QFileDialog>
#include <QDateTime>

RecordPage::RecordPage(QWidget *parent) : QWidget(parent) {
    m_recorder = new Recorder(this);
    m_monitors = Monitor::listMonitors();
    m_webcams = Webcam::detectAll();

    setupUI();

    // Connect recorder signals — native Qt, works perfectly across threads
    connect(m_recorder, &Recorder::recordingStarted, this, &RecordPage::onRecorderStarted);
    connect(m_recorder, &Recorder::recordingStopped, this, &RecordPage::onRecorderStopped);
    connect(m_recorder, &Recorder::recordingError, this, &RecordPage::onRecorderError);

    // Countdown timer
    m_countdownTimer = new QTimer(this);
    connect(m_countdownTimer, &QTimer::timeout, this, &RecordPage::onCountdownTick);

    // Elapsed timer
    m_elapsedTimer = new QTimer(this);
    connect(m_elapsedTimer, &QTimer::timeout, this, [this]() {
        qint64 ms = m_elapsed.elapsed();
        int h = ms / 3600000;
        int m = (ms / 60000) % 60;
        int s = (ms / 1000) % 60;
        m_elapsedLabel->setText(QString("%1:%2:%3")
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0')));
    });
}

void RecordPage::setupUI() {
    auto &cfg = Config::instance();
    cfg.load();

    auto *layout = new QHBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(10, 10, 10, 10);

    QString labelStyle = "QLabel { color: #cdd6f4; font-size: 12px; }";
    QString inputStyle = "QLineEdit { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 5px; font-size: 12px; }";

    // === Left sidebar ===
    auto *leftCol = new QWidget;
    leftCol->setFixedWidth(220);
    auto *leftLayout = new QVBoxLayout(leftCol);
    leftLayout->setSpacing(6);

    auto *metaLabel = new QLabel("Recording");
    metaLabel->setStyleSheet("QLabel { color: #89b4fa; font-size: 14px; font-weight: bold; }");
    leftLayout->addWidget(metaLabel);

    // Title
    auto *titleRow = new QHBoxLayout;
    auto *titleLbl = new QLabel("Title:");
    titleLbl->setStyleSheet(labelStyle);
    titleLbl->setFixedWidth(60);
    titleRow->addWidget(titleLbl);
    m_titleInput = new QLineEdit;
    m_titleInput->setPlaceholderText("Title...");
    m_titleInput->setStyleSheet(inputStyle);
    m_titleInput->setToolTip("Recording title. Appears on canvas and in filenames.");
    connect(m_titleInput, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_canvas->setTitle(text);
    });
    titleRow->addWidget(m_titleInput);
    leftLayout->addLayout(titleRow);

    // Number
    auto *numRow = new QHBoxLayout;
    auto *numLbl = new QLabel("Number:");
    numLbl->setStyleSheet(labelStyle);
    numLbl->setFixedWidth(60);
    numRow->addWidget(numLbl);
    m_numberInput = new QLineEdit;
    m_numberInput->setStyleSheet(inputStyle);
    m_numberInput->setText("001"); // TODO: auto-increment
    m_numberInput->setToolTip("Sequential recording number.");
    numRow->addWidget(m_numberInput);
    leftLayout->addLayout(numRow);

    // Presenter
    auto *presRow = new QHBoxLayout;
    auto *presLbl = new QLabel("Presenter:");
    presLbl->setStyleSheet(labelStyle);
    presLbl->setFixedWidth(60);
    presRow->addWidget(presLbl);
    m_presenterInput = new QLineEdit;
    m_presenterInput->setStyleSheet(inputStyle);
    m_presenterInput->setText(cfg.defaultPresenter);
    m_presenterInput->setToolTip("Presenter name.");
    presRow->addWidget(m_presenterInput);
    leftLayout->addLayout(presRow);

    m_audioCheck = new QCheckBox("Record Audio");
    m_audioCheck->setChecked(true);
    m_audioCheck->setStyleSheet("QCheckBox { color: #cdd6f4; font-size: 12px; }");
    m_audioCheck->setToolTip("Record microphone audio.");
    leftLayout->addWidget(m_audioCheck);

    // Canvas mode
    auto *modeLabel = new QLabel("Canvas Mode");
    modeLabel->setStyleSheet("QLabel { color: #89b4fa; font-size: 12px; font-weight: bold; padding-top: 6px; }");
    leftLayout->addWidget(modeLabel);

    QString radioStyle = "QRadioButton { color: #cdd6f4; font-size: 12px; }";
    m_modeGroup = new QButtonGroup(this);

    auto *landscapeRadio = new QRadioButton("Landscape 16:9");
    landscapeRadio->setChecked(true);
    landscapeRadio->setStyleSheet(radioStyle);
    m_modeGroup->addButton(landscapeRadio, 0);
    leftLayout->addWidget(landscapeRadio);

    auto *verticalRadio = new QRadioButton("Vertical 9:16");
    verticalRadio->setStyleSheet(radioStyle);
    m_modeGroup->addButton(verticalRadio, 1);
    leftLayout->addWidget(verticalRadio);

    auto *leftSplitRadio = new QRadioButton("Vertical (Left Split)");
    leftSplitRadio->setStyleSheet(radioStyle);
    m_modeGroup->addButton(leftSplitRadio, 2);
    leftLayout->addWidget(leftSplitRadio);

    auto *rightSplitRadio = new QRadioButton("Vertical (Right Split)");
    rightSplitRadio->setStyleSheet(radioStyle);
    m_modeGroup->addButton(rightSplitRadio, 3);
    leftLayout->addWidget(rightSplitRadio);

    connect(m_modeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_canvas->setMode(id);
    });

    leftLayout->addStretch();
    layout->addWidget(leftCol);

    // === Center: Canvas + Controls ===
    auto *centerCol = new QWidget;
    auto *centerLayout = new QVBoxLayout(centerCol);
    centerLayout->setSpacing(8);
    centerLayout->setContentsMargins(0, 0, 0, 0);

    m_canvas = new Canvas(this);
    m_canvas->setToolTip("WYSIWYG preview.\nAdd elements with + button.\nDrag to move, scroll to resize.");
    centerLayout->addWidget(m_canvas, 1);

    // Add element + controls
    auto *addRow = new QHBoxLayout;
    addRow->setSpacing(8);

    auto *addBtn = new QPushButton("+ Add Element");
    addBtn->setStyleSheet("QPushButton { background: #89b4fa; color: #1e1e2e; border: none; border-radius: 6px; padding: 8px 16px; font-size: 13px; font-weight: bold; } QPushButton:hover { background: #74c7ec; }");

    auto *addMenu = new QMenu(this);
    addMenu->setStyleSheet("QMenu { background: #1e1e2e; color: #cdd6f4; border: 1px solid #45475a; } QMenu::item { padding: 6px 20px; } QMenu::item:selected { background: #45475a; }");

    auto *screenMenu = addMenu->addMenu("Screen");
    for (const auto &mon : m_monitors) {
        QString label = mon.description.isEmpty() ? mon.name : mon.description;
        label += QString(" (%1x%2)").arg(mon.width).arg(mon.height);
        auto *action = screenMenu->addAction(label);
        connect(action, &QAction::triggered, this, [this, mon]() {
            addScreen(mon);
        });
    }

    auto *webcamMenu = addMenu->addMenu("Webcam");
    for (const auto &dev : m_webcams) {
        auto *devMenu = webcamMenu->addMenu(dev.name);
        auto *roundAction = devMenu->addAction("Round bubble");
        connect(roundAction, &QAction::triggered, this, [this, dev]() { m_canvas->addWebcam(dev.device, dev.name, 0); });
        auto *squareAction = devMenu->addAction("Square");
        connect(squareAction, &QAction::triggered, this, [this, dev]() { m_canvas->addWebcam(dev.device, dev.name, 1); });
        auto *rectAction = devMenu->addAction("Rectangle");
        connect(rectAction, &QAction::triggered, this, [this, dev]() { m_canvas->addWebcam(dev.device, dev.name, 2); });
    }

    auto *logoAction = addMenu->addAction("Logo");
    connect(logoAction, &QAction::triggered, this, [this]() {
        QString file = QFileDialog::getOpenFileName(this, "Select Logo");
        if (!file.isEmpty()) m_canvas->addLogo(file);
    });

    auto *titleAction = addMenu->addAction("Title Text");
    connect(titleAction, &QAction::triggered, this, [this]() {
        QString t = m_titleInput->text();
        if (t.isEmpty()) t = "Title";
        m_canvas->setTitle(t);
    });

    connect(addBtn, &QPushButton::clicked, this, [addBtn, addMenu]() {
        addMenu->popup(addBtn->mapToGlobal(QPoint(0, addBtn->height())));
    });
    addRow->addWidget(addBtn);
    addRow->addStretch();

    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setStyleSheet("QLabel { color: #a6e3a1; font-size: 13px; font-weight: bold; }");
    m_statusLabel->setWordWrap(true);
    addRow->addWidget(m_statusLabel);

    m_elapsedLabel = new QLabel("00:00:00");
    m_elapsedLabel->setStyleSheet("QLabel { color: #cdd6f4; font-size: 16px; font-weight: bold; font-family: monospace; }");
    addRow->addWidget(m_elapsedLabel);

    centerLayout->addLayout(addRow);

    // Buttons
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);

    m_startBtn = new QPushButton("Start Recording");
    m_startBtn->setStyleSheet("QPushButton { background: #a6e3a1; color: #1e1e2e; border: none; border-radius: 6px; padding: 10px 24px; font-size: 14px; font-weight: bold; } QPushButton:hover { background: #94e2d5; } QPushButton:disabled { background: #45475a; color: #6c7086; }");
    connect(m_startBtn, &QPushButton::clicked, this, &RecordPage::onStartClicked);
    btnRow->addWidget(m_startBtn);

    m_pauseBtn = new QPushButton("Pause");
    m_pauseBtn->setStyleSheet("QPushButton { background: #fab387; color: #1e1e2e; border: none; border-radius: 6px; padding: 10px 16px; font-size: 13px; font-weight: bold; } QPushButton:hover { background: #f9e2af; }");
    m_pauseBtn->hide();
    connect(m_pauseBtn, &QPushButton::clicked, this, &RecordPage::onPauseClicked);
    btnRow->addWidget(m_pauseBtn);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setStyleSheet("QPushButton { background: #f38ba8; color: #1e1e2e; border: none; border-radius: 6px; padding: 10px 16px; font-size: 13px; font-weight: bold; } QPushButton:hover { background: #eba0ac; }");
    m_stopBtn->hide();
    connect(m_stopBtn, &QPushButton::clicked, this, &RecordPage::onStopClicked);
    btnRow->addWidget(m_stopBtn);

    centerLayout->addLayout(btnRow);
    layout->addWidget(centerCol, 1);
}

void RecordPage::addScreen(const MonitorInfo &mon) {
    m_canvas->setMonitor(mon);
}

void RecordPage::onStartClicked() {
    if (m_titleInput->text().isEmpty()) {
        m_statusLabel->setText("Enter a title");
        m_statusLabel->setStyleSheet("QLabel { color: #f38ba8; font-size: 13px; font-weight: bold; }");
        return;
    }

    m_countdownVal = 5;
    m_statusLabel->setText("Starting in 5...");
    m_statusLabel->setStyleSheet("QLabel { color: #fab387; font-size: 13px; font-weight: bold; }");
    m_startBtn->setEnabled(false);
    m_countdownTimer->start(1000);
}

void RecordPage::onCountdownTick() {
    m_countdownVal--;
    if (m_countdownVal <= 0) {
        m_countdownTimer->stop();
        m_statusLabel->setText("Starting recorders...");

        // Build options and start
        RecordingOptions opts;
        opts.monitor = m_canvas->selectedMonitor();
        opts.noScreen = opts.monitor.isEmpty();
        opts.noAudio = !m_audioCheck->isChecked();
        opts.title = m_titleInput->text();
        opts.number = m_numberInput->text().toInt();
        opts.presenter = m_presenterInput->text();

        m_recorder->start(opts);
    } else {
        m_statusLabel->setText(QString("Starting in %1...").arg(m_countdownVal));
    }
}

void RecordPage::onRecorderStarted() {
    // This slot is called via signal/slot — guaranteed on the main thread!
    m_isRecording = true;
    m_statusLabel->setText("Recording");
    m_statusLabel->setStyleSheet("QLabel { color: #f38ba8; font-size: 13px; font-weight: bold; }");
    m_startBtn->hide();
    m_pauseBtn->show();
    m_stopBtn->show();
    m_elapsed.start();
    m_elapsedTimer->start(1000);

    emit recordingStarted(); // MainWindow hides
}

void RecordPage::onStopClicked() {
    m_statusLabel->setText("Stopping...");
    m_stopBtn->setEnabled(false);
    m_pauseBtn->setEnabled(false);
    m_recorder->stop();
}

void RecordPage::onRecorderStopped() {
    // Signal/slot — main thread!
    m_isRecording = false;
    m_elapsedTimer->stop();
    m_elapsedLabel->setText("00:00:00");
    m_startBtn->show();
    m_startBtn->setEnabled(true);
    m_pauseBtn->hide();
    m_pauseBtn->setEnabled(true);
    m_stopBtn->hide();
    m_stopBtn->setEnabled(true);

    // Increment number
    int num = m_numberInput->text().toInt() + 1;
    m_numberInput->setText(QString("%1").arg(num, 3, 10, QChar('0')));

    m_statusLabel->setText("Ready");
    m_statusLabel->setStyleSheet("QLabel { color: #a6e3a1; font-size: 13px; font-weight: bold; }");

    emit recordingStopped(); // MainWindow shows + navigates to processing
}

void RecordPage::onRecorderError(const QString &error) {
    m_statusLabel->setText("Error: " + error);
    m_statusLabel->setStyleSheet("QLabel { color: #f38ba8; font-size: 13px; font-weight: bold; }");
    m_startBtn->show();
    m_startBtn->setEnabled(true);
    m_pauseBtn->hide();
    m_stopBtn->hide();
}

void RecordPage::onPauseClicked() {
    if (m_recorder->isPaused()) {
        m_recorder->resume();
        m_pauseBtn->setText("Pause");
        m_statusLabel->setText("Recording");
        m_statusLabel->setStyleSheet("QLabel { color: #f38ba8; font-size: 13px; font-weight: bold; }");
    } else {
        m_recorder->pause();
        m_pauseBtn->setText("Resume");
        m_statusLabel->setText("Paused");
        m_statusLabel->setStyleSheet("QLabel { color: #fab387; font-size: 13px; font-weight: bold; }");
    }
}
