#include "recorder/recorder.h"
#include "merger/merger.h"
#include <QApplication>
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
    if (!m_recording && !m_paused) return 0;
    if (m_paused) return m_elapsedAccumulated;
    return m_elapsedAccumulated + m_elapsed.elapsed();
}

void Recorder::start(const RecordingOptions &opts) {
    if (m_recording) {
        emit recordingError("Recording already in progress");
        return;
    }

    m_opts = opts;
    m_startTime = QDateTime::currentDateTime();
    m_currentPart = 0;
    m_elapsedAccumulated = 0;
    m_screenParts.clear();
    m_audioParts.clear();
    m_webcamParts.clear();

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

    // Set part filenames and start recorders
    startRecordersForPart();

    m_recording = true;
    m_paused = false;
    m_elapsed.start();

    // Write initial recording.json
    writeRecordingJson("recording");

    emit recordingStarted();
}

void Recorder::startRecordersForPart() {
    QString partSuffix = QString("_part%1").arg(m_currentPart, 3, 10, QChar('0'));

    if (!m_opts.noScreen && !m_opts.monitor.isEmpty()) {
        m_screenFile = m_outputDir + "/screen" + partSuffix + ".mp4";
        m_screenParts.append(m_screenFile);
        startScreenRecorder(m_opts);
    }
    if (!m_opts.noAudio) {
        m_audioFile = m_outputDir + "/audio" + partSuffix + ".wav";
        m_audioParts.append(m_audioFile);
        startAudioRecorder(m_opts);
    }
    if (!m_opts.noWebcam && !m_opts.webcamDevice.isEmpty()) {
        m_webcamFile = m_outputDir + "/webcam" + partSuffix + ".mp4";
        m_webcamParts.append(m_webcamFile);
        startWebcamRecorder(m_opts);
    }
}

void Recorder::stopAllProcesses() {
    stopProcess(m_screenProc);
    stopProcess(m_audioProc);
    stopProcess(m_webcamProc);

    if (m_screenProc) { m_screenProc->deleteLater(); m_screenProc = nullptr; }
    if (m_audioProc) { m_audioProc->deleteLater(); m_audioProc = nullptr; }
    if (m_webcamProc) { m_webcamProc->deleteLater(); m_webcamProc = nullptr; }
}

