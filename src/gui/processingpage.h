/**
 * @file processingpage.h
 * @brief Post-recording processing progress page.
 */

#pragma once
#include <QWidget>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include "recorder/recorder.h"

/**
 * @class ProcessingPage
 * @brief Page widget showing progress of post-recording processing tasks.
 *
 * After a recording finishes, this page monitors ffmpeg merge/encode
 * tasks and displays per-task progress bars, elapsed time, and a summary.
 */
class ProcessingPage : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Construct the processing page.
     * @param parent Optional parent widget.
     */
    explicit ProcessingPage(QWidget *parent = nullptr);

    /**
     * @brief Begin monitoring a recorder's post-processing tasks.
     * @param recorder The Recorder whose tasks will be tracked.
     */
    void startMonitoring(Recorder *recorder);

signals:
    /**
     * @brief Emitted when all processing tasks have completed.
     * @param success True if all tasks succeeded.
     */
    void processingDone(bool success);
    /** @brief Emitted when the user requests navigation back to the history page. */
    void backToHistory();
    /** @brief Emitted when the user cancels processing. */
    void processingCancelled();

private:
    /** @brief Progress bars for each processing task. */
    QList<QProgressBar*> m_bars;
    /** @brief Status labels for each processing task. */
    QList<QLabel*> m_statusLabels;
    /** @brief Error message labels for each processing task. */
    QList<QLabel*> m_errorLabels;
    /** @brief Label showing total elapsed processing time. */
    QLabel *m_elapsedLabel;
    /** @brief Label showing a summary of completed/failed tasks. */
    QLabel *m_summaryLabel;
    /** @brief Button to navigate back to the history page. */
    QPushButton *m_backBtn;
    /** @brief Button to cancel processing. */
    QPushButton *m_cancelBtn;
    /** @brief Button to open the output folder in the file manager. */
    QPushButton *m_openFolderBtn;
    /** @brief Button to upload the processed video to YouTube. */
    QPushButton *m_uploadBtn;
    /** @brief Progress bar for YouTube upload. */
    QProgressBar *m_uploadBar;
    /** @brief Label showing YouTube link after upload. */
    QLabel *m_ytLinkLabel;
    /** @brief Timer updating the elapsed time display. */
    QTimer *m_elapsedTimer;
    /** @brief Elapsed timer tracking processing wall-clock time. */
    QElapsedTimer m_elapsed;
    /** @brief The recorder instance being monitored, or nullptr. */
    Recorder *m_recorder = nullptr;
};
