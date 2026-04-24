#include "gui/historypage.h"
#include "config/config.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QUrl>
#include <algorithm>

HistoryPage::HistoryPage(QWidget *parent) : QWidget(parent) {
    setupUI();
    loadRecordings();
}

void HistoryPage::setupUI() {
    auto *layout = new QHBoxLayout(this);
    layout->setSpacing(15);
    layout->setContentsMargins(15, 15, 15, 15);

    // === Left: recording list ===
    auto *leftPanel = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setSpacing(8);

    auto *title = new QLabel("Recording History");
    title->setStyleSheet("QLabel { color: #cdd6f4; font-size: 18px; font-weight: bold; }");
    leftLayout->addWidget(title);

    m_searchInput = new QLineEdit;
    m_searchInput->setPlaceholderText("Search recordings...");
    m_searchInput->setStyleSheet("QLineEdit { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 6px; }");
    connect(m_searchInput, &QLineEdit::textChanged, this, &HistoryPage::onSearchChanged);
    leftLayout->addWidget(m_searchInput);

    m_list = new QListWidget;
    m_list->setStyleSheet(R"(
        QListWidget { background: #1e1e2e; color: #cdd6f4; border: 1px solid #313244; border-radius: 4px; font-size: 13px; }
        QListWidget::item { padding: 8px 10px; border-bottom: 1px solid #313244; }
        QListWidget::item:selected { background: #45475a; }
        QListWidget::item:hover { background: #313244; }
    )");
    connect(m_list, &QListWidget::currentRowChanged, this, &HistoryPage::onRecordingSelected);
    leftLayout->addWidget(m_list);

    auto *refreshBtn = new QPushButton("Refresh");
    refreshBtn->setStyleSheet("QPushButton { background: #45475a; color: #cdd6f4; border: none; border-radius: 4px; padding: 6px; } QPushButton:hover { background: #585b70; }");
    connect(refreshBtn, &QPushButton::clicked, this, &HistoryPage::refresh);
    leftLayout->addWidget(refreshBtn);

    layout->addWidget(leftPanel);

    // === Right: player + details ===
    auto *rightPanel = new QWidget;
    rightPanel->setFixedWidth(420);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setSpacing(8);

    auto *detailsTitle = new QLabel("Details");
    detailsTitle->setStyleSheet("QLabel { color: #cdd6f4; font-size: 16px; font-weight: bold; }");
    rightLayout->addWidget(detailsTitle);

    // Inline video player
    m_videoWidget = new QVideoWidget;
    m_videoWidget->setMinimumSize(400, 225);
    m_videoWidget->setStyleSheet("background: #000000; border-radius: 8px;");
    rightLayout->addWidget(m_videoWidget);

    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoOutput(m_videoWidget);

    // Playback controls
    auto *controlsRow = new QHBoxLayout;
    controlsRow->setSpacing(5);

    m_playBtn = new QPushButton("Play");
    m_playBtn->setStyleSheet("QPushButton { background: #a6e3a1; color: #1e1e2e; border: none; border-radius: 4px; padding: 6px 16px; font-weight: bold; } QPushButton:hover { background: #94e2d5; } QPushButton:disabled { background: #45475a; color: #6c7086; }");
    m_playBtn->setEnabled(false);
    connect(m_playBtn, &QPushButton::clicked, this, &HistoryPage::onPlayClicked);
    controlsRow->addWidget(m_playBtn);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setStyleSheet("QPushButton { background: #f38ba8; color: #1e1e2e; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold; } QPushButton:hover { background: #eba0ac; }");
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &HistoryPage::onStopPlayback);
    controlsRow->addWidget(m_stopBtn);

    m_timeLabel = new QLabel("00:00 / 00:00");
    m_timeLabel->setStyleSheet("QLabel { color: #6c7086; font-size: 11px; }");
    controlsRow->addWidget(m_timeLabel);

    rightLayout->addLayout(controlsRow);

    // Seek slider
    m_seekSlider = new QSlider(Qt::Horizontal);
    m_seekSlider->setStyleSheet(R"(
        QSlider::groove:horizontal { background: #313244; height: 6px; border-radius: 3px; }
        QSlider::handle:horizontal { background: #89b4fa; width: 12px; height: 12px; margin: -3px 0; border-radius: 6px; }
        QSlider::sub-page:horizontal { background: #89b4fa; border-radius: 3px; }
    )");
    connect(m_seekSlider, &QSlider::sliderMoved, this, [this](int pos) {
        m_player->setPosition(pos);
    });
    rightLayout->addWidget(m_seekSlider);

    // Connect player signals
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        if (!m_seekSlider->isSliderDown()) {
            m_seekSlider->setValue(pos);
        }
        qint64 dur = m_player->duration();
        auto fmt = [](qint64 ms) -> QString {
            int s = ms / 1000;
            int m = s / 60; s %= 60;
            int h = m / 60; m %= 60;
            if (h > 0) return QString("%1:%2:%3").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
            return QString("%1:%2").arg(m).arg(s,2,10,QChar('0'));
        };
        m_timeLabel->setText(fmt(pos) + " / " + fmt(dur));
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
        m_seekSlider->setMaximum(dur);
    });

    // Metadata
    QString dimStyle = "QLabel { color: #6c7086; font-size: 12px; }";

    m_titleLabel = new QLabel("Select a recording");
    m_titleLabel->setStyleSheet("QLabel { color: #cdd6f4; font-size: 14px; font-weight: bold; }");
    m_titleLabel->setWordWrap(true);
    rightLayout->addWidget(m_titleLabel);

    m_statusLabel = new QLabel;
    m_statusLabel->setStyleSheet(dimStyle);
    rightLayout->addWidget(m_statusLabel);

    m_durationLabel = new QLabel;
    m_durationLabel->setStyleSheet(dimStyle);
    rightLayout->addWidget(m_durationLabel);

    m_filesLabel = new QLabel;
    m_filesLabel->setStyleSheet(dimStyle);
    m_filesLabel->setWordWrap(true);
    rightLayout->addWidget(m_filesLabel);

    m_sizeLabel = new QLabel;
    m_sizeLabel->setStyleSheet(dimStyle);
    rightLayout->addWidget(m_sizeLabel);

    // Action buttons
    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(5);

    m_deleteBtn = new QPushButton("Delete");
    m_deleteBtn->setStyleSheet("QPushButton { background: #f38ba8; color: #1e1e2e; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold; } QPushButton:hover { background: #eba0ac; } QPushButton:disabled { background: #313244; color: #6c7086; }");
    m_deleteBtn->setEnabled(false);
    connect(m_deleteBtn, &QPushButton::clicked, this, &HistoryPage::onDeleteClicked);
    actionRow->addWidget(m_deleteBtn);

    actionRow->addStretch();
    rightLayout->addLayout(actionRow);

    rightLayout->addStretch();
    layout->addWidget(rightPanel);
}

void HistoryPage::loadRecordings() {
    m_recordings.clear();
    m_list->clear();

    auto &cfg = Config::instance();
    QString videosDir = cfg.outputDir;
    if (videosDir.isEmpty()) {
        videosDir = QDir::homePath() + "/Videos/Screencasts";
    }

    QDir dir(videosDir);
    if (!dir.exists()) return;

    for (const auto &entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QString folder = entry.absoluteFilePath();
        QString jsonPath = folder + "/recording.json";

        RecordingEntry rec;
        rec.folder = folder;
        rec.dirName = entry.fileName();

        // Try to load recording.json
        QFile jsonFile(jsonPath);
        if (jsonFile.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll());
            jsonFile.close();
            QJsonObject root = doc.object();

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

            QString startTime = root["start_time"].toString();
            if (!startTime.isEmpty()) {
                QDateTime dt = QDateTime::fromString(startTime, Qt::ISODate);
                rec.date = dt.toString("yyyy-MM-dd HH:mm");
            }
        }

        // If no recording.json, scan for files directly
        if (rec.title.isEmpty()) rec.title = rec.dirName;
        if (rec.date.isEmpty()) rec.date = entry.fileName().mid(4, 19).replace('_', ' ');

        // Find merged/screen files by scanning directory
        if (rec.mergedFile.isEmpty() || !QFile::exists(rec.mergedFile)) {
            QDir recDir(folder);
            for (const auto &f : recDir.entryInfoList({"*.mp4"}, QDir::Files)) {
                QString name = f.fileName();
                if (!name.startsWith("screen_") && !name.startsWith("webcam_")) {
                    rec.mergedFile = f.absoluteFilePath();
                    break;
                }
            }
        }
        if (rec.screenFile.isEmpty() || !QFile::exists(rec.screenFile)) {
            QDir recDir(folder);
            for (const auto &f : recDir.entryInfoList({"screen_*.mp4"}, QDir::Files)) {
                rec.screenFile = f.absoluteFilePath();
                break;
            }
        }

        // Calculate total size if not in JSON
        if (rec.totalSize == 0) {
            QDir recDir(folder);
            for (const auto &f : recDir.entryInfoList(QDir::Files)) {
                rec.totalSize += f.size();
            }
        }

        m_recordings.append(rec);
    }

    // Sort newest first
    std::sort(m_recordings.begin(), m_recordings.end(), [](const RecordingEntry &a, const RecordingEntry &b) {
        return a.date > b.date;
    });

    // Populate list
    for (const auto &rec : m_recordings) {
        QString display = rec.title + "\n" + rec.date;
        if (rec.duration > 0) {
            int secs = rec.duration / 1000000000;
            int m = secs / 60, s = secs % 60;
            display += QString(" | %1:%2").arg(m).arg(s, 2, 10, QChar('0'));
        }
        if (!rec.status.isEmpty()) {
            display += " | " + rec.status;
        }
        m_list->addItem(display);
    }
}

void HistoryPage::refresh() {
    if (m_playing) onStopPlayback();
    loadRecordings();
    m_titleLabel->setText("Select a recording");
    m_statusLabel->clear();
    m_durationLabel->clear();
    m_filesLabel->clear();
    m_sizeLabel->clear();
    m_playBtn->setEnabled(false);
    m_stopBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);
}

void HistoryPage::onRecordingSelected(int row) {
    if (row < 0 || row >= m_recordings.size()) return;

    if (m_playing) onStopPlayback();

    const auto &rec = m_recordings[row];

    m_titleLabel->setText(rec.title);

    // Status
    if (rec.status == "completed") {
        m_statusLabel->setText("Status: Completed");
        m_statusLabel->setStyleSheet("QLabel { color: #a6e3a1; font-size: 12px; }");
    } else if (rec.status == "failed") {
        m_statusLabel->setText("Status: Failed");
        m_statusLabel->setStyleSheet("QLabel { color: #f38ba8; font-size: 12px; }");
    } else {
        m_statusLabel->setText("Status: " + rec.status);
        m_statusLabel->setStyleSheet("QLabel { color: #6c7086; font-size: 12px; }");
    }

    // Duration
    if (rec.duration > 0) {
        int secs = rec.duration / 1000000000;
        int h = secs / 3600, m = (secs / 60) % 60, s = secs % 60;
        if (h > 0)
            m_durationLabel->setText(QString("Duration: %1:%2:%3").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0')));
        else
            m_durationLabel->setText(QString("Duration: %1:%2").arg(m).arg(s,2,10,QChar('0')));
    } else {
        m_durationLabel->setText("Duration: unknown");
    }

    // Files
    QStringList files;
    if (!rec.mergedFile.isEmpty() && QFile::exists(rec.mergedFile)) files << "Merged";
    if (!rec.screenFile.isEmpty() && QFile::exists(rec.screenFile)) files << "Screen";
    if (!rec.audioFile.isEmpty() && QFile::exists(rec.audioFile)) files << "Audio";
    if (!rec.webcamFile.isEmpty() && QFile::exists(rec.webcamFile)) files << "Webcam";
    m_filesLabel->setText("Files: " + files.join(", "));

    // Size
    auto formatBytes = [](qint64 b) -> QString {
        if (b < 1024) return QString("%1 B").arg(b);
        if (b < 1024*1024) return QString("%1 KB").arg(b/1024);
        if (b < 1024*1024*1024) return QString("%1 MB").arg(b/(1024*1024));
        return QString("%1 GB").arg(b/(1024*1024*1024));
    };
    m_sizeLabel->setText("Size: " + formatBytes(rec.totalSize));

    // Enable buttons
    QString bestVideo = findBestVideo(rec);
    m_playBtn->setEnabled(!bestVideo.isEmpty());
    m_playBtn->setText("Play");
    m_deleteBtn->setEnabled(true);
}

QString HistoryPage::findBestVideo(const RecordingEntry &rec) {
    if (!rec.mergedFile.isEmpty() && QFile::exists(rec.mergedFile)) return rec.mergedFile;
    if (!rec.screenFile.isEmpty() && QFile::exists(rec.screenFile)) return rec.screenFile;
    if (!rec.webcamFile.isEmpty() && QFile::exists(rec.webcamFile)) return rec.webcamFile;
    return {};
}

void HistoryPage::onPlayClicked() {
    if (m_playing) {
        // Toggle pause
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
    m_playing = false;
    m_playBtn->setText("Play");
    m_stopBtn->setEnabled(false);
    m_seekSlider->setValue(0);
    m_timeLabel->setText("00:00 / 00:00");
}

void HistoryPage::onDeleteClicked() {
    int row = m_list->currentRow();
    if (row < 0 || row >= m_recordings.size()) return;

    const auto &rec = m_recordings[row];
    auto result = QMessageBox::question(this, "Delete Recording",
        QString("Delete recording \"%1\"?\n\nThis will permanently remove all files in:\n%2")
            .arg(rec.title, rec.folder));

    if (result == QMessageBox::Yes) {
        if (m_playing) onStopPlayback();
        QDir(rec.folder).removeRecursively();
        refresh();
    }
}

void HistoryPage::onSearchChanged(const QString &text) {
    QString query = text.toLower();
    for (int i = 0; i < m_list->count(); i++) {
        auto *item = m_list->item(i);
        item->setHidden(!item->text().toLower().contains(query));
    }
}
