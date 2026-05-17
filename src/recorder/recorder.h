/**
 * @file recorder.h
 * @brief Screen, audio, and webcam recording orchestration.
 *
 * Defines RecordingOptions (capture parameters, logo/webcam overlays) and the
 * Recorder class that manages FFmpeg sub-processes for multi-stream capture
 * with pause/resume and post-recording processing.
 */
#pragma once

#include <atomic>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <QDir>
#include <QVector>

/**
 * @brief Configuration for a recording session.
 *
 * Holds every user-adjustable parameter: which devices to capture, output
 * directory, canvas layout, logo/webcam overlay positions, and metadata.
 */
struct RecordingOptions {
    QString monitor;                              /**< Display/monitor identifier for screen capture. */
    QString outputDir;                            /**< Directory where raw and processed files are stored. */
    bool noAudio = false;                         /**< If true, skip audio recording. */
    bool noWebcam = false;                        /**< If true, skip webcam recording. */
    bool noScreen = false;                        /**< If true, skip screen recording. */
    bool hwAccel = false;                         /**< Enable hardware-accelerated encoding if available. */
    /** @brief Canvas layout mode: 0=landscape, 1=vertical, 2=left split, 3=right split. */
    int canvasMode = 0;
    QString webcamDevice;                         /**< V4L2 device path for the webcam (e.g. /dev/video0). */
    int webcamFPS = 30;                           /**< Webcam capture frame rate. */
    QString audioDevice = "@DEFAULT_SOURCE@";     /**< PulseAudio/PipeWire source name. */
    QString title;                                /**< Episode or recording title. */
    QString description;                          /**< Optional description stored in metadata. */
    QString presenter;                            /**< Presenter name stored in metadata. */
    int number = 1;                               /**< Episode / recording number. */

    /**
     * @brief Options for a single logo overlay (image or animated GIF).
     *
     * Positions are expressed as fractions (0.0 -- 1.0) of the canvas
     * dimensions so they remain resolution-independent.
     */
    struct LogoOpts {
        QString path;                             /**< Filesystem path to the logo image or GIF. */
        /** @brief GIF loop mode: 0=first frame only, 1=play once, 2=continuous. */
        int gifLoop = 2;
        int gifLoopMax = 3;                       /**< Maximum number of GIF loops (when gifLoop==1). */
        double relX = 0;                          /**< Relative X position on the canvas (0.0 -- 1.0). */
        double relY = 0;                          /**< Relative Y position on the canvas (0.0 -- 1.0). */
        double relW = 0.15;                       /**< Relative width on the canvas (0.0 -- 1.0). */
        double relH = 0.15;                       /**< Relative height on the canvas (0.0 -- 1.0). */
        /** @brief Return true if the logo path points to an animated GIF. */
        bool isGif() const { return path.toLower().endsWith(".gif"); }
    };

    /** @brief Webcam overlay relative X position (0.0 -- 1.0). */
    double webcamRelX = 0.85;
    /** @brief Webcam overlay relative Y position (0.0 -- 1.0). */
    double webcamRelY = 0.8;
    /** @brief Webcam overlay relative width (0.0 -- 1.0). */
    double webcamRelW = 0.15;
    /** @brief Webcam overlay relative height (0.0 -- 1.0). */
    double webcamRelH = 0.2;
    /** @brief Webcam shape: 0=round/bubble, 1=square, 2=rectangle. */
    int webcamShape = 0;
    /** @brief Overlay logos/GIFs placed on the canvas (0..N). */
    QVector<LogoOpts> logos;
    QString titleColor = "#62A4C7";               /**< Hex colour used when rendering the title. */
    QString startSound;                           /**< Path to audio file played at recording start. */
    QString endSound;                             /**< Path to audio file played at recording end. */
    /** @brief Screen crop as fractions (0.0-1.0) of the screen dimensions. */
    double screenCropTop = 0, screenCropBottom = 0, screenCropLeft = 0, screenCropRight = 0;
};

/**
 * @brief Manages FFmpeg sub-processes for multi-stream recording.
 *
 * Recorder spawns separate FFmpeg processes for screen, audio, and webcam
 * capture.  It supports pause/resume (which splits the recording into
 * numbered parts) and triggers post-recording processing (concatenation,
 * loudness normalisation, merging) once the user stops the session.
 */
class Recorder : public QObject {
    Q_OBJECT

public:
    /** @brief Construct a Recorder with an optional QObject parent. */
    explicit Recorder(QObject *parent = nullptr);
    /** @brief Destructor -- stops any running processes. */
    ~Recorder();

    /** @brief Return true while a recording session is active. */
    bool isRecording() const { return m_recording; }
    /** @brief Return true while the recording is paused. */
    bool isPaused() const { return m_paused; }
    /** @brief Return total elapsed recording time in milliseconds (excludes paused time). */
    qint64 elapsedMs() const;

