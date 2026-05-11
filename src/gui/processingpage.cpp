#include "gui/processingpage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProcess>

static const QStringList stepNames = {
    "Analyzing audio",
    "Normalizing audio",
    "Merging video & audio",
    "Creating vertical video",
};

ProcessingPage::ProcessingPage(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(30, 20, 30, 20);
    layout->setSpacing(8);

    auto *title = new QLabel("Processing Recording...");
    title->setStyleSheet("QLabel { color: #cdd6f4; font-size: 18px; font-weight: bold; }");
    layout->addWidget(title);

    QString barStyle = "QProgressBar { background: #313244; border: none; border-radius: 4px; height: 18px; color: #cdd6f4; text-align: center; } QProgressBar::chunk { background: #89b4fa; border-radius: 4px; }";
    QString failedBarStyle = "QProgressBar { background: #313244; border: none; border-radius: 4px; height: 18px; color: #cdd6f4; text-align: center; } QProgressBar::chunk { background: #f38ba8; border-radius: 4px; }";

    for (int i = 0; i < stepNames.size(); i++) {
        auto *row = new QHBoxLayout;
        auto *label = new QLabel(QString("Step %1: %2").arg(i+1).arg(stepNames[i]));
        label->setStyleSheet("QLabel { color: #cdd6f4; font-size: 12px; }");
        label->setFixedWidth(230);
        row->addWidget(label);

        auto *bar = new QProgressBar;
        bar->setStyleSheet(barStyle);
        bar->setValue(0);
        m_bars.append(bar);
        row->addWidget(bar);

        auto *status = new QLabel("Pending");
        status->setStyleSheet("QLabel { color: #6c7086; font-size: 11px; }");
        status->setFixedWidth(70);
        m_statusLabels.append(status);
        row->addWidget(status);

        layout->addLayout(row);

        // Error detail label (hidden by default)
        auto *errLabel = new QLabel;
        errLabel->setStyleSheet("QLabel { color: #f38ba8; font-size: 11px; padding-left: 230px; }");
        errLabel->setWordWrap(true);
        errLabel->hide();
        m_errorLabels.append(errLabel);
        layout->addWidget(errLabel);
    }

    m_elapsedLabel = new QLabel("Elapsed: 00:00");
    m_elapsedLabel->setStyleSheet("QLabel { color: #6c7086; font-size: 12px; padding-top: 10px; }");
    layout->addWidget(m_elapsedLabel);

    // Summary (hidden until processing completes)
    m_summaryLabel = new QLabel;
    m_summaryLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; padding-top: 10px; }");
    m_summaryLabel->hide();
    layout->addWidget(m_summaryLabel);

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);

    m_cancelBtn = new QPushButton("Cancel Processing");
    m_cancelBtn->setStyleSheet("QPushButton { background: #f38ba8; color: #1e1e2e; border: none; border-radius: 4px; padding: 8px 16px; font-weight: bold; } QPushButton:hover { background: #eba0ac; }");
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_recorder) m_recorder->cancelProcessing();
        m_cancelBtn->hide();
        emit processingCancelled();
    });
    btnRow->addWidget(m_cancelBtn);

    m_openFolderBtn = new QPushButton("Open Folder");
    m_openFolderBtn->setStyleSheet("QPushButton { background: #89b4fa; color: #1e1e2e; border: none; border-radius: 4px; padding: 8px 16px; font-weight: bold; } QPushButton:hover { background: #74c7ec; }");
    m_openFolderBtn->hide();
    connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
        if (m_recorder && !m_recorder->outputDir().isEmpty()) {
            QProcess::startDetached("xdg-open", {m_recorder->outputDir()});
        }
    });
    btnRow->addWidget(m_openFolderBtn);

    m_backBtn = new QPushButton("Back to History");
    m_backBtn->setStyleSheet("QPushButton { background: #a6e3a1; color: #1e1e2e; border: none; border-radius: 4px; padding: 8px 16px; font-weight: bold; } QPushButton:hover { background: #94e2d5; }");
    m_backBtn->hide();
    connect(m_backBtn, &QPushButton::clicked, this, [this]() {
        emit backToHistory();
    });
    btnRow->addWidget(m_backBtn);

    btnRow->addStretch();
    layout->addLayout(btnRow);

    layout->addStretch();

    m_elapsedTimer = new QTimer(this);
    connect(m_elapsedTimer, &QTimer::timeout, this, [this]() {
        qint64 ms = m_elapsed.elapsed();
        int m = (ms / 60000) % 60;
        int s = (ms / 1000) % 60;
        m_elapsedLabel->setText(QString("Elapsed: %1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0')));
    });
}

void ProcessingPage::startMonitoring(Recorder *recorder) {
    // Disconnect previous recorder if any
    if (m_recorder) {
        disconnect(m_recorder, nullptr, this, nullptr);
    }
    m_recorder = recorder;

    // Reset UI
    QString barStyle = "QProgressBar { background: #313244; border: none; border-radius: 4px; height: 18px; color: #cdd6f4; text-align: center; } QProgressBar::chunk { background: #89b4fa; border-radius: 4px; }";
    for (auto *bar : m_bars) { bar->setValue(0); bar->setStyleSheet(barStyle); }
    for (auto *lbl : m_statusLabels) {
        lbl->setText("Pending");
        lbl->setStyleSheet("QLabel { color: #6c7086; font-size: 11px; }");
    }
    for (auto *lbl : m_errorLabels) {
        lbl->clear();
        lbl->hide();
    }
    m_summaryLabel->hide();
    m_cancelBtn->show();
    m_backBtn->hide();
    m_openFolderBtn->hide();

    m_elapsed.start();
    m_elapsedTimer->start(1000);

    // Room noise capture status
    connect(recorder, &Recorder::roomNoiseStarted, this, [this]() {
        m_elapsedLabel->setText("Capturing room noise - Please keep quiet!");
        m_elapsedLabel->setStyleSheet("QLabel { color: #fab387; font-size: 12px; font-weight: bold; padding-top: 10px; }");
    });
    connect(recorder, &Recorder::roomNoiseProgress, this, [this](int secs) {
        m_elapsedLabel->setText(QString("Room noise capture: %1s remaining").arg(secs));
    });
    connect(recorder, &Recorder::roomNoiseFinished, this, [this]() {
        m_elapsedLabel->setStyleSheet("QLabel { color: #6c7086; font-size: 12px; padding-top: 10px; }");
        m_elapsed.start();
    });

    // Processing progress
    connect(recorder, &Recorder::processingProgress, this, [this](int step, int percent, const QString &) {
        if (step >= 0 && step < m_bars.size()) {
            m_bars[step]->setValue(percent);
            m_statusLabels[step]->setText(QString("%1%").arg(percent));
            m_statusLabels[step]->setStyleSheet("QLabel { color: #89b4fa; font-size: 11px; }");
        }
    });

    // Step completed
    connect(recorder, &Recorder::processingStepDone, this, [this](int step, const QString &, bool skipped) {
        if (step >= 0 && step < m_bars.size()) {
            m_bars[step]->setValue(100);
            if (skipped) {
                m_statusLabels[step]->setText("Skipped");
                m_statusLabels[step]->setStyleSheet("QLabel { color: #6c7086; font-size: 11px; }");
            } else {
                m_statusLabels[step]->setText("Done");
                m_statusLabels[step]->setStyleSheet("QLabel { color: #a6e3a1; font-size: 11px; font-weight: bold; }");
            }
        }
    });

    // Step error
    connect(recorder, &Recorder::processingStepError, this, [this](int step, const QString &, const QString &error) {
        if (step >= 0 && step < m_bars.size()) {
            m_bars[step]->setStyleSheet("QProgressBar { background: #313244; border: none; border-radius: 4px; height: 18px; color: #cdd6f4; text-align: center; } QProgressBar::chunk { background: #f38ba8; border-radius: 4px; }");
            m_statusLabels[step]->setText("Failed");
            m_statusLabels[step]->setStyleSheet("QLabel { color: #f38ba8; font-size: 11px; font-weight: bold; }");
        }
        if (step >= 0 && step < m_errorLabels.size() && !error.isEmpty()) {
            m_errorLabels[step]->setText(error);
            m_errorLabels[step]->show();
        }
    });

    // Processing finished
    connect(recorder, &Recorder::processingFinished, this, [this](bool success) {
        m_elapsedTimer->stop();
        m_cancelBtn->hide();

        if (success) {
            m_summaryLabel->setText("Processing complete!");
            m_summaryLabel->setStyleSheet("QLabel { color: #a6e3a1; font-size: 14px; font-weight: bold; padding-top: 10px; }");
        } else {
            m_summaryLabel->setText("Processing finished with errors");
            m_summaryLabel->setStyleSheet("QLabel { color: #f38ba8; font-size: 14px; font-weight: bold; padding-top: 10px; }");
        }
        m_summaryLabel->show();
        m_backBtn->show();
        m_openFolderBtn->show();

        emit processingDone(success);
    });
}
