/**
 * @file historypage.h
 * @brief Recording history browser with inline playback and YouTube upload.
 */

#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTextEdit>
#include <QSlider>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QVideoSink>
#include <QProgressBar>
#include <QStackedWidget>

class YouTube;

/**
 * @struct RecordingEntry
 * @brief Metadata for a single recorded session stored on disk.
 */
struct RecordingEntry {
    QString folder;        /**< Absolute path to the recording directory. */
    QString dirName;       /**< Directory base name. */
    QString title;         /**< Recording title from metadata. */
    QString status;        /**< Processing status (e.g. "done", "pending"). */
    QString date;          /**< Date string when the recording was made. */
    qint64 duration = 0;   /**< Recording duration in nanoseconds. */
    qint64 totalSize = 0;  /**< Total size of all output files in bytes. */
    QString mergedFile;    /**< Path to the merged output video, if any. */
    QString verticalFile;  /**< Path to the vertical format video, if any. */
    QString screenFile;    /**< Path to the raw screen capture file. */
    QString audioFile;     /**< Path to the raw audio capture file. */
    QString webcamFile;    /**< Path to the raw webcam capture file. */
    QString description;   /**< Recording description from metadata. */
    // YouTube
    QString youtubeVideoId;  /**< YouTube video ID if uploaded. */
    QString youtubeVideoUrl; /**< YouTube video URL if uploaded. */
};

/**
 * @class HistoryPage
 * @brief Page widget for browsing past recordings and playing them back.
 */
class HistoryPage : public QWidget {
    Q_OBJECT

public:
    explicit HistoryPage(QWidget *parent = nullptr);
    void refresh();
    static QString findBestVideo(const RecordingEntry &rec);

signals:
    void reprocessRequested(const QString &folder);

private slots:
    void onRecordingSelected();
    void onPlayClicked();
    void onStopPlayback();
    void onDeleteClicked();
    void onSearchChanged(const QString &text);

private:
    void setupUI();
    void loadRecordings();
    void updateThumbnail(const QString &videoPath);
    void showUploadForm();
    void hideUploadForm();
    void renameRecording(int row, const QString &newTitle);
    void saveYouTubeMetadata(const QString &folder, const QString &videoId,
                              const QString &videoUrl);

    // -- List --
    QTreeWidget *m_tree;
    QLineEdit *m_searchInput;

    // -- Preview --
    QStackedWidget *m_previewStack;
    QLabel *m_thumbnailLabel;
    QVideoWidget *m_videoWidget;
    QMediaPlayer *m_player;
    QAudioOutput *m_audioOutput;
    QPushButton *m_playBtn;
    QPushButton *m_stopBtn;
    QSlider *m_seekSlider;
    QLabel *m_timeLabel;
    bool m_playing = false;
    QImage m_lastFrame;

    // -- Details --
    QLineEdit *m_titleInput;
    QLabel *m_statusLabel;
    QLabel *m_durationLabel;
    QLabel *m_filesLabel;
    QLabel *m_sizeLabel;

    // -- Actions --
    QPushButton *m_deleteBtn;

    // -- YouTube upload form --
    QWidget *m_uploadForm;
    QLineEdit *m_ytTitleInput;
    QTextEdit *m_ytDescInput;
    QLineEdit *m_ytTagsInput;
    QComboBox *m_ytPrivacyCombo;
    QComboBox *m_ytPlaylistCombo;
    QPushButton *m_ytUploadBtn;
    QPushButton *m_ytCancelBtn;
    QProgressBar *m_uploadProgress;
    QLabel *m_ytLinkLabel;
    QPushButton *m_uploadBtn;
    YouTube *m_youtube = nullptr;

    QList<RecordingEntry> m_recordings;
};
