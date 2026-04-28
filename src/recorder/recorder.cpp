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

qint64 Recorder::getVideoDurationUs(const QString &filePath) {
    QProcess probe;
    probe.start("ffprobe", {"-v", "error", "-show_entries", "format=duration",
                            "-of", "default=noprint_wrappers=1:nokey=1", filePath});
    probe.waitForFinished(10000);
    QString out = probe.readAllStandardOutput().trimmed();
    double secs = out.toDouble();
    return static_cast<qint64>(secs * 1000000.0);
}

int Recorder::runFFmpegWithProgress(const QStringList &args, int step, const QString &stepName, qint64 durationUs) {
    QProcess proc;
    QStringList fullArgs = args;
    fullArgs.insert(1, "-progress"); // after -y
    fullArgs.insert(2, "pipe:1");
    fullArgs.insert(3, "-stats_period");
    fullArgs.insert(4, "0.5");

    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start("ffmpeg", fullArgs);
    proc.waitForStarted(5000);

    while (proc.state() != QProcess::NotRunning) {
        proc.waitForReadyRead(500);
        while (proc.canReadLine()) {
            QString line = proc.readLine().trimmed();
            if (line.startsWith("out_time_us=")) {
                qint64 timeUs = line.mid(12).toLongLong();
                if (durationUs > 0) {
                    int pct = qBound(0, static_cast<int>(timeUs * 100 / durationUs), 99);
                    emit processingProgress(step, pct, stepName);
                }
            }
        }
    }
    proc.waitForFinished(-1);
    return proc.exitCode();
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

    // Determine audio file to use (may become normalized version)
    QString audioToUse = m_audioFile;
    QString normalizedAudio = m_outputDir + "/audio_normalized.wav";

    // Step 0: Analyze audio (loudnorm pass 1)
    emit processingProgress(0, 0, "Analyzing audio");
    QString loudnormParams; // will hold measured_* params for pass 2
    if (hasAudio) {
        // Pass 1: measure loudness
        QProcess analyze;
        QStringList args;
        args << "-y" << "-i" << m_audioFile
             << "-af" << "loudnorm=I=-18:TP=-1.5:LRA=11:print_format=json"
             << "-f" << "null" << "-";
        analyze.start("ffmpeg", args);
        analyze.waitForFinished(-1);

        QString output = analyze.readAllStandardError();
        // Parse measured values from JSON output
        auto extractVal = [&output](const QString &key) -> QString {
            int idx = output.indexOf("\"" + key + "\"");
            if (idx < 0) return {};
            int colon = output.indexOf(':', idx);
            int quote1 = output.indexOf('"', colon);
            int quote2 = output.indexOf('"', quote1 + 1);
            if (quote1 < 0 || quote2 < 0) return {};
            return output.mid(quote1 + 1, quote2 - quote1 - 1);
        };

        QString measI = extractVal("input_i");
        QString measTP = extractVal("input_tp");
        QString measLRA = extractVal("input_lra");
        QString measThresh = extractVal("input_thresh");
        QString measOffset = extractVal("target_offset");

        if (!measI.isEmpty()) {
            loudnormParams = QString("loudnorm=I=-18:TP=-1.5:LRA=11:"
                "measured_I=%1:measured_TP=%2:measured_LRA=%3:"
                "measured_thresh=%4:offset=%5:linear=true")
                .arg(measI, measTP, measLRA, measThresh, measOffset);
        }

        emit processingProgress(0, 100, "Analyzing audio");
        emit processingStepDone(0, "Analyzing audio", false);
    } else {
        emit processingStepDone(0, "Analyzing audio", true);
    }

    // Step 1: Normalize audio (loudnorm pass 2)
    emit processingProgress(1, 0, "Normalizing audio");
    if (hasAudio && !loudnormParams.isEmpty()) {
        QProcess norm;
        QStringList args;
        args << "-y" << "-i" << m_audioFile
             << "-af" << loudnormParams
             << "-ar" << "48000" << "-ac" << "2"
             << normalizedAudio;

        qDebug() << "Normalizing audio:" << args;
        norm.start("ffmpeg", args);
        norm.waitForFinished(-1);

        if (norm.exitCode() == 0 && QFile::exists(normalizedAudio)) {
            audioToUse = normalizedAudio;
            emit processingProgress(1, 100, "Normalizing audio");
            emit processingStepDone(1, "Normalizing audio", false);
        } else {
            qWarning() << "Normalization failed, using raw audio";
            emit processingStepDone(1, "Normalizing audio", true);
        }
    } else {
        emit processingStepDone(1, "Normalizing audio", true);
    }

    // Step 2: Merge video + audio
    emit processingProgress(2, 0, "Merging video & audio");
    if (hasScreen) {
        QString titleSlug = m_opts.title.toLower().replace(QRegularExpression("[^a-z0-9]+"), "_").trimmed();
        if (titleSlug.isEmpty()) titleSlug = "recording";
        m_mergedFile = m_outputDir + "/" +
            QString("%1-%2.mp4").arg(m_opts.number, 3, 10, QChar('0')).arg(titleSlug);

        qint64 durationUs = getVideoDurationUs(m_screenFile);

        QStringList args;
        args << "-y" << "-i" << m_screenFile;
        if (hasAudio) args << "-i" << audioToUse;
        args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "18" << "-r" << "30";
        if (hasAudio) {
            args << "-c:a" << "aac" << "-b:a" << "320k" << "-shortest";
        } else {
            args << "-an";
        }
        args << m_mergedFile;

        qDebug() << "Merging:" << args;
        int exitCode = runFFmpegWithProgress(args, 2, "Merging video & audio", durationUs);

        if (exitCode == 0) {
            emit processingProgress(2, 100, "Merging video & audio");
            emit processingStepDone(2, "Merging video & audio", false);
        } else {
            emit processingStepError(2, "Merging", "FFmpeg merge failed");
        }
    } else {
        emit processingStepDone(2, "Merging video & audio", true);
    }

    // Step 3: Create vertical video
    emit processingProgress(3, 0, "Creating vertical video");
    if (hasScreen) {
        createVerticalVideo(audioToUse, hasAudio);
    } else {
        emit processingStepDone(3, "Creating vertical video", true);
    }

    // Write final recording.json
    writeRecordingJson("completed");

    emit processingFinished(true);
}