    /** @brief Return the output directory for the current/last session. */
    QString outputDir() const { return m_outputDir; }

public slots:
    /** @brief Start a new recording session with the given options. */
    void start(const RecordingOptions &opts);
    /** @brief Stop the current recording and begin post-processing. */
    void stop();
    /** @brief Pause the current recording (splits into a new part on resume). */
    void pause();
    /** @brief Resume a paused recording, starting a new part. */
    void resume();
    /** @brief Re-run post-processing on an existing recording folder. */
    void reprocess(const QString &folder);
    /** @brief Cancel in-progress post-processing. */
    void cancelProcessing();

signals:
    /** @brief Emitted when recording has successfully started. */
    void recordingStarted();
    /** @brief Emitted when recording has stopped and files are written. */
    void recordingStopped();
    /** @brief Emitted when recording is paused. */
    void recordingPaused();
    /** @brief Emitted when recording is resumed after a pause. */
    void recordingResumed();
    /** @brief Emitted when a recording error occurs. */
    void recordingError(const QString &error);
    /** @brief Emitted when room-noise capture begins. */
    void roomNoiseStarted();
    /** @brief Emitted periodically during room-noise capture with seconds remaining. */
    void roomNoiseProgress(int secondsRemaining);
    /** @brief Emitted when room-noise capture is complete. */
    void roomNoiseFinished();

    /** @brief Emitted when post-recording processing begins. */
    void processingStarted();
    /**
     * @brief Emitted to report progress of a processing step.
     * @param step      Zero-based step index.
     * @param percent   Completion percentage (0-100) within this step.
     * @param stepName  Human-readable name of the step.
     */
    void processingProgress(int step, int percent, const QString &stepName);
    /** @brief Emitted when a processing step completes (or is skipped). */
    void processingStepDone(int step, const QString &stepName, bool skipped);
    /** @brief Emitted when a processing step fails. */
    void processingStepError(int step, const QString &stepName, const QString &error);
    /** @brief Emitted when all post-processing is finished. */
    void processingFinished(bool success);

private:
    /** @brief Launch the FFmpeg screen-capture process. */
    void startScreenRecorder(const RecordingOptions &opts);
    /** @brief Launch the FFmpeg audio-capture process. */
    void startAudioRecorder(const RecordingOptions &opts);
    /** @brief Launch the FFmpeg webcam-capture process. */
    void startWebcamRecorder(const RecordingOptions &opts);
    /** @brief Gracefully stop a single FFmpeg process. */
    void stopProcess(QProcess *proc);
    /** @brief Run the full post-recording processing pipeline. */
    void processRecordings();

    bool m_recording = false;              /**< True while a session is active. */
    bool m_paused = false;                 /**< True while the session is paused. */
    QElapsedTimer m_elapsed;               /**< Timer for the current recording segment. */
    qint64 m_elapsedAccumulated = 0;       /**< Milliseconds accumulated before the current segment. */

    QProcess *m_screenProc = nullptr;      /**< FFmpeg process for screen capture. */
    QProcess *m_audioProc = nullptr;       /**< FFmpeg process for audio capture. */
    QProcess *m_webcamProc = nullptr;      /**< FFmpeg process for webcam capture. */

    QString m_outputDir;                   /**< Session output directory. */
    QString m_screenFile;                  /**< Current-part screen capture file path. */
    QString m_audioFile;                   /**< Current-part audio capture file path. */
    QString m_webcamFile;                  /**< Current-part webcam capture file path. */

    int m_currentPart = 0;                 /**< Zero-based index of the current recording part. */
    QStringList m_screenParts;             /**< Ordered list of screen-part file paths. */
    QStringList m_audioParts;              /**< Ordered list of audio-part file paths. */
    QStringList m_webcamParts;             /**< Ordered list of webcam-part file paths. */

    /** @brief Per-part stream start timestamps (ms since epoch) for sync alignment. */
    struct PartTimestamps {
        qint64 screenStartMs = 0;
        qint64 audioStartMs = 0;
        qint64 webcamStartMs = 0;
    };
    QVector<PartTimestamps> m_partTimestamps; /**< Start timestamps for each part. */

    RecordingOptions m_opts;               /**< Options for the active session. */
    QDateTime m_startTime;                 /**< Wall-clock time when the session started. */
    QString m_mergedFile;                  /**< Path to the final merged output file. */
    QString m_verticalFile;                /**< Path to the vertical format output file. */

    /** @brief Copy logos, GIFs, and sound assets into the output directory. */
    void copyAssetsToOutputDir();
    /** @brief Stop all running FFmpeg sub-processes. */
    void stopAllProcesses();
    /** @brief Record a short room-noise sample before the main capture. */
    void captureRoomNoise();
    /** @brief Start screen/audio/webcam recorders for the current part index. */
    void startRecordersForPart();
    /** @brief Rename the output folder to include the title slug after processing. */
    void renameOutputFolder();

    /** @brief Write or update the recording.json metadata file. */
    void writeRecordingJson(const QString &status);

    bool m_processing = false;             /**< True while post-processing is running. */
    std::atomic<bool> m_cancelRequested{false}; /**< Set to true to abort processing between steps. */
    QThread *m_processingThread = nullptr; /**< Worker thread for post-processing. */
};
