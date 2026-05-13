#include "gui/processingpage.h"
#include "youtube/youtube.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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
    title->setStyleSheet("QLabel { color: #e8e8ec; font-size: 18px; font-weight: bold; }");
    layout->addWidget(title);

    QString barStyle = "QProgressBar { background: #2d2d44; border: none; border-radius: 4px; height: 18px; color: #e8e8ec; text-align: center; } QProgressBar::chunk { background: #569FC6; border-radius: 4px; }";
    QString failedBarStyle = "QProgressBar { background: #2d2d44; border: none; border-radius: 4px; height: 18px; color: #e8e8ec; text-align: center; } QProgressBar::chunk { background: #CC0403; border-radius: 4px; }";

    for (int i = 0; i < stepNames.size(); i++) {
        auto *row = new QHBoxLayout;
        auto *label = new QLabel(QString("Step %1: %2").arg(i+1).arg(stepNames[i]));
        label->setStyleSheet("QLabel { color: #e8e8ec; font-size: 12px; }");
        label->setFixedWidth(230);
        row->addWidget(label);

        auto *bar = new QProgressBar;
        bar->setStyleSheet(barStyle);
        bar->setValue(0);
        m_bars.append(bar);
        row->addWidget(bar);

        auto *status = new QLabel("Pending");
        status->setStyleSheet("QLabel { color: #8A8B8B; font-size: 11px; }");
        status->setFixedWidth(70);
        m_statusLabels.append(status);
        row->addWidget(status);

        layout->addLayout(row);

        // Error detail label (hidden by default)
        auto *errLabel = new QLabel;
        errLabel->setStyleSheet("QLabel { color: #CC0403; font-size: 11px; padding-left: 230px; }");
        errLabel->setWordWrap(true);
        errLabel->hide();
        m_errorLabels.append(errLabel);
        layout->addWidget(errLabel);
    }

    m_elapsedLabel = new QLabel("Elapsed: 00:00");
    m_elapsedLabel->setStyleSheet("QLabel { color: #8A8B8B; font-size: 12px; padding-top: 10px; }");
    layout->addWidget(m_elapsedLabel);

    // Summary (hidden until processing completes)
    m_summaryLabel = new QLabel;
    m_summaryLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; padding-top: 10px; }");
    m_summaryLabel->hide();
    layout->addWidget(m_summaryLabel);

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);

    m_openFolderBtn = new QPushButton(QString::fromUtf8("\u2302 Folder"));
    m_openFolderBtn->setStyleSheet("QPushButton { background: #3d3d56; color: #e8e8ec; border: none; border-radius: 4px; padding: 8px 16px; font-size: 13px; } QPushButton:hover { background: #4d4d68; }");
    m_openFolderBtn->hide();
    connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
        if (m_recorder && !m_recorder->outputDir().isEmpty()) {
            QProcess::startDetached("xdg-open", {m_recorder->outputDir()});
        }
    });
    btnRow->addWidget(m_openFolderBtn);

    m_backBtn = new QPushButton(QString::fromUtf8("\u2190 History"));
    m_backBtn->setStyleSheet("QPushButton { background: #06969A; color: #ffffff; border: none; border-radius: 4px; padding: 8px 16px; font-size: 13px; } QPushButton:hover { background: #058084; }");
    m_backBtn->hide();
    connect(m_backBtn, &QPushButton::clicked, this, [this]() {
        emit backToHistory();
    });
    btnRow->addWidget(m_backBtn);

    btnRow->addStretch(); // push destructive action right

    m_cancelBtn = new QPushButton(QString::fromUtf8("\u2716 Cancel"));
    m_cancelBtn->setStyleSheet("QPushButton { background: #CC0403; color: #ffffff; border: none; border-radius: 4px; padding: 8px 16px; font-size: 13px; } QPushButton:hover { background: #E03030; }");
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_recorder) m_recorder->cancelProcessing();
        m_cancelBtn->hide();
        emit processingCancelled();
    });
    btnRow->addWidget(m_cancelBtn);

    layout->addLayout(btnRow);

    // YouTube upload (shown after processing completes)
    m_uploadBtn = new QPushButton(QString::fromUtf8("\u25B2 Upload to YouTube"));
    m_uploadBtn->setStyleSheet("QPushButton { background: #DF9E2F; color: #ffffff; border: none; border-radius: 4px; padding: 8px 16px; font-size: 13px; } QPushButton:hover { background: #E8B84A; }");
    m_uploadBtn->hide();
    layout->addWidget(m_uploadBtn);

    m_uploadBar = new QProgressBar;
    m_uploadBar->setStyleSheet("QProgressBar { background: #2d2d44; border: none; border-radius: 4px; height: 14px; color: #e8e8ec; text-align: center; } QProgressBar::chunk { background: #DF9E2F; border-radius: 4px; }");
    m_uploadBar->hide();
    layout->addWidget(m_uploadBar);

    m_ytLinkLabel = new QLabel;
    m_ytLinkLabel->setStyleSheet("QLabel { color: #569FC6; font-size: 12px; }");
    m_ytLinkLabel->setOpenExternalLinks(true);
    m_ytLinkLabel->setTextFormat(Qt::RichText);
    m_ytLinkLabel->hide();
    layout->addWidget(m_ytLinkLabel);

    auto *yt = new YouTube(this);

    connect(m_uploadBtn, &QPushButton::clicked, this, [this, yt]() {
        if (!m_recorder) return;
        // Find best output file
        QString dir = m_recorder->outputDir();
        QString video;
        QDir d(dir);
        for (const auto &f : d.entryInfoList({"*.mp4"}, QDir::Files)) {
            QString n = f.fileName();
            if (!n.startsWith("screen_") && !n.startsWith("webcam_") && !n.startsWith("audio")) {
                video = f.absoluteFilePath();
                break;
            }
        }
        if (video.isEmpty()) return;

        // Read title from recording.json
        QString title = "Recording";
        QString description;
        QFile jsonFile(dir + "/recording.json");
        if (jsonFile.open(QIODevice::ReadOnly)) {
            auto root = QJsonDocument::fromJson(jsonFile.readAll()).object();
            jsonFile.close();
            auto meta = root["metadata"].toObject();
            if (!meta["title"].toString().isEmpty()) title = meta["title"].toString();
            description = meta["description"].toString();
        }

        UploadOptions opts;
        opts.videoPath = video;
        opts.title = title;
        opts.description = description;
        opts.privacy = "unlisted";

        m_uploadBtn->setEnabled(false);
        m_uploadBtn->setText("Uploading...");
        m_uploadBar->setValue(0);
        m_uploadBar->show();

        yt->uploadVideo(opts);
    });

    connect(yt, &YouTube::uploadProgress, this, [this](int pct) {
        m_uploadBar->setValue(pct);
    });

    connect(yt, &YouTube::uploadFinished, this, [this](const QString &videoId, const QString &videoUrl) {
        m_uploadBar->hide();
        m_uploadBtn->hide();
        m_ytLinkLabel->setText(QString(R"(<a href="%1" style="color:#569FC6;">View on YouTube: %2</a>)")
            .arg(videoUrl, videoId));
        m_ytLinkLabel->show();

        // Save to recording.json
        if (m_recorder) {
            QString jsonPath = m_recorder->outputDir() + "/recording.json";
            QJsonObject root;
            QFile file(jsonPath);
            if (file.open(QIODevice::ReadOnly)) {
                root = QJsonDocument::fromJson(file.readAll()).object();
                file.close();
            }
            QJsonObject ytMeta;
            ytMeta["video_id"] = videoId;
            ytMeta["video_url"] = videoUrl;
            ytMeta["uploaded_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            root["youtube"] = ytMeta;
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QJsonDocument(root).toJson());
                file.close();
            }
        }
    });

    connect(yt, &YouTube::uploadError, this, [this](const QString &) {
        m_uploadBar->hide();
        m_uploadBtn->setText("Upload to YouTube");
        m_uploadBtn->setEnabled(true);
    });

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
    QString barStyle = "QProgressBar { background: #2d2d44; border: none; border-radius: 4px; height: 18px; color: #e8e8ec; text-align: center; } QProgressBar::chunk { background: #569FC6; border-radius: 4px; }";
    for (auto *bar : m_bars) { bar->setValue(0); bar->setStyleSheet(barStyle); }
    for (auto *lbl : m_statusLabels) {
        lbl->setText("Pending");
        lbl->setStyleSheet("QLabel { color: #8A8B8B; font-size: 11px; }");
    }
    for (auto *lbl : m_errorLabels) {
        lbl->clear();
        lbl->hide();
    }
    m_summaryLabel->hide();
    m_cancelBtn->show();
    m_backBtn->hide();
    m_openFolderBtn->hide();
    m_uploadBtn->hide();
    m_uploadBar->hide();
    m_ytLinkLabel->hide();

    m_elapsed.start();
    m_elapsedTimer->start(1000);

    // Room noise capture status
    connect(recorder, &Recorder::roomNoiseStarted, this, [this]() {
        m_elapsedLabel->setText("Capturing room noise - Please keep quiet!");
        m_elapsedLabel->setStyleSheet("QLabel { color: #DF9E2F; font-size: 12px; font-weight: bold; padding-top: 10px; }");
    });
    connect(recorder, &Recorder::roomNoiseProgress, this, [this](int secs) {
        m_elapsedLabel->setText(QString("Room noise capture: %1s remaining").arg(secs));
    });
    connect(recorder, &Recorder::roomNoiseFinished, this, [this]() {
        m_elapsedLabel->setStyleSheet("QLabel { color: #8A8B8B; font-size: 12px; padding-top: 10px; }");
        m_elapsed.start();
    });

    // Processing progress
    connect(recorder, &Recorder::processingProgress, this, [this](int step, int percent, const QString &) {
        if (step >= 0 && step < m_bars.size()) {
            m_bars[step]->setValue(percent);
            m_statusLabels[step]->setText(QString("%1%").arg(percent));
            m_statusLabels[step]->setStyleSheet("QLabel { color: #569FC6; font-size: 11px; }");
        }
    });

    // Step completed
    connect(recorder, &Recorder::processingStepDone, this, [this](int step, const QString &, bool skipped) {
        if (step >= 0 && step < m_bars.size()) {
            m_bars[step]->setValue(100);
            if (skipped) {
                m_statusLabels[step]->setText("Skipped");
                m_statusLabels[step]->setStyleSheet("QLabel { color: #8A8B8B; font-size: 11px; }");
            } else {
                m_statusLabels[step]->setText("Done");
                m_statusLabels[step]->setStyleSheet("QLabel { color: #06969A; font-size: 11px; font-weight: bold; }");
            }
        }
    });

    // Step error
    connect(recorder, &Recorder::processingStepError, this, [this](int step, const QString &, const QString &error) {
        if (step >= 0 && step < m_bars.size()) {
            m_bars[step]->setStyleSheet("QProgressBar { background: #2d2d44; border: none; border-radius: 4px; height: 18px; color: #e8e8ec; text-align: center; } QProgressBar::chunk { background: #CC0403; border-radius: 4px; }");
            m_statusLabels[step]->setText("Failed");
            m_statusLabels[step]->setStyleSheet("QLabel { color: #CC0403; font-size: 11px; font-weight: bold; }");
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
            m_summaryLabel->setStyleSheet("QLabel { color: #06969A; font-size: 14px; font-weight: bold; padding-top: 10px; }");
        } else {
            m_summaryLabel->setText("Processing finished with errors");
            m_summaryLabel->setStyleSheet("QLabel { color: #CC0403; font-size: 14px; font-weight: bold; padding-top: 10px; }");
        }
        m_summaryLabel->show();
        m_backBtn->show();
        m_openFolderBtn->show();

        // Show upload button if YouTube is configured
        YouTube ytCheck;
        if (success && ytCheck.hasToken()) {
            m_uploadBtn->setText("Upload to YouTube");
            m_uploadBtn->setEnabled(true);
            m_uploadBtn->show();
        }

        emit processingDone(success);
    });
}