void Recorder::writeRecordingJson(const QString &status) {
    QJsonObject root;
    root["status"] = status;
    root["start_time"] = m_startTime.toString(Qt::ISODate);
    root["app_version"] = QApplication::applicationVersion();
    root["created_at"] = m_startTime.toString(Qt::ISODate);
    root["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    if (status == "completed" || status == "processing") {
        root["end_time"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        qint64 totalMs = elapsedMs();
        root["duration"] = totalMs * 1000000LL; // ms to ns
    }

    // Metadata
    QJsonObject meta;
    meta["title"] = m_opts.title;
    meta["number"] = m_opts.number;
    meta["presenter"] = m_opts.presenter;
    meta["description"] = m_opts.description;
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
    files["current_part"] = m_currentPart;

    // Part lists
    auto toJsonArray = [](const QStringList &list) {
        QJsonArray arr;
        for (const auto &s : list) arr.append(s);
        return arr;
    };
    if (!m_screenParts.isEmpty()) files["video_parts"] = toJsonArray(m_screenParts);
    if (!m_audioParts.isEmpty()) files["audio_parts"] = toJsonArray(m_audioParts);
    if (!m_webcamParts.isEmpty()) files["webcam_parts"] = toJsonArray(m_webcamParts);

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
    if (!m_opts.leftLogo.path.isEmpty()) settings["left_logo"] = m_opts.leftLogo.path;
    if (!m_opts.rightLogo.path.isEmpty()) settings["right_logo"] = m_opts.rightLogo.path;
    if (!m_opts.bannerLogo.path.isEmpty()) {
        settings["banner_logo"] = m_opts.bannerLogo.path;
        settings["banner_gif_loop"] = m_opts.bannerLogo.gifLoop;
        settings["banner_gif_loop_max"] = m_opts.bannerLogo.gifLoopMax;
    }
    settings["title_color"] = m_opts.titleColor;
    settings["canvas_mode"] = m_opts.canvasMode;
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
         << "-framerate" << QString::number(opts.webcamFPS > 0 ? opts.webcamFPS : 30)
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

void Recorder::renameOutputFolder() {
    QString slug = Merger::titleSlug(m_opts.title);
    if (slug == "recording") return; // no meaningful title

    QString parentDir = QFileInfo(m_outputDir).absolutePath();
    QString newName = QString("%1-%2")
        .arg(m_opts.number, 3, 10, QChar('0'))
        .arg(slug);
    QString newDir = parentDir + "/" + newName;

    if (newDir == m_outputDir) return; // already correct
    if (QDir(newDir).exists()) return; // avoid collision

    QString oldDir = m_outputDir;
    if (!QDir().rename(oldDir, newDir)) {
        qWarning() << "Failed to rename" << oldDir << "to" << newDir;
        return;
    }

    qDebug() << "Renamed output folder:" << oldDir << "->" << newDir;

    // Update all tracked paths
    auto updatePath = [&oldDir, &newDir](QString &path) {
        if (path.startsWith(oldDir)) {
            path = newDir + path.mid(oldDir.length());
        }
    };

    updatePath(m_outputDir);
    updatePath(m_screenFile);
    updatePath(m_audioFile);
    updatePath(m_webcamFile);
    updatePath(m_mergedFile);

    for (auto &p : m_screenParts) updatePath(p);
    for (auto &p : m_audioParts) updatePath(p);
    for (auto &p : m_webcamParts) updatePath(p);
}

void Recorder::captureRoomNoise() {
    static const int DURATION_SECS = 30;
    QString roomNoiseFile = m_outputDir + "/room_noise.wav";

    qDebug() << "Capturing room noise for" << DURATION_SECS << "seconds...";
    emit roomNoiseStarted();

    QString device = m_opts.audioDevice;
    if (device.isEmpty()) device = "@DEFAULT_SOURCE@";

    QProcess proc;
    QStringList args;
    args << "-f" << "pulse" << "-i" << device
         << "-ac" << "2" << "-t" << QString::number(DURATION_SECS)
         << "-y" << roomNoiseFile;

    proc.start("ffmpeg", args);
    if (!proc.waitForStarted(5000)) {
        qWarning() << "Failed to start room noise capture";
        emit roomNoiseFinished();
        return;
    }

    // Emit countdown progress
    for (int i = DURATION_SECS; i > 0; i--) {
        emit roomNoiseProgress(i);
        QThread::msleep(1000);
    }

    proc.waitForFinished(DURATION_SECS * 1000 + 5000);

    qDebug() << "Room noise captured:" << roomNoiseFile
             << "exists:" << QFile::exists(roomNoiseFile);
    emit roomNoiseFinished();
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
    if (!m_recording && !m_paused) {
        qWarning() << "stop() called but not recording or paused";
        return;
    }

    bool wasPaused = m_paused;

    if (!wasPaused) {
        // Accumulate final segment time
        m_elapsedAccumulated += m_elapsed.elapsed();
    }

    m_recording = false;
    m_paused = false;

    qDebug() << "Stopping recorders..." << (wasPaused ? "(from paused)" : "");

    if (!wasPaused) {
        stopAllProcesses();
    }

    qDebug() << "Recorders stopped, files at:" << m_outputDir
             << "parts:" << m_currentPart + 1;

    // Rename folder from timestamp to NNN-title format
    renameOutputFolder();

    // Update recording.json
    writeRecordingJson("processing");

    emit recordingStopped();

    // Start room noise capture + processing in a worker thread
    bool hasAudioEnabled = !m_opts.noAudio;
    int waitMs = wasPaused ? 500 : 2000;
    QThread *thread = QThread::create([this, waitMs, hasAudioEnabled]() {
        QThread::msleep(waitMs);

        // Room noise capture (only if audio was recorded)
        if (hasAudioEnabled) {
            captureRoomNoise();
        }

        processRecordings();
    });
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void Recorder::pause() {
    if (!m_recording || m_paused) return;

    qDebug() << "Pausing recording, stopping processes for part" << m_currentPart;

    // Accumulate elapsed time for this segment
    m_elapsedAccumulated += m_elapsed.elapsed();

    // Stop all recorder processes
    stopAllProcesses();

    m_paused = true;
    m_recording = false; // processes are stopped

    writeRecordingJson("paused");
    emit recordingPaused();
}

void Recorder::resume() {
    if (!m_paused) return;

    m_currentPart++;
    qDebug() << "Resuming recording, starting part" << m_currentPart;

    // Start new recorder processes with incremented part number
    startRecordersForPart();

    m_paused = false;
    m_recording = true;
    m_elapsed.start(); // restart elapsed for this segment

    writeRecordingJson("recording");
    emit recordingResumed();
}

void Recorder::reprocess(const QString &folder) {
    if (m_recording || m_processing) {
        emit recordingError("Cannot reprocess while recording or processing");
        return;
    }

    m_outputDir = folder;

    // Load recording.json to restore options
    QFile file(folder + "/recording.json");
    if (!file.open(QIODevice::ReadOnly)) {
        emit recordingError("No recording.json in " + folder);
        return;
    }
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    auto meta = root["metadata"].toObject();
    m_opts.title = meta["title"].toString();
    m_opts.number = meta["number"].toInt(1);
    m_opts.presenter = meta["presenter"].toString();
    m_opts.description = meta["description"].toString();

    auto settings = root["settings"].toObject();
    m_opts.noScreen = !settings["screen_enabled"].toBool(true);
    m_opts.noAudio = !settings["audio_enabled"].toBool(true);
    m_opts.noWebcam = !settings["webcam_enabled"].toBool(true);
    m_opts.titleColor = settings["title_color"].toString("#62A4C7");
    m_opts.canvasMode = settings["canvas_mode"].toInt(0);

    if (settings.contains("left_logo")) {
        m_opts.leftLogo.path = settings["left_logo"].toString();
    }
    if (settings.contains("right_logo")) {
        m_opts.rightLogo.path = settings["right_logo"].toString();
    }
    if (settings.contains("banner_logo")) {
        m_opts.bannerLogo.path = settings["banner_logo"].toString();
        m_opts.bannerLogo.gifLoop = settings["banner_gif_loop"].toInt(2);
        m_opts.bannerLogo.gifLoopMax = settings["banner_gif_loop_max"].toInt(3);
    }

    // Find source files in folder
    QDir dir(folder);
    m_screenParts.clear(); m_audioParts.clear(); m_webcamParts.clear();

    for (const auto &f : dir.entryInfoList({"screen_part*.mp4"}, QDir::Files, QDir::Name))
        m_screenParts.append(f.absoluteFilePath());
    for (const auto &f : dir.entryInfoList({"audio_part*.wav"}, QDir::Files, QDir::Name))
        m_audioParts.append(f.absoluteFilePath());
    for (const auto &f : dir.entryInfoList({"webcam_part*.mp4"}, QDir::Files, QDir::Name))
        m_webcamParts.append(f.absoluteFilePath());

    // Also check for combined files
    if (m_screenParts.isEmpty() && QFile::exists(folder + "/screen_combined.mp4"))
        m_screenParts.append(folder + "/screen_combined.mp4");
    if (m_audioParts.isEmpty() && QFile::exists(folder + "/audio_combined.wav"))
        m_audioParts.append(folder + "/audio_combined.wav");
    if (m_webcamParts.isEmpty() && QFile::exists(folder + "/webcam_combined.mp4"))
        m_webcamParts.append(folder + "/webcam_combined.mp4");

    m_screenFile = m_screenParts.isEmpty() ? QString() : m_screenParts.first();
    m_audioFile = m_audioParts.isEmpty() ? QString() : m_audioParts.first();
    m_webcamFile = m_webcamParts.isEmpty() ? QString() : m_webcamParts.first();
    m_currentPart = 0;
    m_mergedFile.clear();

    // Update status
    writeRecordingJson("processing");
    // Note: do NOT emit recordingStopped() here — the caller (mainwindow reprocess handler)
    // already navigates to the processing page and calls startMonitoring

    QThread *thread = QThread::create([this]() {
        processRecordings();
    });
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void Recorder::processRecordings() {
    m_processing = true;
    emit processingStarted();

    // Concatenate parts if we had pause/resume cycles
    if (m_screenParts.size() > 1) {
        QString c = Merger::concatenateParts(m_screenParts, m_outputDir + "/screen_combined.mp4", "concat_screen");
        if (!c.isEmpty()) m_screenFile = c;
    } else if (!m_screenParts.isEmpty()) {
        m_screenFile = m_screenParts.first();
    }
    if (m_audioParts.size() > 1) {
        QString c = Merger::concatenateParts(m_audioParts, m_outputDir + "/audio_combined.wav", "concat_audio");
        if (!c.isEmpty()) m_audioFile = c;
    } else if (!m_audioParts.isEmpty()) {
        m_audioFile = m_audioParts.first();
    }
    if (m_webcamParts.size() > 1) {
        QString c = Merger::concatenateParts(m_webcamParts, m_outputDir + "/webcam_combined.mp4", "concat_webcam");
        if (!c.isEmpty()) m_webcamFile = c;
    } else if (!m_webcamParts.isEmpty()) {
        m_webcamFile = m_webcamParts.first();
    }

    bool hasScreen = QFile::exists(m_screenFile);
    bool hasAudio = QFile::exists(m_audioFile);

    if (!hasScreen && !hasAudio && !QFile::exists(m_webcamFile)) {
        emit processingStepError(0, "Merge", "No input files found");
        emit processingFinished(false);
        writeRecordingJson("failed");
        m_processing = false;
        return;
    }

    QString audioToUse = m_audioFile;
    QString normalizedAudio = m_outputDir + "/audio_normalized.wav";

    // Step 0: Analyze audio
    emit processingProgress(0, 0, "Analyzing audio");
    Merger::LoudnormParams loudnorm;
    if (hasAudio) {
        loudnorm = Merger::analyzeAudio(m_audioFile);
        emit processingProgress(0, 100, "Analyzing audio");
        emit processingStepDone(0, "Analyzing audio", false);
    } else {
        emit processingStepDone(0, "Analyzing audio", true);
    }

    // Step 1: Normalize audio
    emit processingProgress(1, 0, "Normalizing audio");
    if (hasAudio && loudnorm.valid) {
        if (Merger::normalizeAudio(m_audioFile, normalizedAudio, loudnorm)) {
            audioToUse = normalizedAudio;
            emit processingProgress(1, 100, "Normalizing audio");
            emit processingStepDone(1, "Normalizing audio", false);
        } else {
            emit processingStepDone(1, "Normalizing audio", true);
        }
    } else {
        emit processingStepDone(1, "Normalizing audio", true);
    }

    // Render based on canvas mode:
    // Mode 0 (landscape) = merged video only
    // Mode 1/2/3 (vertical/left split/right split) = vertical video only
    bool isVerticalMode = m_opts.canvasMode >= 1;

    // Step 2: Merged landscape video
    emit processingProgress(2, 0, "Merging video & audio");
    if (hasScreen && !isVerticalMode) {
        m_mergedFile = m_outputDir + "/" + Merger::outputFileName(m_opts.number, m_opts.title);
        qint64 durationUs = Merger::getVideoDurationUs(m_screenFile);

        Merger::MergeInputs in{m_screenFile, audioToUse, m_webcamFile, m_opts};
        QStringList args = Merger::buildMergedArgs(in, m_mergedFile);

        int exitCode = Merger::runFFmpegWithProgress(args, durationUs, [this](int pct) {
            emit processingProgress(2, pct, "Merging video & audio");
        });
        if (exitCode == 0) {
            emit processingProgress(2, 100, "Merging video & audio");
            emit processingStepDone(2, "Merging video & audio", false);
        } else {
            emit processingStepError(2, "Merging", "FFmpeg merge failed");
        }
    } else {
        emit processingStepDone(2, "Merging video & audio", !isVerticalMode ? true : true);
    }

    // Step 3: Vertical video
    emit processingProgress(3, 0, "Creating vertical video");
    if (hasScreen && isVerticalMode) {
        QString vertFile = m_outputDir + "/" + Merger::outputFileName(m_opts.number, m_opts.title, "vertical");
        qint64 durationUs = Merger::getVideoDurationUs(m_screenFile);

        Merger::MergeInputs in{m_screenFile, audioToUse, m_webcamFile, m_opts};
        QStringList args = Merger::buildVerticalArgs(in, vertFile);

        int exitCode = Merger::runFFmpegWithProgress(args, durationUs, [this](int pct) {
            emit processingProgress(3, pct, "Creating vertical video");
        });
        if (exitCode == 0) {
            emit processingProgress(3, 100, "Creating vertical video");
            emit processingStepDone(3, "Creating vertical video", false);
        } else {
            emit processingStepError(3, "Creating vertical video", "FFmpeg vertical video failed");
        }
    } else {
        emit processingStepDone(3, "Creating vertical video", true);
    }

    writeRecordingJson("completed");
    m_processing = false;
    emit processingFinished(true);
}
