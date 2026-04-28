#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <QDir>

struct RecordingOptions {
    QString monitor;
    QString outputDir;
    bool noAudio = false;
    bool noWebcam = false;
    bool noScreen = false;
    bool hwAccel = false;
    // Canvas mode: 0=landscape, 1=vertical, 2=left split, 3=right split
    int canvasMode = 0;
    QString webcamDevice;
    int webcamFPS = 30;
    QString audioDevice = "@DEFAULT_SOURCE@";
    QString title;
    QString description;
    QString presenter;
    int number = 1;

    // Logos: up to 3 paths (left, right, banner)
    // gifLoop: 0=first frame only, 1=play once, 2=continuous
    struct LogoOpts {
        QString path;
        int gifLoop = 2;
        int gifLoopMax = 3;
        // Canvas-relative position (0.0 - 1.0 of canvas dimensions)
        double relX = 0, relY = 0, relW = 0.15, relH = 0.15;
        bool isGif() const { return path.toLower().endsWith(".gif"); }
    };
    // Webcam overlay (canvas-relative position + shape)
    // shape: 0=round/bubble, 1=square, 2=rectangle
    double webcamRelX = 0.85, webcamRelY = 0.8, webcamRelW = 0.15, webcamRelH = 0.2;
    int webcamShape = 0;
    LogoOpts leftLogo;
    LogoOpts rightLogo;
    LogoOpts bannerLogo;
    QString titleColor = "#62A4C7";
};

class Recorder : public QObject {
    Q_OBJECT

public:
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder();

    bool isRecording() const { return m_recording; }
    bool isPaused() const { return m_paused; }
    qint64 elapsedMs() const;

    QString outputDir() const { return m_outputDir; }

public slots:
    void start(const RecordingOptions &opts);
    void stop();
    void pause();
    void resume();
    void reprocess(const QString &folder);

signals:
    void recordingStarted();
    void recordingStopped();
    void recordingPaused();
    void recordingResumed();
    void recordingError(const QString &error);
    void roomNoiseStarted();
    void roomNoiseProgress(int secondsRemaining);
    void roomNoiseFinished();

    // Processing signals
    void processingStarted();
    void processingProgress(int step, int percent, const QString &stepName);
    void processingStepDone(int step, const QString &stepName, bool skipped);
    void processingStepError(int step, const QString &stepName, const QString &error);
    void processingFinished(bool success);

private:
    void startScreenRecorder(const RecordingOptions &opts);
    void startAudioRecorder(const RecordingOptions &opts);
    void startWebcamRecorder(const RecordingOptions &opts);
    void stopProcess(QProcess *proc);
    void processRecordings();

    bool m_recording = false;
    bool m_paused = false;
    QElapsedTimer m_elapsed;
    qint64 m_elapsedAccumulated = 0; // ms accumulated before current segment

    QProcess *m_screenProc = nullptr;
    QProcess *m_audioProc = nullptr;
    QProcess *m_webcamProc = nullptr;

    QString m_outputDir;
    QString m_screenFile;  // current part file
    QString m_audioFile;   // current part file
    QString m_webcamFile;  // current part file

    // Part tracking
    int m_currentPart = 0;
    QStringList m_screenParts;
    QStringList m_audioParts;
    QStringList m_webcamParts;

    RecordingOptions m_opts;
    QDateTime m_startTime;
    QString m_mergedFile;

    void stopAllProcesses();
    void captureRoomNoise();
    void startRecordersForPart();
    void renameOutputFolder();

    void writeRecordingJson(const QString &status);

    bool m_processing = false;
};
