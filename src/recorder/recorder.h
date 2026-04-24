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
    QString webcamDevice;
    int webcamFPS = 60;
    QString audioDevice = "@DEFAULT_SOURCE@";
    QString title;
    QString presenter;
    int number = 1;
};

class Recorder : public QObject {
    Q_OBJECT

public:
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder();

    bool isRecording() const { return m_recording; }
    bool isPaused() const { return m_paused; }
    qint64 elapsedMs() const;

public slots:
    void start(const RecordingOptions &opts);
    void stop();
    void pause();
    void resume();

signals:
    void recordingStarted();
    void recordingStopped();
    void recordingPaused();
    void recordingResumed();
    void recordingError(const QString &error);

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

    QProcess *m_screenProc = nullptr;
    QProcess *m_audioProc = nullptr;
    QProcess *m_webcamProc = nullptr;

    QString m_outputDir;
    QString m_screenFile;
    QString m_audioFile;
    QString m_webcamFile;

    RecordingOptions m_opts;
    QDateTime m_startTime;
    QString m_mergedFile;

    void writeRecordingJson(const QString &status);
};
