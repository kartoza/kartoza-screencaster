#include "gui/processingpage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

static const QStringList stepNames = {
    "Analyzing audio",
    "Normalizing audio",
    "Merging video & audio",
    "Creating vertical video",
};

ProcessingPage::ProcessingPage(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(30, 20, 30, 20);
    layout->setSpacing(10);

    auto *title = new QLabel("Processing Recording...");
    title->setStyleSheet("QLabel { color: #cdd6f4; font-size: 18px; font-weight: bold; }");
    layout->addWidget(title);

    QString barStyle = "QProgressBar { background: #313244; border: none; border-radius: 4px; height: 18px; color: #cdd6f4; text-align: center; } QProgressBar::chunk { background: #89b4fa; border-radius: 4px; }";

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
        status->setFixedWidth(60);
        m_statusLabels.append(status);
        row->addWidget(status);

        layout->addLayout(row);
    }

    m_elapsedLabel = new QLabel("Elapsed: 00:00");
    m_elapsedLabel->setStyleSheet("QLabel { color: #6c7086; font-size: 12px; padding-top: 10px; }");
    layout->addWidget(m_elapsedLabel);

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
    for (auto *bar : m_bars) bar->setValue(0);
    for (auto *lbl : m_statusLabels) {
        lbl->setText("Pending");
        lbl->setStyleSheet("QLabel { color: #6c7086; font-size: 11px; }");
    }

    m_elapsed.start();
    m_elapsedTimer->start(1000);

    // Native Qt signals/slots — JUST WORKS across threads!
    connect(recorder, &Recorder::processingProgress, this, [this](int step, int percent, const QString &) {
        if (step >= 0 && step < m_bars.size()) {
            m_bars[step]->setValue(percent);
            m_statusLabels[step]->setText(QString("%1%").arg(percent));
            m_statusLabels[step]->setStyleSheet("QLabel { color: #89b4fa; font-size: 11px; }");
        }
    });
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
    connect(recorder, &Recorder::processingFinished, this, [this](bool success) {
        m_elapsedTimer->stop();
        emit processingDone(success);
    });
}
