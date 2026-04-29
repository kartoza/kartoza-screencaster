/**
 * @file historypage.h
 * @brief Recording history browser with inline playback.
 */

#pragma once

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoSink>
#include <QVideoFrame>

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
};

/**
 * @class HistoryPage
 * @brief Page widget for browsing past recordings and playing them back.
 *
 * Lists all recordings found in the output directory, shows metadata details,
 * and provides an inline video player with seek controls.
 */
class HistoryPage : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Construct the history page.
     * @param parent Optional parent widget.
     */
    explicit HistoryPage(QWidget *parent = nullptr);
    /** @brief Reload the recording list from disk. */
    void refresh();

signals:
    /**
     * @brief Emitted when the user requests reprocessing of a recording.
     * @param folder Path to the recording directory to reprocess.
     */
    void reprocessRequested(const QString &folder);

private slots:
    /**
     * @brief Handle selection of a recording in the list.
     * @param row Selected row index.
     */
    void onRecordingSelected(int row);
    /** @brief Start or resume inline video playback. */
    void onPlayClicked();
    /** @brief Stop inline video playback. */
    void onStopPlayback();
    /** @brief Delete the currently selected recording. */
    void onDeleteClicked();
    /**
     * @brief Filter the recording list by search text.
     * @param text Current search input text.
     */
    void onSearchChanged(const QString &text);

private:
    /** @brief Build the complete UI layout. */
    void setupUI();
    /** @brief Scan the output directory and populate the recording list. */
    void loadRecordings();
    /**
     * @brief Find the best available video file for playback.
     * @param rec The recording entry to search.
     * @return Path to the preferred video file.
     */
    QString findBestVideo(const RecordingEntry &rec);

    /** @brief List widget displaying available recordings. */
    QListWidget *m_list;
    /** @brief Search/filter input field. */
    QLineEdit *m_searchInput;

    // -- Player widgets --
    /** @brief Label used as the video rendering surface. */
    QLabel *m_videoLabel;
    /** @brief Video sink receiving decoded frames for display. */
    QVideoSink *m_videoSink;
    /** @brief Media player instance for inline playback. */
    QMediaPlayer *m_player;
    /** @brief Audio output device for playback. */
    QAudioOutput *m_audioOutput;
    /** @brief Play/pause button. */
    QPushButton *m_playBtn;
    /** @brief Stop playback button. */
    QPushButton *m_stopBtn;
    /** @brief Seek slider for video position. */
    QSlider *m_seekSlider;
    /** @brief Label showing current playback time. */
    QLabel *m_timeLabel;
    /** @brief Whether playback is currently active. */
    bool m_playing = false;

    // -- Detail labels --
    /** @brief Label displaying the recording title. */
    QLabel *m_titleLabel;
    /** @brief Label displaying the processing status. */
    QLabel *m_statusLabel;
    /** @brief Label displaying the recording duration. */
    QLabel *m_durationLabel;
    /** @brief Label listing output file names. */
    QLabel *m_filesLabel;
    /** @brief Label displaying the total file size. */
    QLabel *m_sizeLabel;

    // -- Actions --
    /** @brief Button to delete the selected recording. */
    QPushButton *m_deleteBtn;

    /** @brief Cached list of all discovered recording entries. */
    QList<RecordingEntry> m_recordings;
};
