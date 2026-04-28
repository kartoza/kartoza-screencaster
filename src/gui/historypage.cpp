#include "gui/historypage.h"
#include "config/config.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QUrl>
#include <QTimer>
#include <QProcess>
#include <QPixmap>
#include <QtConcurrent>
#include <algorithm>

HistoryPage::HistoryPage(QWidget *parent) : QWidget(parent) {
    setupUI();
    loadRecordings();
}

void HistoryPage::setupUI() {
    auto *layout = new QHBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(10, 10, 10, 10);

    // === Left panel: recording list ===
    auto *leftPanel = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setSpacing(6);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel("Recording History");
    title->setStyleSheet("QLabel { color: #cdd6f4; font-size: 16px; font-weight: bold; }");
    leftLayout->addWidget(title);

    m_searchInput = new QLineEdit;
    m_searchInput->setPlaceholderText("Search...");
    m_searchInput->setStyleSheet("QLineEdit { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 5px; }");
    connect(m_searchInput, &QLineEdit::textChanged, this, &HistoryPage::onSearchChanged);
    leftLayout->addWidget(m_searchInput);

    m_list = new QListWidget;
    m_list->setStyleSheet(R"(
        QListWidget { background: #1e1e2e; color: #cdd6f4; border: 1px solid #313244; border-radius: 4px; }
        QListWidget::item { padding: 6px 8px; border-bottom: 1px solid #313244; }
        QListWidget::item:selected { background: #45475a; }
        QListWidget::item:hover { background: #313244; }
    )");
    connect(m_list, &QListWidget::currentRowChanged, this, &HistoryPage::onRecordingSelected);
    leftLayout->addWidget(m_list);

    auto *refreshBtn = new QPushButton("Refresh");
    refreshBtn->setStyleSheet("QPushButton { background: #45475a; color: #cdd6f4; border: none; border-radius: 4px; padding: 6px; } QPushButton:hover { background: #585b70; }");
    connect(refreshBtn, &QPushButton::clicked, this, &HistoryPage::refresh);
    leftLayout->addWidget(refreshBtn);

    layout->addWidget(leftPanel, 1);

    // === Right panel: details + player ===
    auto *rightPanel = new QWidget;
    rightPanel->setFixedWidth(400);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setSpacing(6);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    // Video display — using QLabel + QVideoSink to avoid QVideoWidget Wayland positioning issues
    m_videoLabel = new QLabel;
    m_videoLabel->setFixedSize(400, 225);
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setScaledContents(true);
    m_videoLabel->setStyleSheet("background: #000; border-radius: 4px;");
    rightLayout->addWidget(m_videoLabel);

    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_videoSink = new QVideoSink(this);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoOutput(m_videoSink);

    // Render video frames to the QLabel
    connect(m_videoSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        QImage img = frame.toImage();
        if (!img.isNull()) {
            m_videoLabel->setPixmap(QPixmap::fromImage(img).scaled(
                m_videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    });

    // Debug: log media status changes
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [](QMediaPlayer::MediaStatus status) {
        qDebug() << "Player media status:" << status;
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [](QMediaPlayer::Error error, const QString &msg) {
        qDebug() << "Player error:" << error << msg;
    });

    // Controls
    auto *ctrlRow = new QHBoxLayout;
    ctrlRow->setSpacing(4);

    m_playBtn = new QPushButton("Play");
    m_playBtn->setFixedWidth(70);
    m_playBtn->setStyleSheet("QPushButton { background: #a6e3a1; color: #1e1e2e; border: none; border-radius: 4px; padding: 5px; font-weight: bold; } QPushButton:hover { background: #94e2d5; } QPushButton:disabled { background: #45475a; color: #6c7086; }");
    m_playBtn->setEnabled(false);
    connect(m_playBtn, &QPushButton::clicked, this, &HistoryPage::onPlayClicked);
    ctrlRow->addWidget(m_playBtn);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setFixedWidth(50);
    m_stopBtn->setStyleSheet("QPushButton { background: #f38ba8; color: #1e1e2e; border: none; border-radius: 4px; padding: 5px; font-weight: bold; } QPushButton:hover { background: #eba0ac; }");
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &HistoryPage::onStopPlayback);
    ctrlRow->addWidget(m_stopBtn);

    m_timeLabel = new QLabel("0:00 / 0:00");
    m_timeLabel->setStyleSheet("QLabel { color: #6c7086; font-size: 11px; }");
    ctrlRow->addWidget(m_timeLabel);
    ctrlRow->addStretch();

    rightLayout->addLayout(ctrlRow);

    m_seekSlider = new QSlider(Qt::Horizontal);
    m_seekSlider->setStyleSheet(R"(
        QSlider::groove:horizontal { background: #313244; height: 5px; border-radius: 2px; }
        QSlider::handle:horizontal { background: #89b4fa; width: 10px; height: 10px; margin: -3px 0; border-radius: 5px; }
        QSlider::sub-page:horizontal { background: #89b4fa; border-radius: 2px; }
    )");
    connect(m_seekSlider, &QSlider::sliderMoved, m_player, &QMediaPlayer::setPosition);
    rightLayout->addWidget(m_seekSlider);

    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        if (!m_seekSlider->isSliderDown()) m_seekSlider->setValue(pos);
        auto fmt = [](qint64 ms) {
            int s = ms/1000; int m = s/60; s %= 60;
            return QString("%1:%2").arg(m).arg(s,2,10,QChar('0'));
        };
        m_timeLabel->setText(fmt(pos) + " / " + fmt(m_player->duration()));
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 d) {
        m_seekSlider->setMaximum(d);
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::StoppedState && m_playing) {
            // Playback finished naturally
            m_playing = false;
            m_playBtn->setText("Play");
            m_stopBtn->setEnabled(false);
        }
    });

    // Metadata
    m_titleLabel = new QLabel("Select a recording");
    m_titleLabel->setStyleSheet("QLabel { color: #cdd6f4; font-size: 13px; font-weight: bold; }");
    m_titleLabel->setWordWrap(true);
    rightLayout->addWidget(m_titleLabel);

    QString dim = "QLabel { color: #6c7086; font-size: 11px; }";
    m_statusLabel = new QLabel; m_statusLabel->setStyleSheet(dim);
    m_durationLabel = new QLabel; m_durationLabel->setStyleSheet(dim);
    m_filesLabel = new QLabel; m_filesLabel->setStyleSheet(dim); m_filesLabel->setWordWrap(true);
    m_sizeLabel = new QLabel; m_sizeLabel->setStyleSheet(dim);
    rightLayout->addWidget(m_statusLabel);
    rightLayout->addWidget(m_durationLabel);
    rightLayout->addWidget(m_filesLabel);
    rightLayout->addWidget(m_sizeLabel);

    auto *openBtn = new QPushButton("Open in Player");
    openBtn->setStyleSheet("QPushButton { background: #89b4fa; color: #1e1e2e; border: none; border-radius: 4px; padding: 5px 10px; font-weight: bold; } QPushButton:hover { background: #74c7ec; } QPushButton:disabled { background: #45475a; color: #6c7086; }");
    openBtn->setEnabled(false);
    openBtn->setToolTip("Open in your system's default video player.");
    connect(openBtn, &QPushButton::clicked, this, [this]() {
        int row = m_list->currentRow();
        if (row < 0 || row >= m_recordings.size()) return;
        QString video = findBestVideo(m_recordings[row]);
        if (!video.isEmpty()) {
            QProcess::startDetached("xdg-open", {video});
        }
    });
    rightLayout->addWidget(openBtn);

    // Store openBtn reference for enabling/disabling
    connect(m_list, &QListWidget::currentRowChanged, openBtn, [this, openBtn](int row) {
        if (row >= 0 && row < m_recordings.size()) {
            openBtn->setEnabled(!findBestVideo(m_recordings[row]).isEmpty());
        } else {
            openBtn->setEnabled(false);
        }
    });

    auto *btnRow = new QHBoxLayout;

    auto *reprocessBtn = new QPushButton("Reprocess");
    reprocessBtn->setStyleSheet("QPushButton { background: #fab387; color: #1e1e2e; border: none; border-radius: 4px; padding: 5px 10px; font-weight: bold; } QPushButton:hover { background: #f9e2af; } QPushButton:disabled { background: #313244; color: #6c7086; }");
    reprocessBtn->setEnabled(false);
    reprocessBtn->setToolTip("Re-render merged and vertical video from source files.");
    connect(reprocessBtn, &QPushButton::clicked, this, [this]() {
        int row = m_list->currentRow();
        if (row < 0 || row >= m_recordings.size()) return;
        if (QMessageBox::question(this, "Reprocess Recording",
                QString("Reprocess \"%1\"?\n\nThis will re-render the merged and vertical video.")
                .arg(m_recordings[row].title)) == QMessageBox::Yes) {
            if (m_playing) onStopPlayback();
            emit reprocessRequested(m_recordings[row].folder);
        }
    });
    connect(m_list, &QListWidget::currentRowChanged, reprocessBtn, [this, reprocessBtn](int row) {
        reprocessBtn->setEnabled(row >= 0 && row < m_recordings.size());
    });
    btnRow->addWidget(reprocessBtn);

    m_deleteBtn = new QPushButton("Delete");
    m_deleteBtn->setStyleSheet("QPushButton { background: #f38ba8; color: #1e1e2e; border: none; border-radius: 4px; padding: 5px 10px; font-weight: bold; } QPushButton:hover { background: #eba0ac; } QPushButton:disabled { background: #313244; color: #6c7086; }");
    m_deleteBtn->setEnabled(false);
    connect(m_deleteBtn, &QPushButton::clicked, this, &HistoryPage::onDeleteClicked);
    btnRow->addWidget(m_deleteBtn);

    rightLayout->addLayout(btnRow);

    rightLayout->addStretch();
    layout->addWidget(rightPanel);
}

void HistoryPage::loadRecordings() {
    m_recordings.clear();
    m_list->clear();

    auto &cfg = Config::instance();
    QString videosDir = cfg.outputDir;
    if (videosDir.isEmpty()) videosDir = QDir::homePath() + "/Videos/Screencasts";

    QDir dir(videosDir);
    if (!dir.exists()) return;

    for (const auto &entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QString folder = entry.absoluteFilePath();
        RecordingEntry rec;
        rec.folder = folder;
        rec.dirName = entry.fileName();

        // Load recording.json
        QFile jsonFile(folder + "/recording.json");
        if (jsonFile.open(QIODevice::ReadOnly)) {
            QJsonObject root = QJsonDocument::fromJson(jsonFile.readAll()).object();
            jsonFile.close();

            auto meta = root["metadata"].toObject();
            rec.title = meta["title"].toString();
            rec.status = root["status"].toString();
            rec.duration = root["duration"].toVariant().toLongLong();

            auto files = root["files"].toObject();
            rec.mergedFile = files["merged_file"].toString();
            rec.screenFile = files["video_file"].toString();
            rec.audioFile = files["audio_file"].toString();
            rec.webcamFile = files["webcam_file"].toString();
            rec.totalSize = files["total_size"].toVariant().toLongLong();

            QString st = root["start_time"].toString();
            if (!st.isEmpty()) {
                rec.date = QDateTime::fromString(st, Qt::ISODate).toString("yyyy-MM-dd HH:mm");
            }
        }

        // Fallbacks
        if (rec.title.isEmpty()) rec.title = rec.dirName;
        if (rec.date.isEmpty()) {
            // Try to extract date from dir name (NNN-YYYY-MM-DD_HH-MM-SS)
            QString d = rec.dirName;
            if (d.length() > 4) d = d.mid(4);
            d.replace('_', ' ').replace('-', '-');
            rec.date = d;
        }

        // Find files by scanning if not in JSON
        QDir recDir(folder);
        if (rec.mergedFile.isEmpty() || !QFile::exists(rec.mergedFile)) {
            for (const auto &f : recDir.entryInfoList({"*.mp4"}, QDir::Files)) {
                if (!f.fileName().startsWith("screen_") && !f.fileName().startsWith("webcam_")) {
                    rec.mergedFile = f.absoluteFilePath();
                    break;
                }
            }
        }
        if (rec.screenFile.isEmpty() || !QFile::exists(rec.screenFile)) {
            for (const auto &f : recDir.entryInfoList({"screen_*.mp4"}, QDir::Files))
                { rec.screenFile = f.absoluteFilePath(); break; }
        }
        if (rec.totalSize == 0) {
            for (const auto &f : recDir.entryInfoList(QDir::Files))
                rec.totalSize += f.size();
        }

        m_recordings.append(rec);
    }

    // Sort newest first
    std::sort(m_recordings.begin(), m_recordings.end(), [](const RecordingEntry &a, const RecordingEntry &b) {
        return a.dirName > b.dirName;
    });

    for (const auto &rec : m_recordings) {
        QString durStr;
        if (rec.duration > 0) {
            int secs = rec.duration / 1000000000;
            durStr = QString(" | %1:%2").arg(secs/60).arg(secs%60, 2, 10, QChar('0'));
        }
        QString statusStr = rec.status.isEmpty() ? "" : " | " + rec.status;
        m_list->addItem(rec.title + "\n" + rec.date + durStr + statusStr);
    }
}

void HistoryPage::refresh() {
    if (m_playing) onStopPlayback();
    loadRecordings();
    m_titleLabel->setText("Select a recording");
    m_statusLabel->clear(); m_durationLabel->clear();
    m_filesLabel->clear(); m_sizeLabel->clear();
    m_playBtn->setEnabled(false); m_stopBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);
}

void HistoryPage::onRecordingSelected(int row) {
    if (row < 0 || row >= m_recordings.size()) return;
    if (m_playing) onStopPlayback();

    const auto &rec = m_recordings[row];
    m_titleLabel->setText(rec.title);

    if (rec.status == "completed")
        m_statusLabel->setText("Status: Completed"),
        m_statusLabel->setStyleSheet("QLabel { color: #a6e3a1; font-size: 11px; }");
    else if (rec.status == "failed")
        m_statusLabel->setText("Status: Failed"),
        m_statusLabel->setStyleSheet("QLabel { color: #f38ba8; font-size: 11px; }");
    else
        m_statusLabel->setText("Status: " + (rec.status.isEmpty() ? "unknown" : rec.status)),
        m_statusLabel->setStyleSheet("QLabel { color: #6c7086; font-size: 11px; }");

    if (rec.duration > 0) {
        int s = rec.duration / 1000000000;
        m_durationLabel->setText(QString("Duration: %1:%2").arg(s/60).arg(s%60,2,10,QChar('0')));
    } else {
        m_durationLabel->setText("Duration: unknown");
    }

    QStringList fl;
    if (!rec.mergedFile.isEmpty() && QFile::exists(rec.mergedFile)) fl << "Merged";
    if (!rec.screenFile.isEmpty() && QFile::exists(rec.screenFile)) fl << "Screen";
    if (!rec.audioFile.isEmpty() && QFile::exists(rec.audioFile)) fl << "Audio";
    m_filesLabel->setText("Files: " + (fl.isEmpty() ? "none" : fl.join(", ")));

    auto fmtBytes = [](qint64 b) {
        if (b < 1024) return QString("%1 B").arg(b);
        if (b < 1048576) return QString("%1 KB").arg(b/1024);
        if (b < 1073741824) return QString("%1 MB").arg(b/1048576);
        return QString("%1 GB").arg(b/1073741824);
    };
    m_sizeLabel->setText("Size: " + fmtBytes(rec.totalSize));

    QString video = findBestVideo(rec);
    m_playBtn->setEnabled(!video.isEmpty());
    m_playBtn->setText("Play");
    m_deleteBtn->setEnabled(true);

    // Extract thumbnail asynchronously
    if (!video.isEmpty()) {
        QString thumbPath = rec.folder + "/.thumbnail.jpg";
        QSize labelSize = m_videoLabel->size();
        QLabel *label = m_videoLabel;

        if (QFile::exists(thumbPath)) {
            QPixmap thumb(thumbPath);
            if (!thumb.isNull())
                label->setPixmap(thumb.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            QString videoPath = video;
            QtConcurrent::run([thumbPath, videoPath, label, labelSize]() {
                QProcess ffmpeg;
                ffmpeg.start("ffmpeg", {"-y", "-i", videoPath, "-vf", "thumbnail,scale=400:-1",
                                        "-frames:v", "1", thumbPath});
                ffmpeg.waitForFinished(10000);
                if (QFile::exists(thumbPath)) {
                    QPixmap thumb(thumbPath);
                    if (!thumb.isNull()) {
                        QMetaObject::invokeMethod(label, [label, thumb, labelSize]() {
                            label->setPixmap(thumb.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                        }, Qt::QueuedConnection);
                    }
                }
            });
        }
    }
}

QString HistoryPage::findBestVideo(const RecordingEntry &rec) {
    if (!rec.mergedFile.isEmpty() && QFile::exists(rec.mergedFile)) return rec.mergedFile;
    if (!rec.screenFile.isEmpty() && QFile::exists(rec.screenFile)) return rec.screenFile;
    return {};
}

void HistoryPage::onPlayClicked() {
    if (m_playing) {
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            m_player->pause();
            m_playBtn->setText("Play");
        } else {
            m_player->play();
            m_playBtn->setText("Pause");
        }
        return;
    }

    int row = m_list->currentRow();
    if (row < 0 || row >= m_recordings.size()) return;

    QString video = findBestVideo(m_recordings[row]);
    if (video.isEmpty()) return;

    m_player->setSource(QUrl::fromLocalFile(video));
    m_player->play();
    m_playing = true;
    m_playBtn->setText("Pause");
    m_stopBtn->setEnabled(true);
}

void HistoryPage::onStopPlayback() {
    m_player->stop();
    m_player->setSource(QUrl());
    m_playing = false;
    m_playBtn->setText("Play");
    m_stopBtn->setEnabled(false);
    m_seekSlider->setValue(0);
    m_timeLabel->setText("0:00 / 0:00");
}

void HistoryPage::onDeleteClicked() {
    int row = m_list->currentRow();
    if (row < 0 || row >= m_recordings.size()) return;
    const auto &rec = m_recordings[row];

    if (QMessageBox::question(this, "Delete Recording",
            QString("Delete \"%1\"?\n\nAll files in:\n%2").arg(rec.title, rec.folder))
            == QMessageBox::Yes) {
        if (m_playing) onStopPlayback();
        QDir(rec.folder).removeRecursively();
        refresh();
    }
}

void HistoryPage::onSearchChanged(const QString &text) {
    QString q = text.toLower();
    for (int i = 0; i < m_list->count(); i++)
        m_list->item(i)->setHidden(!m_list->item(i)->text().toLower().contains(q));
}
