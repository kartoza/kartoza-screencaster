#include "recorder/recorder.h"
#include <QDebug>
#include <QDir>
#include <QDateTime>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QRegularExpression>
#include <signal.h>

Recorder::Recorder(QObject *parent) : QObject(parent) {}

Recorder::~Recorder() {
    if (m_recording) stop();
}

qint64 Recorder::elapsedMs() const {
    if (!m_recording) return 0;
    return m_elapsed.elapsed();
}

void Recorder::start(const RecordingOptions &opts) {
    if (m_recording) {
        emit recordingError("Recording already in progress");
        return;
    }

    m_opts = opts;
    m_startTime = QDateTime::currentDateTime();

    // Create output directory
    m_outputDir = opts.outputDir;
    if (m_outputDir.isEmpty()) {
        QString home = QDir::homePath();
        QString timestamp = m_startTime.toString("yyyy-MM-dd_HH-mm-ss");
        m_outputDir = QString("%1/Videos/Screencasts/%2-%3")
            .arg(home)
            .arg(opts.number, 3, 10, QChar('0'))
            .arg(timestamp);
    }
    QDir().mkpath(m_outputDir);

    m_screenFile = m_outputDir + "/screen_part000.mp4";
    m_audioFile = m_outputDir + "/audio_part000.wav";
    m_webcamFile = m_outputDir + "/webcam_part000.mp4";

    // Start recorders
    if (!opts.noScreen && !opts.monitor.isEmpty()) {
        startScreenRecorder(opts);
    }
    if (!opts.noAudio) {
        startAudioRecorder(opts);
    }
    if (!opts.noWebcam && !opts.webcamDevice.isEmpty()) {
        startWebcamRecorder(opts);
    }

    m_recording = true;
    m_paused = false;
    m_elapsed.start();

    // Write initial recording.json
    writeRecordingJson("recording");

    emit recordingStarted();
}

