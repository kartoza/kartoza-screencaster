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
    root["app_version"] = "0.9.0";
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
    if (!m_opts.leftLogo.isEmpty()) settings["left_logo"] = m_opts.leftLogo;
    if (!m_opts.rightLogo.isEmpty()) settings["right_logo"] = m_opts.rightLogo;
    if (!m_opts.bannerLogo.isEmpty()) settings["banner_logo"] = m_opts.bannerLogo;
    settings["title_color"] = m_opts.titleColor;
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

void Recorder::renameOutputFolder() {
    QString titleSlug = m_opts.title.toLower()
        .replace(QRegularExpression("[^a-z0-9]+"), "_")
        .replace(QRegularExpression("^_+|_+$"), ""); // trim leading/trailing underscores
    if (titleSlug.isEmpty()) return; // keep timestamp name

    QString parentDir = QFileInfo(m_outputDir).absolutePath();
    QString newName = QString("%1-%2")
        .arg(m_opts.number, 3, 10, QChar('0'))
        .arg(titleSlug);
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
    if (!m_recording && !m_paused) return;

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

    // Update recording.json with processing status
    writeRecordingJson("processing");

    emit recordingStopped();

    // Start processing in a worker thread
    int waitMs = wasPaused ? 500 : 2000;
    QThread *thread = QThread::create([this, waitMs]() {
        QThread::msleep(waitMs);
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

QString Recorder::concatenateParts(const QStringList &parts, const QString &outputFile) {
    if (parts.isEmpty()) return {};
    if (parts.size() == 1) return parts.first(); // no concat needed

    // Filter to only existing files
    QStringList existing;
    for (const auto &p : parts) {
        if (QFile::exists(p)) existing.append(p);
    }
    if (existing.isEmpty()) return {};
    if (existing.size() == 1) return existing.first();

    // Write concat list file
    QString listFile = m_outputDir + "/concat_list.txt";
    QFile list(listFile);
    if (!list.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    for (const auto &p : existing) {
        list.write(QString("file '%1'\n").arg(p).toUtf8());
    }
    list.close();

    // Run ffmpeg concat
    QProcess ffmpeg;
    QStringList args;
    args << "-y" << "-f" << "concat" << "-safe" << "0"
         << "-i" << listFile << "-c" << "copy" << outputFile;

    qDebug() << "Concatenating" << existing.size() << "parts to" << outputFile;
    ffmpeg.start("ffmpeg", args);
    ffmpeg.waitForFinished(-1);

    QFile::remove(listFile);

    if (ffmpeg.exitCode() == 0 && QFile::exists(outputFile)) {
        return outputFile;
    }
    qWarning() << "Concat failed:" << ffmpeg.readAllStandardError().left(200);
    return existing.first(); // fallback to first part
}

void Recorder::processRecordings() {
    emit processingStarted();

    // Concatenate parts if we had pause/resume cycles
    if (m_screenParts.size() > 1) {
        QString concat = concatenateParts(m_screenParts, m_outputDir + "/screen_combined.mp4");
        if (!concat.isEmpty()) m_screenFile = concat;
    } else if (!m_screenParts.isEmpty()) {
        m_screenFile = m_screenParts.first();
    }
    if (m_audioParts.size() > 1) {
        QString concat = concatenateParts(m_audioParts, m_outputDir + "/audio_combined.wav");
        if (!concat.isEmpty()) m_audioFile = concat;
    } else if (!m_audioParts.isEmpty()) {
        m_audioFile = m_audioParts.first();
    }
    if (m_webcamParts.size() > 1) {
        QString concat = concatenateParts(m_webcamParts, m_outputDir + "/webcam_combined.mp4");
        if (!concat.isEmpty()) m_webcamFile = concat;
    } else if (!m_webcamParts.isEmpty()) {
        m_webcamFile = m_webcamParts.first();
    }

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

        // Collect overlay inputs
        bool hasLeftLogo = !m_opts.leftLogo.isEmpty() && QFile::exists(m_opts.leftLogo);
        bool hasRightLogo = !m_opts.rightLogo.isEmpty() && QFile::exists(m_opts.rightLogo);
        bool hasBannerLogo = !m_opts.bannerLogo.isEmpty() && QFile::exists(m_opts.bannerLogo);
        bool mergeWebcam = hasWebcam && !m_opts.noScreen; // overlay webcam only when screen is primary
        bool needsFilter = hasLeftLogo || hasRightLogo || hasBannerLogo || mergeWebcam;

        QStringList args;
        args << "-y" << "-i" << m_screenFile;
        int nextInput = 1;
        int audioInput = -1, webcamInput = -1;
        if (hasAudio) { audioInput = nextInput++; args << "-i" << audioToUse; }
        if (mergeWebcam) { webcamInput = nextInput++; args << "-i" << m_webcamFile; }
        int leftLogoInput = -1, rightLogoInput = -1, bannerLogoInput = -1;
        if (hasLeftLogo) { leftLogoInput = nextInput++; args << "-i" << m_opts.leftLogo; }
        if (hasRightLogo) { rightLogoInput = nextInput++; args << "-i" << m_opts.rightLogo; }
        if (hasBannerLogo) { bannerLogoInput = nextInput++; args << "-i" << m_opts.bannerLogo; }

        if (needsFilter) {
            QString filter;
            QString current = "[0:v]";
            QString logoEnable = "enable='between(t,0,15)'";
            int vIdx = 0;

            // Logo overlays (first 15 seconds)
            if (hasLeftLogo) {
                filter += QString("[%1:v]scale=iw*1/8:-1[ll];").arg(leftLogoInput);
                filter += QString("%1[ll]overlay=10:10:%2[v%3];").arg(current, logoEnable).arg(++vIdx);
                current = QString("[v%1]").arg(vIdx);
            }
            if (hasRightLogo) {
                filter += QString("[%1:v]scale=iw*1/8:-1[rl];").arg(rightLogoInput);
                filter += QString("%1[rl]overlay=W-w-10:10:%2[v%3];").arg(current, logoEnable).arg(++vIdx);
                current = QString("[v%1]").arg(vIdx);
            }
            if (hasBannerLogo) {
                filter += QString("[%1:v]scale=iw*1/2:-1[bl];").arg(bannerLogoInput);
                filter += QString("%1[bl]overlay=10:H-h-10:%2[v%3];").arg(current, logoEnable).arg(++vIdx);
                current = QString("[v%1]").arg(vIdx);
            }

            // Circular webcam overlay (full duration, bottom-right, 250px)
            if (mergeWebcam) {
                filter += QString("[%1:v]scale=250:250,setsar=1[wcam];").arg(webcamInput);
                filter += "color=black:250x250,geq=lum='if(lt(hypot(X-125,Y-125),125),255,0)':cb=128:cr=128[cmask];";
                filter += "[wcam][cmask]alphamerge[wcam_c];";
                filter += QString("%1[wcam_c]overlay=W-w-20:H-h-20[v%2];").arg(current).arg(++vIdx);
                current = QString("[v%1]").arg(vIdx);
            }

            // Rename final output
            filter.chop(1); // remove trailing ';'
            int lastBracket = filter.lastIndexOf('[');
            filter = filter.left(lastBracket) + "[outv]";

            args << "-filter_complex" << filter << "-map" << "[outv]";
            if (hasAudio) args << "-map" << QString("%1:a").arg(audioInput);
        }

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
    bool hasLeftLogo = !m_opts.leftLogo.isEmpty() && QFile::exists(m_opts.leftLogo);
    bool hasRightLogo = !m_opts.rightLogo.isEmpty() && QFile::exists(m_opts.rightLogo);
    bool hasBannerLogo = !m_opts.bannerLogo.isEmpty() && QFile::exists(m_opts.bannerLogo);

    QString titleColor = m_opts.titleColor.isEmpty() ? "white" : m_opts.titleColor;
    QString escapedTitle = m_opts.title;
    escapedTitle.replace("'", "\\'");
    escapedTitle.replace(":", "\\:");

    // Build input list and track indices
    QStringList args;
    args << "-y" << "-i" << m_screenFile;
    int nextInput = 1;
    int webcamInput = -1, audioInput = -1;
    int leftLogoIn = -1, rightLogoIn = -1, bannerLogoIn = -1;

    if (hasWebcam) { webcamInput = nextInput++; args << "-i" << m_webcamFile; }
    if (hasAudio) { audioInput = nextInput++; args << "-i" << audioFile; }
    if (hasLeftLogo) { leftLogoIn = nextInput++; args << "-i" << m_opts.leftLogo; }
    if (hasRightLogo) { rightLogoIn = nextInput++; args << "-i" << m_opts.rightLogo; }
    if (hasBannerLogo) { bannerLogoIn = nextInput++; args << "-i" << m_opts.bannerLogo; }

    // Build filter_complex for 1080x1920 (9:16)
    // Layout: screen at top, white lower third at y=1280, logos in lower third, title at bottom
    QString f;

    // Scale screen to 1080 width
    f += "[0:v]scale=1080:-2,setsar=1[screen];";

    // Create canvas with white lower third
    f += "[screen]pad=1080:1920:(ow-iw)/2:0:black[padded];";
    f += "[padded]drawbox=y=1280:w=1080:h=640:c=white:t=fill[canvas];";

    QString current = "[canvas]";

    // Webcam circle overlay (bottom-right of screen area)
    if (hasWebcam) {
        f += QString("[%1:v]scale=250:250,setsar=1[wcam];").arg(webcamInput);
        f += "color=black:250x250,geq=lum='if(lt(hypot(X-125,Y-125),125),255,0)':cb=128:cr=128[cmask];";
        f += "[wcam][cmask]alphamerge[wcam_c];";
        f += QString("%1[wcam_c]overlay=W-w-20:1020[wc_out];").arg(current);
        current = "[wc_out]";
    }

    // Left logo in lower third (top-left of bottom area)
    if (hasLeftLogo) {
        f += QString("[%1:v]scale=360:-1[ll];").arg(leftLogoIn);
        f += QString("%1[ll]overlay=10:1290[ll_out];").arg(current);
        current = "[ll_out]";
    }

    // Right logo in lower third (top-right of bottom area)
    if (hasRightLogo) {
        f += QString("[%1:v]scale=360:-1[rl];").arg(rightLogoIn);
        f += QString("%1[rl]overlay=W-w-10:1290[rl_out];").arg(current);
        current = "[rl_out]";
    }

    // Banner logo (full width, centered above title)
    if (hasBannerLogo) {
        f += QString("[%1:v]scale=1080:-1[bl];").arg(bannerLogoIn);
        f += QString("%1[bl]overlay=(W-w)/2:1650[bl_out];").arg(current);
        current = "[bl_out]";
    }

    // Title text centered near bottom
    f += QString("%1drawtext=text='%2':fontsize=36:fontcolor=%3:"
        "x=(w-text_w)/2:y=1850[outv]")
        .arg(current, escapedTitle, titleColor);

    qint64 durationUs = getVideoDurationUs(m_screenFile);

    args << "-filter_complex" << f
         << "-map" << "[outv]";
    if (hasAudio) {
        args << "-map" << QString("%1:a").arg(audioInput);
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