void Recorder::createVerticalVideo(const QString &audioFile, bool hasAudio) {
    QString titleSlug = m_opts.title.toLower().replace(QRegularExpression("[^a-z0-9]+"), "_").trimmed();
    if (titleSlug.isEmpty()) titleSlug = "recording";
    QString vertFile = m_outputDir + "/" +
        QString("%1-%2-vertical.mp4").arg(m_opts.number, 3, 10, QChar('0')).arg(titleSlug);

    bool hasWebcam = QFile::exists(m_webcamFile);

    // Build filter_complex for 1080x1920 (9:16) vertical video
    // Layout: screen at top, webcam below (if present), title at bottom
    QString filter;
    int inputIdx = 0;
    int screenIdx = inputIdx++;
    int webcamIdx = hasWebcam ? inputIdx++ : -1;
    int audioIdx = hasAudio ? inputIdx++ : -1;
    Q_UNUSED(audioIdx);

    // Scale screen to fit 1080 width, maintaining aspect ratio
    filter += QString("[%1:v]scale=1080:-2,setsar=1[screen];").arg(screenIdx);

    if (hasWebcam) {
        // Scale webcam to 250x250 circle overlay
        filter += QString("[%1:v]scale=250:250,setsar=1[wcam];").arg(webcamIdx);

        // Stack screen on black 1080x1920 canvas
        filter += "[screen]pad=1080:1920:(ow-iw)/2:0:black[canvas];";

        // Create circular mask for webcam
        filter += "color=black:250x250[cmask];";
        filter += "[cmask]drawbox=x=0:y=0:w=250:h=250:c=white@1:t=fill[mask_bg];";
        filter += "color=white:250x250[circle_fg];";
        filter += "[circle_fg]geq=lum='if(lt(hypot(X-125,Y-125),125),255,0)':cb=128:cr=128[circle_mask];";
        filter += "[mask_bg][circle_mask]overlay=0:0[alpha_mask];";

        // Apply mask to webcam
        filter += "[wcam][alpha_mask]alphamerge[wcam_circle];";

        // Overlay webcam on canvas (bottom-right with margin)
        filter += "[canvas][wcam_circle]overlay=W-w-20:H-w-200[comp];";

        // Add title text
        filter += QString("[comp]drawtext=text='%1':fontsize=36:fontcolor=white:"
            "x=(w-text_w)/2:y=h-100:fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf[outv]")
            .arg(m_opts.title.replace("'", "\\'"));
    } else {
        // No webcam: just screen on canvas with title
        filter += "[screen]pad=1080:1920:(ow-iw)/2:0:black[canvas];";
        filter += QString("[canvas]drawtext=text='%1':fontsize=36:fontcolor=white:"
            "x=(w-text_w)/2:y=h-100:fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf[outv]")
            .arg(m_opts.title.replace("'", "\\'"));
    }

    qint64 durationUs = getVideoDurationUs(m_screenFile);

    QStringList args;
    args << "-y" << "-i" << m_screenFile;
    if (hasWebcam) args << "-i" << m_webcamFile;
    if (hasAudio) args << "-i" << audioFile;
    args << "-filter_complex" << filter
         << "-map" << "[outv]";
    if (hasAudio) {
        args << "-map" << QString("%1:a").arg(hasWebcam ? 2 : 1);
        args << "-c:a" << "aac" << "-b:a" << "320k" << "-shortest";
    } else {
        args << "-an";
    }
    args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "18" << "-r" << "30"
         << "-s" << "1080x1920"
         << vertFile;

    qDebug() << "Creating vertical:" << args;
    int exitCode = runFFmpegWithProgress(args, 3, "Creating vertical video", durationUs);

    if (exitCode == 0) {
        emit processingProgress(3, 100, "Creating vertical video");
        emit processingStepDone(3, "Creating vertical video", false);
    } else {
        emit processingStepError(3, "Creating vertical video", "FFmpeg vertical video failed");
    }
}