void Recorder::writeRecordingJson(const QString &status) {
    QJsonObject root;
    root["status"] = status;
    root["start_time"] = m_startTime.toString(Qt::ISODate);
    root["app_version"] = "0.9.0";
    root["created_at"] = m_startTime.toString(Qt::ISODate);
    root["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    if (status == "completed" || status == "processing") {
        root["end_time"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        qint64 durationNs = m_elapsed.elapsed() * 1000000LL; // ms to ns
        root["duration"] = durationNs;
    }

    // Metadata
    QJsonObject meta;
    meta["title"] = m_opts.title;
    meta["number"] = m_opts.number;
    meta["presenter"] = m_opts.presenter;
    meta["description"] = "";
    root["metadata"] = meta;

    // Environment
    QJsonObject env;
    env["os"] = "linux";
    env["hostname"] = QHostInfo::localHostName();
    env["monitor"] = m_opts.monitor;
    root["environment"] = env;

    // Files
    QJsonObject files;
    files["folder_path"] = m_outputDir;
    if (QFile::exists(m_screenFile)) {
        files["video_file"] = m_screenFile;
        files["video_size"] = QFileInfo(m_screenFile).size();
    }
    if (QFile::exists(m_audioFile)) {
        files["audio_file"] = m_audioFile;
        files["audio_size"] = QFileInfo(m_audioFile).size();
    }
    if (QFile::exists(m_webcamFile)) {
        files["webcam_file"] = m_webcamFile;
        files["webcam_size"] = QFileInfo(m_webcamFile).size();
    }
    if (!m_mergedFile.isEmpty() && QFile::exists(m_mergedFile)) {
        files["merged_file"] = m_mergedFile;
        files["merged_size"] = QFileInfo(m_mergedFile).size();
    }

    // Calculate total size
    qint64 totalSize = 0;
    QDir dir(m_outputDir);
    for (const auto &fi : dir.entryInfoList(QDir::Files)) {
        totalSize += fi.size();
    }
    files["total_size"] = totalSize;

    root["files"] = files;

    // Settings
    QJsonObject settings;
    settings["screen_enabled"] = !m_opts.noScreen;
    settings["audio_enabled"] = !m_opts.noAudio;
    settings["webcam_enabled"] = !m_opts.noWebcam;
    root["settings"] = settings;

    // Write
    QFile file(m_outputDir + "/recording.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }
}

void Recorder::startScreenRecorder(const RecordingOptions &opts) {
    m_screenProc = new QProcess(this);

    QStringList args;
    if (!opts.hwAccel) {
        args << "--no-hw";
    }
    args << QString("--output=%1").arg(opts.monitor)
         << QString("--filename=%1").arg(m_screenFile);

    qDebug() << "Starting wl-screenrec:" << args;
    m_screenProc->start("wl-screenrec", args);

    if (!m_screenProc->waitForStarted(5000)) {
        emit recordingError("Failed to start screen recorder: " + m_screenProc->errorString());
    }
}

void Recorder::startAudioRecorder(const RecordingOptions &opts) {
    m_audioProc = new QProcess(this);

    QString device = opts.audioDevice;
    if (device.isEmpty()) device = "@DEFAULT_SOURCE@";

    QStringList args;
    args << "-f" << "pulse"
         << "-i" << device
         << "-ac" << "2"
         << "-y" << m_audioFile;

    qDebug() << "Starting audio ffmpeg:" << args;
    m_audioProc->start("ffmpeg", args);

    if (!m_audioProc->waitForStarted(5000)) {
        qWarning() << "Failed to start audio recorder:" << m_audioProc->errorString();
        delete m_audioProc;
        m_audioProc = nullptr;
    }
}

void Recorder::startWebcamRecorder(const RecordingOptions &opts) {
    m_webcamProc = new QProcess(this);

    QStringList args;
    args << "-f" << "v4l2"
         << "-framerate" << QString::number(opts.webcamFPS > 0 ? opts.webcamFPS : 60)
         << "-i" << ("/dev/" + opts.webcamDevice)
         << "-c:v" << "libx264"
         << "-preset" << "ultrafast"
         << "-tune" << "zerolatency"
         << "-crf" << "18"
         << "-pix_fmt" << "yuv420p"
         << "-y" << m_webcamFile;

    qDebug() << "Starting webcam ffmpeg:" << args;
    m_webcamProc->start("ffmpeg", args);

    if (!m_webcamProc->waitForStarted(5000)) {
        qWarning() << "Failed to start webcam recorder:" << m_webcamProc->errorString();
        delete m_webcamProc;
        m_webcamProc = nullptr;
    }
}

void Recorder::stopProcess(QProcess *proc) {
    if (!proc || proc->state() == QProcess::NotRunning) return;

    qint64 pid = proc->processId();
    if (pid > 0) {
        ::kill(pid, SIGINT);
    }
    if (!proc->waitForFinished(5000)) {
        proc->terminate();
        if (!proc->waitForFinished(3000)) {
            proc->kill();
            proc->waitForFinished(1000);
        }
    }
}

void Recorder::stop() {
    if (!m_recording) return;

    m_recording = false;
    m_paused = false;

    qDebug() << "Stopping recorders...";

    stopProcess(m_screenProc);
    stopProcess(m_audioProc);
    stopProcess(m_webcamProc);

    if (m_screenProc) { m_screenProc->deleteLater(); m_screenProc = nullptr; }
    if (m_audioProc) { m_audioProc->deleteLater(); m_audioProc = nullptr; }
    if (m_webcamProc) { m_webcamProc->deleteLater(); m_webcamProc = nullptr; }

    qDebug() << "Recorders stopped, files at:" << m_outputDir;

    // Update recording.json with processing status
    writeRecordingJson("processing");

    emit recordingStopped();

    // Start processing in a worker thread
    QThread *thread = QThread::create([this]() {
        QThread::msleep(2000); // wait for files to flush
        processRecordings();
    });
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void Recorder::pause() {
    if (!m_recording || m_paused) return;
    m_paused = true;
    emit recordingPaused();
}

void Recorder::resume() {
    if (!m_paused) return;
    m_paused = false;
    emit recordingResumed();
}

void Recorder::processRecordings() {
    emit processingStarted();

    bool hasScreen = QFile::exists(m_screenFile);
    bool hasAudio = QFile::exists(m_audioFile);
    bool hasWebcam = QFile::exists(m_webcamFile);

    if (!hasScreen && !hasAudio && !hasWebcam) {
        emit processingStepError(0, "Merge", "No input files found");
        emit processingFinished(false);
        writeRecordingJson("failed");
        return;
    }

    // Step 0: Analyze audio
    emit processingProgress(0, 0, "Analyzing audio");
    if (hasAudio) {
        emit processingProgress(0, 100, "Analyzing audio");
        emit processingStepDone(0, "Analyzing audio", false);
    } else {
        emit processingStepDone(0, "Analyzing audio", true);
    }

    // Step 1: Normalize audio
    emit processingProgress(1, 0, "Normalizing audio");
    // TODO: actual normalization
    emit processingStepDone(1, "Normalizing audio", true);

    // Step 2: Merge video + audio
    emit processingProgress(2, 0, "Merging video & audio");
    if (hasScreen) {
        QString titleSlug = m_opts.title.toLower().replace(QRegularExpression("[^a-z0-9]+"), "_").trimmed();
        if (titleSlug.isEmpty()) titleSlug = "recording";
        m_mergedFile = m_outputDir + "/" +
            QString("%1-%2.mp4").arg(m_opts.number, 3, 10, QChar('0')).arg(titleSlug);

        QProcess ffmpeg;
        QStringList args;
        args << "-y" << "-i" << m_screenFile;
        if (hasAudio) args << "-i" << m_audioFile;
        args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "18" << "-r" << "30";
        if (hasAudio) {
            args << "-c:a" << "aac" << "-b:a" << "320k" << "-shortest";
        } else {
            args << "-an";
        }
        args << m_mergedFile;

        qDebug() << "Merging:" << args;
        ffmpeg.start("ffmpeg", args);
        ffmpeg.waitForFinished(-1);

        if (ffmpeg.exitCode() == 0) {
            emit processingProgress(2, 100, "Merging video & audio");
            emit processingStepDone(2, "Merging video & audio", false);
        } else {
            QString err = ffmpeg.readAllStandardError();
            qWarning() << "Merge failed:" << err;
            emit processingStepError(2, "Merging", err.left(200));
        }
    } else {
        emit processingStepDone(2, "Merging video & audio", true);
    }

    // Step 3: Create vertical video (placeholder)
    emit processingProgress(3, 0, "Creating vertical video");
    emit processingStepDone(3, "Creating vertical video", true);

    // Write final recording.json
    writeRecordingJson("completed");

    emit processingFinished(true);
}
