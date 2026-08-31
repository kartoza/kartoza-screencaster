#include "gui/historypage.h"
#include "config/config.h"
#include "youtube/youtube.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QUrl>
#include <QProcess>
#include <QRegularExpression>
#include <QVideoFrame>
#include <algorithm>

// Unicode icons for the list
static const QString ICON_OK    = QString::fromUtf8("\u2714"); // check mark
static const QString ICON_WARN  = QString::fromUtf8("\u26A0"); // warning
static const QString ICON_FAIL  = QString::fromUtf8("\u2718"); // cross
static const QString ICON_YT    = QString::fromUtf8("\u25B6"); // play triangle (YouTube)
static const QString ICON_CAL   = QString::fromUtf8("\xF0\x9F\x93\x85"); // calendar
static const QString ICON_CLOCK = QString::fromUtf8("\xF0\x9F\x95\x92"); // clock

HistoryPage::HistoryPage(QWidget *parent) : QWidget(parent) {
    setupUI();
    loadRecordings();
}

void HistoryPage::setupUI() {
    auto *layout = new QHBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(10, 10, 10, 10);

    QString inputStyle = "QLineEdit { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; border-radius: 4px; padding: 5px; }";
    QString btnStyle = "QPushButton { background: #3d3d56; color: #e8e8ec; border: none; border-radius: 4px; padding: 5px 10px; } QPushButton:hover { background: #4d4d68; }";

    // === Left panel: recording list ===
    auto *leftPanel = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setSpacing(6);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel("Recording History");
    title->setStyleSheet("QLabel { color: #e8e8ec; font-size: 16px; font-weight: bold; }");
    leftLayout->addWidget(title);

    m_searchInput = new QLineEdit;
    m_searchInput->setPlaceholderText("Search...");
    m_searchInput->setStyleSheet(inputStyle);
    connect(m_searchInput, &QLineEdit::textChanged, this, &HistoryPage::onSearchChanged);
    leftLayout->addWidget(m_searchInput);

    m_tree = new QTreeWidget;
    m_tree->setHeaderLabels({"", "", "Date", "Duration", "Title"});
    m_tree->setColumnCount(5);
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents); // status
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents); // youtube
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents); // date
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents); // duration
    m_tree->setStyleSheet(R"(
        QTreeWidget { background: #1a1a2e; color: #e8e8ec; border: 1px solid #2d2d44; border-radius: 4px; font-size: 14px; }
        QTreeWidget::item { padding: 6px 4px; border-bottom: 1px solid #2d2d44; }
        QTreeWidget::item:selected { background: #3d3d56; }
        QTreeWidget::item:hover { background: #2d2d44; }
        QHeaderView::section { background: #141428; color: #8A8B8B; border: none; padding: 6px 8px; font-size: 13px; font-weight: bold; }
    )");
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this]() { onRecordingSelected(); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int col) {
        Q_UNUSED(col);
        if (!item) return;
        int row = m_tree->indexOfTopLevelItem(item);
        if (row >= 0 && row < m_recordings.size()) {
            // Double-click YouTube column opens URL
            if (!m_recordings[row].youtubeVideoUrl.isEmpty()) {
                QDesktopServices::openUrl(QUrl(m_recordings[row].youtubeVideoUrl));
            }
        }
    });
    leftLayout->addWidget(m_tree);

    auto *refreshBtn = new QPushButton(QString::fromUtf8("\u21BB Refresh"));
    refreshBtn->setStyleSheet(btnStyle);
    connect(refreshBtn, &QPushButton::clicked, this, &HistoryPage::refresh);
    leftLayout->addWidget(refreshBtn);

    layout->addWidget(leftPanel, 1);

    // === Right panel: details + player ===
    auto *rightPanel = new QWidget;
    rightPanel->setFixedWidth(420);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setSpacing(6);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    // Preview area: stacked thumbnail / video
    m_previewStack = new QStackedWidget;
    m_previewStack->setFixedSize(420, 236);

    m_thumbnailLabel = new QLabel;
    m_thumbnailLabel->setFixedSize(420, 236);
    m_thumbnailLabel->setAlignment(Qt::AlignCenter);
    m_thumbnailLabel->setStyleSheet("QLabel { background: #000; border-radius: 4px; }");
    m_thumbnailLabel->setText("No recording selected");
    m_thumbnailLabel->setStyleSheet("QLabel { background: #0f0f20; color: #8A8B8B; font-size: 12px; border-radius: 4px; }");
    m_previewStack->addWidget(m_thumbnailLabel); // index 0

    m_videoWidget = new QVideoWidget;
    m_videoWidget->setFixedSize(420, 236);
    m_previewStack->addWidget(m_videoWidget); // index 1

    rightLayout->addWidget(m_previewStack);

    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoOutput(m_videoWidget);

    // Grab frames for thumbnail via video sink
    connect(m_player->videoSink(), &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (frame.isValid()) {
            m_lastFrame = frame.toImage();
        }
    });

    // Scrubber row: play | stop | slider | time
    auto *scrubRow = new QHBoxLayout;
    scrubRow->setSpacing(4);

    m_playBtn = new QPushButton(QString::fromUtf8("\u25B6")); // play triangle
    m_playBtn->setFixedSize(28, 28);
    m_playBtn->setStyleSheet("QPushButton { background: #06969A; color: #ffffff; border: none; border-radius: 4px; font-size: 14px; } QPushButton:hover { background: #058084; } QPushButton:disabled { background: #3d3d56; color: #8A8B8B; }");
    m_playBtn->setEnabled(false);
    connect(m_playBtn, &QPushButton::clicked, this, &HistoryPage::onPlayClicked);
    scrubRow->addWidget(m_playBtn);

    m_stopBtn = new QPushButton(QString::fromUtf8("\u25A0")); // stop square
    m_stopBtn->setFixedSize(28, 28);
    m_stopBtn->setStyleSheet("QPushButton { background: #CC0403; color: #ffffff; border: none; border-radius: 4px; font-size: 14px; } QPushButton:hover { background: #E03030; }");
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &HistoryPage::onStopPlayback);
    scrubRow->addWidget(m_stopBtn);

    m_seekSlider = new QSlider(Qt::Horizontal);
    m_seekSlider->setStyleSheet(R"(
        QSlider::groove:horizontal { background: #2d2d44; height: 5px; border-radius: 2px; }
        QSlider::handle:horizontal { background: #569FC6; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }
        QSlider::sub-page:horizontal { background: #569FC6; border-radius: 2px; }
    )");
    connect(m_seekSlider, &QSlider::sliderMoved, m_player, &QMediaPlayer::setPosition);
    scrubRow->addWidget(m_seekSlider, 1);

    m_timeLabel = new QLabel("0:00 / 0:00");
    m_timeLabel->setStyleSheet("QLabel { color: #8A8B8B; font-size: 12px; }");
    scrubRow->addWidget(m_timeLabel);

    rightLayout->addLayout(scrubRow);

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
        if (state == QMediaPlayer::PausedState) {
            // Show last frame as thumbnail
            if (!m_lastFrame.isNull()) {
                m_thumbnailLabel->setPixmap(QPixmap::fromImage(m_lastFrame).scaled(
                    m_thumbnailLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                m_previewStack->setCurrentIndex(0);
            }
        } else if (state == QMediaPlayer::PlayingState) {
            m_previewStack->setCurrentIndex(1); // show video widget
        } else if (state == QMediaPlayer::StoppedState && m_playing) {
            m_playing = false;
            m_playBtn->setText(QString::fromUtf8("\u25B6"));
            m_stopBtn->setEnabled(false);
            if (!m_lastFrame.isNull()) {
                m_thumbnailLabel->setPixmap(QPixmap::fromImage(m_lastFrame).scaled(
                    m_thumbnailLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                m_previewStack->setCurrentIndex(0);
            }
        }
    });

    // Editable title
    m_titleInput = new QLineEdit("Select a recording");
    m_titleInput->setStyleSheet("QLineEdit { background: transparent; color: #e8e8ec; border: none; font-size: 18px; font-weight: bold; padding: 4px; } QLineEdit:focus { background: #2d2d44; border: 1px solid #569FC6; border-radius: 4px; }");
    m_titleInput->setToolTip("Edit to rename the recording. Press Enter to apply.");
    connect(m_titleInput, &QLineEdit::returnPressed, this, [this]() {
        m_titleInput->clearFocus();
    });
    connect(m_titleInput, &QLineEdit::editingFinished, this, [this]() {
        int row = -1;
        auto *item = m_tree->currentItem();
        if (item) row = m_tree->indexOfTopLevelItem(item);
        if (row >= 0 && row < m_recordings.size()) {
            QString newTitle = m_titleInput->text().trimmed();
            if (!newTitle.isEmpty() && newTitle != m_recordings[row].title) {
                renameRecording(row, newTitle);
            }
        }
    });
    rightLayout->addWidget(m_titleInput);

    QString dim = "QLabel { color: #8A8B8B; font-size: 13px; }";
    m_statusLabel = new QLabel; m_statusLabel->setStyleSheet(dim);
    m_durationLabel = new QLabel; m_durationLabel->setStyleSheet(dim);
    m_filesLabel = new QLabel; m_filesLabel->setStyleSheet(dim); m_filesLabel->setWordWrap(true);
    m_sizeLabel = new QLabel; m_sizeLabel->setStyleSheet(dim);
    rightLayout->addWidget(m_statusLabel);
    rightLayout->addWidget(m_durationLabel);
    rightLayout->addWidget(m_filesLabel);
    rightLayout->addWidget(m_sizeLabel);

    // Action buttons row
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(4);

    QString histBtnStyle = "QPushButton { background: #3d3d56; color: #e8e8ec; border: none; border-radius: 4px; padding: 5px 10px; font-size: 13px; } QPushButton:hover { background: #4d4d68; } QPushButton:disabled { background: #2d2d44; color: #8A8B8B; }";
    QString histDelStyle = "QPushButton { background: #CC0403; color: #ffffff; border: none; border-radius: 4px; padding: 5px 10px; font-size: 13px; } QPushButton:hover { background: #E03030; } QPushButton:disabled { background: #2d2d44; color: #8A8B8B; }";

    auto *openBtn = new QPushButton(QString::fromUtf8("\u25B6 Open"));
    openBtn->setStyleSheet("QPushButton { background: #569FC6; color: #ffffff; border: none; border-radius: 4px; padding: 5px 10px; font-size: 13px; } QPushButton:hover { background: #4889B0; } QPushButton:disabled { background: #3d3d56; color: #8A8B8B; }");
    openBtn->setEnabled(false);
    openBtn->setToolTip("Open in system video player");
    connect(openBtn, &QPushButton::clicked, this, [this]() {
        auto *item = m_tree->currentItem();
        if (!item) return;
        int row = m_tree->indexOfTopLevelItem(item);
        if (row < 0 || row >= m_recordings.size()) return;
        QString video = findBestVideo(m_recordings[row]);
        if (!video.isEmpty()) QProcess::startDetached("xdg-open", {video});
    });
    btnRow->addWidget(openBtn);

    auto *folderBtn = new QPushButton(QString::fromUtf8("\u2302 Folder"));
    folderBtn->setStyleSheet(histBtnStyle);
    folderBtn->setEnabled(false);
    folderBtn->setToolTip("Open recording folder");
    connect(folderBtn, &QPushButton::clicked, this, [this]() {
        auto *item = m_tree->currentItem();
        if (!item) return;
        int row = m_tree->indexOfTopLevelItem(item);
        if (row >= 0 && row < m_recordings.size())
            QProcess::startDetached("xdg-open", {m_recordings[row].folder});
    });
    btnRow->addWidget(folderBtn);

    auto *reprocessBtn = new QPushButton(QString::fromUtf8("\u21BB Reprocess"));
    reprocessBtn->setStyleSheet("QPushButton { background: #DF9E2F; color: #ffffff; border: none; border-radius: 4px; padding: 5px 10px; font-size: 13px; } QPushButton:hover { background: #E8B84A; } QPushButton:disabled { background: #2d2d44; color: #8A8B8B; }");
    reprocessBtn->setEnabled(false);
    connect(reprocessBtn, &QPushButton::clicked, this, [this]() {
        auto *item = m_tree->currentItem();
        if (!item) return;
        int row = m_tree->indexOfTopLevelItem(item);
        if (row < 0 || row >= m_recordings.size()) return;
        if (QMessageBox::question(this, "Reprocess",
                QString("Reprocess \"%1\"?").arg(m_recordings[row].title)) == QMessageBox::Yes) {
            if (m_playing) onStopPlayback();
            emit reprocessRequested(m_recordings[row].folder);
        }
    });
    btnRow->addWidget(reprocessBtn);

    btnRow->addStretch(); // push destructive actions to the right

    m_deleteBtn = new QPushButton(QString::fromUtf8("\u2716 Delete"));
    m_deleteBtn->setStyleSheet(histDelStyle);
    m_deleteBtn->setEnabled(false);
    connect(m_deleteBtn, &QPushButton::clicked, this, &HistoryPage::onDeleteClicked);
    btnRow->addWidget(m_deleteBtn);

    rightLayout->addLayout(btnRow);

    // Enable/disable buttons on selection
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [=](QTreeWidgetItem *cur) {
        int row = cur ? m_tree->indexOfTopLevelItem(cur) : -1;
        bool valid = row >= 0 && row < m_recordings.size();
        bool hasVideo = valid && !findBestVideo(m_recordings[row]).isEmpty();
        openBtn->setEnabled(hasVideo);
        folderBtn->setEnabled(valid);
        reprocessBtn->setEnabled(valid);
        m_deleteBtn->setEnabled(valid);
    });

    // --- YouTube ---
    m_ytLinkLabel = new QLabel;
    m_ytLinkLabel->setStyleSheet("QLabel { color: #569FC6; font-size: 13px; padding: 2px; }");
    m_ytLinkLabel->setOpenExternalLinks(true);
    m_ytLinkLabel->setTextFormat(Qt::RichText);
    m_ytLinkLabel->hide();
    rightLayout->addWidget(m_ytLinkLabel);

    m_uploadBtn = new QPushButton(QString::fromUtf8("\u25B2 Upload to YouTube"));
    m_uploadBtn->setStyleSheet("QPushButton { background: #DF9E2F; color: #ffffff; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold; } QPushButton:hover { background: #E8B84A; } QPushButton:disabled { background: #2d2d44; color: #8A8B8B; }");
    m_uploadBtn->hide();
    rightLayout->addWidget(m_uploadBtn);

    // Upload form (hidden until Upload button clicked)
    m_uploadForm = new QWidget;
    auto *formLayout = new QVBoxLayout(m_uploadForm);
    formLayout->setContentsMargins(0, 4, 0, 0);
    formLayout->setSpacing(4);

    auto formLabel = [](const QString &text) {
        auto *l = new QLabel(text);
        l->setStyleSheet("QLabel { color: #569FC6; font-size: 14px; font-weight: bold; }");
        return l;
    };

    formLayout->addWidget(formLabel("YouTube Upload"));

    m_ytTitleInput = new QLineEdit;
    m_ytTitleInput->setStyleSheet(inputStyle);
    m_ytTitleInput->setPlaceholderText("Video title...");
    formLayout->addWidget(m_ytTitleInput);

    m_ytDescInput = new QTextEdit;
    m_ytDescInput->setStyleSheet("QTextEdit { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; border-radius: 4px; padding: 4px; font-size: 13px; }");
    m_ytDescInput->setMaximumHeight(50);
    m_ytDescInput->setPlaceholderText("Description...");
    formLayout->addWidget(m_ytDescInput);

    m_ytTagsInput = new QLineEdit;
    m_ytTagsInput->setStyleSheet(inputStyle);
    m_ytTagsInput->setPlaceholderText("Tags (comma-separated)...");
    formLayout->addWidget(m_ytTagsInput);

    auto *privRow = new QHBoxLayout;
    auto *privLabel = new QLabel("Privacy:");
    privLabel->setStyleSheet("QLabel { color: #e8e8ec; font-size: 13px; }");
    privRow->addWidget(privLabel);
    m_ytPrivacyCombo = new QComboBox;
    m_ytPrivacyCombo->addItems({"unlisted", "public", "private"});
    m_ytPrivacyCombo->setStyleSheet("QComboBox { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; border-radius: 4px; padding: 4px 24px 4px 8px; font-size: 13px; } QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 20px; border: none; } QComboBox::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 6px solid #8A8B8B; margin-right: 6px; } QComboBox::down-arrow:hover { border-top-color: #e8e8ec; } QComboBox QAbstractItemView { background: #2d2d44; color: #e8e8ec; selection-background-color: #3d3d56; border: 1px solid #3d3d56; }");
    privRow->addWidget(m_ytPrivacyCombo);
    privRow->addStretch();
    formLayout->addLayout(privRow);

    auto *plRow = new QHBoxLayout;
    auto *plLabel = new QLabel("Playlist:");
    plLabel->setStyleSheet("QLabel { color: #e8e8ec; font-size: 13px; }");
    plRow->addWidget(plLabel);
    m_ytPlaylistCombo = new QComboBox;
    m_ytPlaylistCombo->setStyleSheet("QComboBox { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; border-radius: 4px; padding: 4px 24px 4px 8px; font-size: 13px; } QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 20px; border: none; } QComboBox::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 6px solid #8A8B8B; margin-right: 6px; } QComboBox::down-arrow:hover { border-top-color: #e8e8ec; } QComboBox QAbstractItemView { background: #2d2d44; color: #e8e8ec; selection-background-color: #3d3d56; border: 1px solid #3d3d56; }");
    m_ytPlaylistCombo->addItem("(none)", QString());
    plRow->addWidget(m_ytPlaylistCombo, 1);
    formLayout->addLayout(plRow);

    auto *uploadBtnRow = new QHBoxLayout;
    m_ytUploadBtn = new QPushButton(QString::fromUtf8("\u25B2 Upload"));
    m_ytUploadBtn->setStyleSheet("QPushButton { background: #06969A; color: #ffffff; border: none; border-radius: 4px; padding: 5px 12px; font-size: 13px; } QPushButton:hover { background: #058084; }");
    uploadBtnRow->addWidget(m_ytUploadBtn);
    m_ytCancelBtn = new QPushButton(QString::fromUtf8("\u2716 Cancel"));
    m_ytCancelBtn->setStyleSheet(histBtnStyle);
    uploadBtnRow->addWidget(m_ytCancelBtn);
    uploadBtnRow->addStretch();
    formLayout->addLayout(uploadBtnRow);

    m_uploadProgress = new QProgressBar;
    m_uploadProgress->setStyleSheet("QProgressBar { background: #2d2d44; border: none; border-radius: 4px; height: 14px; color: #e8e8ec; text-align: center; font-size: 12px; } QProgressBar::chunk { background: #06969A; border-radius: 4px; }");
    m_uploadProgress->hide();
    formLayout->addWidget(m_uploadProgress);

    m_uploadForm->hide();
    rightLayout->addWidget(m_uploadForm);

    // YouTube instance and wiring
    m_youtube = new YouTube(this);

    connect(m_uploadBtn, &QPushButton::clicked, this, &HistoryPage::showUploadForm);
    connect(m_ytCancelBtn, &QPushButton::clicked, this, &HistoryPage::hideUploadForm);

    connect(m_youtube, &YouTube::playlistsReady, this, [this](const QList<PlaylistInfo> &playlists) {
        // Preserve current selection
        m_ytPlaylistCombo->clear();
        m_ytPlaylistCombo->addItem("(none)", QString());
        auto &cfg = Config::instance();
        int selectIdx = 0;
        for (int i = 0; i < playlists.size(); i++) {
            m_ytPlaylistCombo->addItem(playlists[i].title, playlists[i].id);
            if (playlists[i].id == cfg.youtubeLastPlaylistId)
                selectIdx = i + 1; // +1 because of "(none)"
        }
        m_ytPlaylistCombo->setCurrentIndex(selectIdx);
    });

    connect(m_ytUploadBtn, &QPushButton::clicked, this, [this]() {
        auto *item = m_tree->currentItem();
        if (!item) return;
        int row = m_tree->indexOfTopLevelItem(item);
        if (row < 0 || row >= m_recordings.size()) return;
        QString video = findBestVideo(m_recordings[row]);
        if (video.isEmpty()) return;

        UploadOptions opts;
        opts.videoPath = video;
        opts.title = m_ytTitleInput->text().trimmed();
        opts.description = m_ytDescInput->toPlainText();
        opts.privacy = m_ytPrivacyCombo->currentText();
        opts.playlistId = m_ytPlaylistCombo->currentData().toString();
        QString tagsStr = m_ytTagsInput->text().trimmed();
        if (!tagsStr.isEmpty()) {
            for (const auto &t : tagsStr.split(","))
                if (!t.trimmed().isEmpty()) opts.tags.append(t.trimmed());
        }

        m_ytUploadBtn->setEnabled(false);
        m_ytUploadBtn->setText("Uploading...");
        m_uploadProgress->setValue(0);
        m_uploadProgress->show();

        m_youtube->uploadVideo(opts);
    });

    connect(m_youtube, &YouTube::uploadProgress, this, [this](int pct) {
        m_uploadProgress->setValue(pct);
    });

    connect(m_youtube, &YouTube::uploadFinished, this, [this](const QString &videoId, const QString &videoUrl) {
        m_uploadProgress->hide();

        // Save last-used playlist
        auto &cfg = Config::instance();
        cfg.youtubeLastPlaylistId = m_ytPlaylistCombo->currentData().toString();
        cfg.youtubeLastPlaylistName = m_ytPlaylistCombo->currentText();
        cfg.save();

        hideUploadForm();

        auto *item = m_tree->currentItem();
        int row = item ? m_tree->indexOfTopLevelItem(item) : -1;
        if (row >= 0 && row < m_recordings.size()) {
            m_recordings[row].youtubeVideoId = videoId;
            m_recordings[row].youtubeVideoUrl = videoUrl;
            saveYouTubeMetadata(m_recordings[row].folder, videoId, videoUrl);
            // Update list icon
            item->setText(1, ICON_YT);
            item->setForeground(1, QBrush(QColor("#CC0403")));
            item->setToolTip(1, videoUrl);
        }

        m_ytLinkLabel->setText(QString(R"(<a href="%1" style="color:#569FC6;">View on YouTube: %2</a>)")
            .arg(videoUrl, videoId));
        m_ytLinkLabel->show();
        m_uploadBtn->hide();
    });

    connect(m_youtube, &YouTube::uploadError, this, [this](const QString &err) {
        m_uploadProgress->hide();
        m_ytUploadBtn->setText("Upload");
        m_ytUploadBtn->setEnabled(true);
        QMessageBox::warning(this, "YouTube Upload Error", err);
    });

    // Show/hide upload button on selection
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *cur) {
        hideUploadForm();
        int row = cur ? m_tree->indexOfTopLevelItem(cur) : -1;
        if (row >= 0 && row < m_recordings.size()) {
            bool hasVideo = !findBestVideo(m_recordings[row]).isEmpty();
            bool uploaded = !m_recordings[row].youtubeVideoId.isEmpty();
            m_uploadBtn->setVisible(hasVideo && !uploaded && m_youtube->hasToken());
            if (uploaded) {
                m_ytLinkLabel->setText(QString(R"(<a href="%1" style="color:#569FC6;">View on YouTube: %2</a>)")
                    .arg(m_recordings[row].youtubeVideoUrl, m_recordings[row].youtubeVideoId));
                m_ytLinkLabel->show();
            } else {
                m_ytLinkLabel->hide();
            }
        } else {
            m_uploadBtn->hide();
            m_ytLinkLabel->hide();
        }
    });

    rightLayout->addStretch();
    layout->addWidget(rightPanel);
}

void HistoryPage::showUploadForm() {
    auto *item = m_tree->currentItem();
    int row = item ? m_tree->indexOfTopLevelItem(item) : -1;
    if (row < 0 || row >= m_recordings.size()) return;

    const auto &rec = m_recordings[row];
    m_ytTitleInput->setText(rec.title);
    m_ytDescInput->setPlainText(rec.description);
    m_ytTagsInput->clear();
    m_ytPrivacyCombo->setCurrentText("unlisted");
    m_ytUploadBtn->setText("Upload");
    m_ytUploadBtn->setEnabled(true);
    m_uploadProgress->hide();

    // Fetch playlists and populate combo
    m_ytPlaylistCombo->clear();
    m_ytPlaylistCombo->addItem("(none)", QString());
    m_youtube->fetchPlaylists();

    m_uploadBtn->hide();
    m_uploadForm->show();
}

void HistoryPage::hideUploadForm() {
    m_uploadForm->hide();
    m_ytUploadBtn->setText("Upload");
    m_ytUploadBtn->setEnabled(true);
    m_uploadProgress->hide();
}

void HistoryPage::updateThumbnail(const QString &videoPath) {
    if (videoPath.isEmpty()) {
        m_thumbnailLabel->setPixmap(QPixmap());
        m_thumbnailLabel->setText("No video");
        m_previewStack->setCurrentIndex(0);
        return;
    }

    // Extract thumbnail using ffmpeg (async)
    QString thumbPath = QDir::tempPath() + "/ks-thumb-" +
        QString::number(qHash(videoPath)) + ".jpg";

    auto *proc = new QProcess(this);
    proc->start("ffmpeg", {"-y", "-ss", "2", "-i", videoPath,
                           "-vframes", "1", "-q:v", "2", thumbPath});
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, thumbPath]() {
        proc->deleteLater();
        QPixmap pix(thumbPath);
        if (!pix.isNull()) {
            m_thumbnailLabel->setPixmap(pix.scaled(
                m_thumbnailLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_thumbnailLabel->setText("");
        }
        m_previewStack->setCurrentIndex(0);
    });
}

void HistoryPage::renameRecording(int row, const QString &newTitle) {
    if (row < 0 || row >= m_recordings.size()) return;
    auto &rec = m_recordings[row];

    // Update recording.json
    QString jsonPath = rec.folder + "/recording.json";
    QJsonObject root;
    QFile jf(jsonPath);
    if (jf.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(jf.readAll()).object();
        jf.close();
    }
    auto meta = root["metadata"].toObject();
    meta["title"] = newTitle;
    root["metadata"] = meta;
    if (jf.open(QIODevice::WriteOnly)) {
        jf.write(QJsonDocument(root).toJson());
        jf.close();
    }

    // Build new folder name: preserve NNN- prefix, replace slug
    QString oldDir = rec.dirName;
    QRegularExpression re("^(\\d{3})-");
    auto match = re.match(oldDir);
    QString prefix = match.hasMatch() ? match.captured(0) : "";
    QString slug = newTitle.toLower().replace(QRegularExpression("[^a-z0-9]+"), "_");
    if (slug.endsWith('_')) slug.chop(1);
    if (slug.startsWith('_')) slug.remove(0, 1);
    QString newDirName = prefix + slug;

    if (newDirName != oldDir) {
        QDir parent(QFileInfo(rec.folder).absolutePath());
        if (parent.rename(oldDir, newDirName)) {
            rec.folder = parent.absoluteFilePath(newDirName);
            rec.dirName = newDirName;
        }
    }

    rec.title = newTitle;

    // Update tree item
    auto *item = m_tree->topLevelItem(row);
    if (item) item->setText(4, newTitle);
}

void HistoryPage::loadRecordings() {
    m_recordings.clear();
    m_tree->clear();

    QDir dir(Config::instance().recordingsDir());
    if (!dir.exists()) return;

    for (const auto &entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QString folder = entry.absoluteFilePath();
        RecordingEntry rec;
        rec.folder = folder;
        rec.dirName = entry.fileName();

        QFile jsonFile(folder + "/recording.json");
        if (jsonFile.open(QIODevice::ReadOnly)) {
            QJsonObject root = QJsonDocument::fromJson(jsonFile.readAll()).object();
            jsonFile.close();

            auto meta = root["metadata"].toObject();
            rec.title = meta["title"].toString();
            rec.status = root["status"].toString();
            rec.duration = root["duration"].toVariant().toLongLong();
            rec.description = meta["description"].toString();

            auto files = root["files"].toObject();
            rec.mergedFile = files["merged_file"].toString();
            rec.verticalFile = files["vertical_file"].toString();
            rec.screenFile = files["video_file"].toString();
            rec.audioFile = files["audio_file"].toString();
            rec.webcamFile = files["webcam_file"].toString();
            rec.totalSize = files["total_size"].toVariant().toLongLong();

            auto yt = root["youtube"].toObject();
            rec.youtubeVideoId = yt["video_id"].toString();
            rec.youtubeVideoUrl = yt["video_url"].toString();

            QString st = root["start_time"].toString();
            if (!st.isEmpty())
                rec.date = QDateTime::fromString(st, Qt::ISODate).toString("yyyy-MM-dd HH:mm");
        }

        if (rec.title.isEmpty()) rec.title = rec.dirName;
        if (rec.date.isEmpty()) {
            QString d = rec.dirName;
            if (d.length() > 4) d = d.mid(4);
            d.replace('_', ' ');
            rec.date = d;
        }

        // Find files by scanning if not in JSON
        QDir recDir(folder);
        if (rec.mergedFile.isEmpty() || !QFile::exists(rec.mergedFile)) {
            for (const auto &f : recDir.entryInfoList({"*.mp4"}, QDir::Files)) {
                QString name = f.fileName();
                if (!name.startsWith("screen_") && !name.startsWith("webcam_") && !name.contains("-vertical")) {
                    rec.mergedFile = f.absoluteFilePath();
                    break;
                }
            }
        }
        if (rec.verticalFile.isEmpty() || !QFile::exists(rec.verticalFile)) {
            for (const auto &f : recDir.entryInfoList({"*-vertical.mp4"}, QDir::Files))
                { rec.verticalFile = f.absoluteFilePath(); break; }
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

    std::sort(m_recordings.begin(), m_recordings.end(), [](const RecordingEntry &a, const RecordingEntry &b) {
        return a.dirName > b.dirName;
    });

    for (const auto &rec : m_recordings) {
        auto *item = new QTreeWidgetItem;

        // Col 0: Status icon
        if (rec.status == "completed") {
            item->setText(0, ICON_OK);
            item->setForeground(0, QBrush(QColor("#06969A")));
            item->setToolTip(0, "Completed");
        } else if (rec.status == "failed") {
            item->setText(0, ICON_FAIL);
            item->setForeground(0, QBrush(QColor("#CC0403")));
            item->setToolTip(0, "Failed");
        } else {
            item->setText(0, ICON_WARN);
            item->setForeground(0, QBrush(QColor("#DF9E2F")));
            item->setToolTip(0, rec.status.isEmpty() ? "Unknown" : rec.status);
        }

        // Col 1: YouTube icon
        if (!rec.youtubeVideoId.isEmpty()) {
            item->setText(1, ICON_YT);
            item->setForeground(1, QBrush(QColor("#CC0403")));
            item->setToolTip(1, rec.youtubeVideoUrl);
        }

        // Col 2: Date
        QString dateOnly = rec.date.left(10);
        item->setText(2, dateOnly);
        item->setForeground(2, QBrush(QColor("#8A8B8B")));

        // Col 3: Duration
        if (rec.duration > 0) {
            int secs = rec.duration / 1000000000;
            item->setText(3, QString("%1:%2").arg(secs/60).arg(secs%60, 2, 10, QChar('0')));
        }
        item->setForeground(3, QBrush(QColor("#8A8B8B")));

        // Col 4: Title
        item->setText(4, rec.title);

        m_tree->addTopLevelItem(item);
    }
}

void HistoryPage::refresh() {
    if (m_playing) onStopPlayback();
    loadRecordings();
    m_titleInput->setText("Select a recording");
    m_statusLabel->clear(); m_durationLabel->clear();
    m_filesLabel->clear(); m_sizeLabel->clear();
    m_playBtn->setEnabled(false); m_stopBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);
    m_uploadBtn->hide();
    m_ytLinkLabel->hide();
    hideUploadForm();
    m_thumbnailLabel->setPixmap(QPixmap());
    m_thumbnailLabel->setText("No recording selected");
    m_previewStack->setCurrentIndex(0);
}

void HistoryPage::onRecordingSelected() {
    auto *item = m_tree->currentItem();
    int row = item ? m_tree->indexOfTopLevelItem(item) : -1;
    if (row < 0 || row >= m_recordings.size()) return;
    if (m_playing) onStopPlayback();

    const auto &rec = m_recordings[row];
    m_titleInput->setText(rec.title);

    if (rec.status == "completed")
        m_statusLabel->setText("Status: Completed"),
        m_statusLabel->setStyleSheet("QLabel { color: #06969A; font-size: 13px; }");
    else if (rec.status == "failed")
        m_statusLabel->setText("Status: Failed"),
        m_statusLabel->setStyleSheet("QLabel { color: #CC0403; font-size: 13px; }");
    else
        m_statusLabel->setText("Status: " + (rec.status.isEmpty() ? "unknown" : rec.status)),
        m_statusLabel->setStyleSheet("QLabel { color: #8A8B8B; font-size: 13px; }");

    if (rec.duration > 0) {
        int s = rec.duration / 1000000000;
        m_durationLabel->setText(QString("Duration: %1:%2").arg(s/60).arg(s%60,2,10,QChar('0')));
    } else {
        m_durationLabel->setText("Duration: unknown");
    }

    QStringList fl;
    if (!rec.mergedFile.isEmpty() && QFile::exists(rec.mergedFile)) fl << "Merged";
    if (!rec.verticalFile.isEmpty() && QFile::exists(rec.verticalFile)) fl << "Vertical";
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
    m_playBtn->setText(QString::fromUtf8("\u25B6"));
    m_deleteBtn->setEnabled(true);

    // Show thumbnail
    updateThumbnail(video);

    // Pre-load source for playback
    if (!video.isEmpty()) {
        m_player->setSource(QUrl::fromLocalFile(video));
    }
}

QString HistoryPage::findBestVideo(const RecordingEntry &rec) {
    if (!rec.mergedFile.isEmpty() && QFile::exists(rec.mergedFile)) return rec.mergedFile;
    if (!rec.verticalFile.isEmpty() && QFile::exists(rec.verticalFile)) return rec.verticalFile;
    if (!rec.screenFile.isEmpty() && QFile::exists(rec.screenFile)) return rec.screenFile;
    return {};
}

void HistoryPage::onPlayClicked() {
    if (m_playing) {
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            m_player->pause();
            m_playBtn->setText(QString::fromUtf8("\u25B6"));
        } else {
            m_previewStack->setCurrentIndex(1);
            m_player->play();
            m_playBtn->setText(QString::fromUtf8("\u23F8"));
        }
        return;
    }

    if (m_player->source().isEmpty()) return;

    m_previewStack->setCurrentIndex(1);
    m_player->play();
    m_playing = true;
    m_playBtn->setText(QString::fromUtf8("\u23F8"));
    m_stopBtn->setEnabled(true);
}

void HistoryPage::onStopPlayback() {
    m_player->stop();
    m_player->setSource(QUrl());
    m_playing = false;
    m_playBtn->setText(QString::fromUtf8("\u25B6"));
    m_stopBtn->setEnabled(false);
    m_seekSlider->setValue(0);
    m_timeLabel->setText("0:00 / 0:00");
    // Show last frame or thumbnail
    if (!m_lastFrame.isNull()) {
        m_thumbnailLabel->setPixmap(QPixmap::fromImage(m_lastFrame).scaled(
            m_thumbnailLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    m_previewStack->setCurrentIndex(0);
}

void HistoryPage::onDeleteClicked() {
    auto *item = m_tree->currentItem();
    if (!item) return;
    int row = m_tree->indexOfTopLevelItem(item);
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

void HistoryPage::saveYouTubeMetadata(const QString &folder, const QString &videoId,
                                       const QString &videoUrl) {
    QString jsonPath = folder + "/recording.json";
    QJsonObject root;
    QFile file(jsonPath);
    if (file.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
    }
    QJsonObject yt;
    yt["video_id"] = videoId;
    yt["video_url"] = videoUrl;
    yt["uploaded_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    root["youtube"] = yt;

    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

void HistoryPage::onSearchChanged(const QString &text) {
    QString q = text.toLower();
    for (int i = 0; i < m_tree->topLevelItemCount(); i++) {
        auto *item = m_tree->topLevelItem(i);
        bool match = item->text(4).toLower().contains(q) ||
                     item->text(2).toLower().contains(q);
        item->setHidden(!match);
    }
}
