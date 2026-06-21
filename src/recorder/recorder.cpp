#include "recorder/recorder.h"
#include "config/config.h"
#include "merger/merger.h"
#include "monitor/monitor.h"
#include "platform/platform.h"
#ifdef HAS_DBUS
#include "portal/portal.h"
#endif
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDateTime>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QHostInfo>
#include <QRegularExpression>
#ifndef Q_OS_WIN
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

Recorder::Recorder(QObject *parent) : QObject(parent) {
#ifdef HAS_DBUS
    // GNOME/KDE Wayland recording path is async: startScreenRecorder
    // fires Portal::requestScreenCast() and returns, the user takes
    // however long they take to authorise the source picker, and the
    // portal then emits screenCastReady() / screenCastFailed() back to
    // us. We can't connect these lambdas inside startScreenRecorder
    // because they'd be re-connected every time and we'd get duplicate
    // gst-launch processes.
    connect(&Portal::instance(), &Portal::screenCastReady, this,
            &Recorder::onScreenCastReady);
    connect(&Portal::instance(), &Portal::screenCastFailed, this,
            &Recorder::onScreenCastFailed);
#endif
}

Recorder::~Recorder() {
    // Disconnect all signals to prevent delivery to destroyed slots
    disconnect();

    if (m_recording) {
        m_recording = false;
        stopAllProcesses();
    }
    // Wait for processing thread to finish — it accesses our members
    if (m_processingThread) {
        // Disconnect the thread's finished signal to prevent deleteLater conflict
        m_processingThread->disconnect(this);
        if (m_processingThread->isRunning()) {
            m_cancelRequested = true;
            m_processingThread->wait(10000);
        }
        delete m_processingThread;
        m_processingThread = nullptr;
    }
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
    m_partTimestamps.clear();

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

    // Copy assets (logos, GIFs, sounds) into the output directory
    copyAssetsToOutputDir();

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

    PartTimestamps ts;

    if (!m_opts.noScreen) {
        m_screenFile = m_outputDir + "/screen" + partSuffix + ".mp4";
        m_screenParts.append(m_screenFile);
        startScreenRecorder(m_opts);
        ts.screenStartMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (!m_opts.noAudio) {
        m_audioFile = m_outputDir + "/audio" + partSuffix + ".wav";
        m_audioParts.append(m_audioFile);
        startAudioRecorder(m_opts);
        ts.audioStartMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (!m_opts.noWebcam && !m_opts.webcamDevice.isEmpty()) {
        m_webcamFile = m_outputDir + "/webcam" + partSuffix + ".mp4";
        m_webcamParts.append(m_webcamFile);
        startWebcamRecorder(m_opts);
        ts.webcamStartMs = QDateTime::currentMSecsSinceEpoch();
    }

    m_partTimestamps.append(ts);
}

void Recorder::copyAssetsToOutputDir() {
    QDir assetsDir(m_outputDir + "/assets");
    assetsDir.mkpath(".");

    // Copy logo files
    for (const auto &logo : m_opts.logos) {
        if (!logo.path.isEmpty() && QFile::exists(logo.path)) {
            QString dest = assetsDir.filePath(QFileInfo(logo.path).fileName());
            if (!QFile::exists(dest))
                QFile::copy(logo.path, dest);
        }
    }

    // Copy start sound
    if (!m_opts.startSound.isEmpty() && QFile::exists(m_opts.startSound)) {
        QString dest = assetsDir.filePath(QFileInfo(m_opts.startSound).fileName());
        if (!QFile::exists(dest))
            QFile::copy(m_opts.startSound, dest);
    }

    // Copy end sound
    if (!m_opts.endSound.isEmpty() && QFile::exists(m_opts.endSound)) {
        QString dest = assetsDir.filePath(QFileInfo(m_opts.endSound).fileName());
        if (!QFile::exists(dest))
            QFile::copy(m_opts.endSound, dest);
    }
}

void Recorder::stopAllProcesses() {
    stopProcess(m_screenProc);
    stopProcess(m_audioProc);
    stopProcess(m_webcamProc);

    if (m_screenProc) { m_screenProc->deleteLater(); m_screenProc = nullptr; }
    if (m_audioProc) { m_audioProc->deleteLater(); m_audioProc = nullptr; }
    if (m_webcamProc) { m_webcamProc->deleteLater(); m_webcamProc = nullptr; }

#ifdef HAS_DBUS
    // Close the portal screencast session so the compositor stops the stream
    // and the green "screen sharing" indicator goes away.
    Portal::instance().stopScreenCast();
#endif
}

void Recorder::writeRecordingJson(const QString &status) {
    QJsonObject root;
    root["status"] = status;
    root["start_time"] = m_startTime.toString(Qt::ISODate);
    root["app_version"] = QCoreApplication::instance()
        ? QCoreApplication::applicationVersion() : QString(APP_VERSION);
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

    // Stream start timestamps for sync alignment
    if (!m_partTimestamps.isEmpty()) {
        QJsonArray tsArr;
        for (const auto &ts : m_partTimestamps) {
            QJsonObject tsObj;
            tsObj["screen_start_ms"] = ts.screenStartMs;
            tsObj["audio_start_ms"] = ts.audioStartMs;
            tsObj["webcam_start_ms"] = ts.webcamStartMs;
            tsArr.append(tsObj);
        }
        files["part_timestamps"] = tsArr;
    }

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
    if (!m_verticalFile.isEmpty() && QFile::exists(m_verticalFile)) {
        files["vertical_file"] = m_verticalFile;
        files["vertical_size"] = QFileInfo(m_verticalFile).size();
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
    // Logos array
    QJsonArray logosArr;
    for (const auto &logo : m_opts.logos) {
        if (logo.path.isEmpty()) continue;
        QJsonObject lo;
        lo["path"] = logo.path;
        lo["gif_loop"] = logo.gifLoop;
        lo["gif_loop_max"] = logo.gifLoopMax;
        lo["rel_x"] = logo.relX;
        lo["rel_y"] = logo.relY;
        lo["rel_w"] = logo.relW;
        lo["rel_h"] = logo.relH;
        logosArr.append(lo);
    }
    if (!logosArr.isEmpty()) settings["logos"] = logosArr;
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
    QString cmd;
    QStringList args;

    switch (Platform::os()) {
    case Platform::OS::Linux:
        if (Platform::isWayland()) {
            if (Platform::supportsWlrCapture()) {
                // wlroots compositors (Hyprland, Sway, COSMIC) — native tool.
                cmd = "wl-screenrec";
                if (!opts.hwAccel) args << "--no-hw";
                args << QString("--output=%1").arg(opts.monitor)
                     << QString("--filename=%1").arg(m_screenFile);
                break;
            }

#ifdef HAS_DBUS
            // GNOME/KDE Wayland — request a portal screencast session
            // asynchronously. Portal::requestScreenCast returns
            // immediately; the actual gst-launch process is spawned
            // later in onScreenCastReady() once the user has clicked
            // through the source picker. We pre-allocated m_screenProc
            // above so subsequent code paths that probe its existence
            // still see "a recording is set up". If the session is
            // already active (multi-part / resume), onScreenCastReady
            // will fire synchronously from inside requestScreenCast
            // and we'll spawn gst-launch before this method returns.
            Portal::instance().requestScreenCast();
            return;
#else
            emit recordingError(
                "This build has no D-Bus support — cannot use the "
                "xdg-desktop-portal recording fallback required on "
                "GNOME/KDE Wayland.");
            delete m_screenProc;
            m_screenProc = nullptr;
            return;
#endif
        } else {
            // ffmpeg x11grab for X11
            cmd = "ffmpeg";
            QString display = qEnvironmentVariable("DISPLAY", ":0");

            // Look up monitor geometry by name via xrandr
            int monW = 1920, monH = 1080, monX = 0, monY = 0;
            auto monitors = Monitor::listMonitors();
            for (const auto &mon : monitors) {
                if (mon.name == opts.monitor || opts.monitor == "default") {
                    monW = mon.width;
                    monH = mon.height;
                    monX = mon.x;
                    monY = mon.y;
                    break;
                }
            }

            args << "-y" << "-f" << "x11grab"
                 << "-framerate" << "30"
                 << "-video_size" << QString("%1x%2").arg(monW).arg(monH)
                 << "-i" << QString("%1+%2,%3").arg(display).arg(monX).arg(monY)
                 << "-c:v" << "libx264" << "-preset" << "ultrafast"
                 << "-crf" << "18" << "-pix_fmt" << "yuv420p"
                 << m_screenFile;
        }
        break;

    case Platform::OS::macOS:
        // ffmpeg avfoundation - screen device is typically "1" or "Capture screen 0"
        cmd = "ffmpeg";
        args << "-y" << "-f" << "avfoundation"
             << "-framerate" << "30"
             << "-capture_cursor" << "1"
             << "-i" << (opts.monitor.isEmpty() ? "1" : opts.monitor) + ":"
             << "-c:v" << "libx264" << "-preset" << "ultrafast"
             << "-crf" << "18" << "-pix_fmt" << "yuv420p"
             << m_screenFile;
        break;

    case Platform::OS::Windows:
        // ffmpeg gdigrab
        cmd = "ffmpeg";
        args << "-y" << "-f" << "gdigrab"
             << "-framerate" << "30"
             << "-i" << "desktop"
             << "-c:v" << "libx264" << "-preset" << "ultrafast"
             << "-crf" << "18" << "-pix_fmt" << "yuv420p"
             << m_screenFile;
        break;

    default:
        emit recordingError("Unsupported platform for screen recording");
        delete m_screenProc;
        m_screenProc = nullptr;
        return;
    }

    qDebug() << "Starting screen recorder:" << cmd << args;
    connect(m_screenProc, &QProcess::readyReadStandardError, this, [this]() {
        qDebug() << "Screen recorder stderr:" << m_screenProc->readAllStandardError();
    });
    m_screenProc->start(cmd, args);

    if (!m_screenProc->waitForStarted(5000)) {
        QString err = "Failed to start screen recorder: " + m_screenProc->errorString();
        qDebug() << err;
        emit recordingError(err);
    }
}

#ifdef HAS_DBUS
void Recorder::onScreenCastReady(uint nodeId, int fd) {
    // Only act if a recording is set up but the screen process hasn't
    // been spawned yet — guards against stray signals (e.g. an old
    // session that's still alive when the user toggles options).
    if (!m_screenProc || m_screenProc->state() != QProcess::NotRunning) {
        return;
    }
    if (m_screenFile.isEmpty()) {
        return;
    }

    // pipewiresrc must read via the portal's private PipeWire connection
    // (the default socket sees the node but the screencast permission
    // grant is bound to this FD). QProcess closes non-stdio fds in the
    // forked child by default, so we dup2 onto a fixed slot in the
    // child-process modifier before exec().
    constexpr int kPwChildFd = 23;
    int portalFd = fd;
    m_screenProc->setChildProcessModifier([portalFd, kPwChildFd]() {
        if (::dup2(portalFd, kPwChildFd) >= 0) {
            int f = ::fcntl(kPwChildFd, F_GETFD);
            if (f >= 0) ::fcntl(kPwChildFd, F_SETFD, f & ~FD_CLOEXEC);
        }
    });

    // Encoder choice: `openh264enc` from gst-plugins-bad. Cisco's
    // OpenH264 is the only H.264 encoder that's reliably present in
    // our nixpkgs dev shell — gst-plugins-ugly (x264enc) isn't loaded
    // and gst-libav (avenc_libx264) is built without libx264 there.
    // OpenH264 is lower quality than libx264 at the same bitrate but
    // is fine for screen recording. bitrate is in bits/sec.
    QString cmd = "gst-launch-1.0";
    QStringList args;
    args << "-e"  // EOS on SIGINT so mp4mux finalises the moov atom
         << "pipewiresrc"
         << QString("path=%1").arg(nodeId)
         << QString("fd=%1").arg(kPwChildFd)
         << "do-timestamp=true"
         << "!" << "videoconvert"
         << "!" << "videorate"
         << "!" << "video/x-raw,framerate=30/1"
         << "!" << "openh264enc" << "bitrate=8000000"
                                 << "complexity=medium"
                                 << "rate-control=bitrate"
         << "!" << "h264parse"
         << "!" << "mp4mux"
         << "!" << "filesink" << QString("location=%1").arg(m_screenFile);

    qDebug() << "Starting portal screen recorder:" << cmd << args;
    connect(m_screenProc, &QProcess::readyReadStandardError, this, [this]() {
        qDebug() << "Screen recorder stderr:"
                 << m_screenProc->readAllStandardError();
    });
    m_screenProc->start(cmd, args);
    if (!m_screenProc->waitForStarted(5000)) {
        emit recordingError("Failed to start gst-launch: "
                            + m_screenProc->errorString());
    }
}

void Recorder::onScreenCastFailed(const QString &reason) {
    // Only react if we're mid-startup waiting for the portal. After a
    // successful session that later closes (e.g. user revokes
    // permission) the recorder will see the gst-launch process exit
    // and surface that separately.
    if (!m_screenProc || m_screenProc->state() != QProcess::NotRunning) {
        return;
    }
    emit recordingError("Portal screencast unavailable: " + reason);
    delete m_screenProc;
    m_screenProc = nullptr;
}
#endif

void Recorder::startAudioRecorder(const RecordingOptions &opts) {
    m_audioProc = new QProcess(this);

    QString device = opts.audioDevice;
    QStringList args;

    switch (Platform::os()) {
    case Platform::OS::Linux:
        if (device.isEmpty()) device = "@DEFAULT_SOURCE@";
        args << "-y" << "-f" << "pulse"
             << "-i" << device
             << "-ac" << "2" << m_audioFile;
        break;

    case Platform::OS::macOS:
        // avfoundation audio - "0" is default mic, or use device name
        if (device.isEmpty()) device = "0";
        args << "-y" << "-f" << "avfoundation"
             << "-i" << (":" + device)
             << "-ac" << "2" << m_audioFile;
        break;

    case Platform::OS::Windows:
        // dshow audio
        if (device.isEmpty()) device = "Microphone";
        args << "-y" << "-f" << "dshow"
             << "-i" << ("audio=" + device)
             << "-ac" << "2" << m_audioFile;
        break;

    default:
        qWarning() << "Unsupported platform for audio recording";
        delete m_audioProc;
        m_audioProc = nullptr;
        return;
    }

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

    int fps = opts.webcamFPS > 0 ? opts.webcamFPS : 30;
    QStringList args;

    switch (Platform::os()) {
    case Platform::OS::Linux:
        args << "-y" << "-f" << "v4l2"
             << "-framerate" << QString::number(fps)
             << "-i" << ("/dev/" + opts.webcamDevice);
        break;

    case Platform::OS::macOS:
        // avfoundation - webcam device index or name
        args << "-y" << "-f" << "avfoundation"
             << "-framerate" << QString::number(fps)
             << "-i" << (opts.webcamDevice + ":");
        break;

    case Platform::OS::Windows:
        // dshow - webcam device name
        args << "-y" << "-f" << "dshow"
             << "-framerate" << QString::number(fps)
             << "-i" << ("video=" + opts.webcamDevice);
        break;

    default:
        qWarning() << "Unsupported platform for webcam recording";
        delete m_webcamProc;
        m_webcamProc = nullptr;
        return;
    }

    // Common encoding options
    args << "-c:v" << "libx264"
         << "-preset" << "ultrafast"
         << "-tune" << "zerolatency"
         << "-crf" << "18"
         << "-pix_fmt" << "yuv420p"
         << m_webcamFile;

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
    updatePath(m_verticalFile);

    for (auto &p : m_screenParts) updatePath(p);
    for (auto &p : m_audioParts) updatePath(p);
    for (auto &p : m_webcamParts) updatePath(p);
}

void Recorder::captureRoomNoise() {
    // 5 seconds is sufficient for noise profiling — ffmpeg's afftdn and
    // anlmdn filters only need ~1-2s to build an accurate noise model.
    static const int DURATION_SECS = 5;
    QString roomNoiseFile = m_outputDir + "/room_noise.wav";

    qDebug() << "Capturing room noise for" << DURATION_SECS << "seconds...";
    emit roomNoiseStarted();

    QString device = m_opts.audioDevice;

    QProcess proc;
    QStringList args;

    switch (Platform::os()) {
    case Platform::OS::Linux:
        if (device.isEmpty()) device = "@DEFAULT_SOURCE@";
        args << "-y" << "-f" << "pulse" << "-i" << device;
        break;
    case Platform::OS::macOS:
        if (device.isEmpty()) device = "0";
        args << "-y" << "-f" << "avfoundation" << "-i" << (":" + device);
        break;
    case Platform::OS::Windows:
        if (device.isEmpty()) device = "Microphone";
        args << "-y" << "-f" << "dshow" << "-i" << ("audio=" + device);
        break;
    default:
        emit roomNoiseFinished();
        return;
    }
    args << "-ac" << "2" << "-t" << QString::number(DURATION_SECS) << roomNoiseFile;

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

#ifndef Q_OS_WIN
    // Send SIGINT for graceful ffmpeg shutdown (POSIX only)
    qint64 pid = proc->processId();
    if (pid > 0) {
        ::kill(pid, SIGINT);
    }
#endif
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
    m_processingThread = QThread::create([this, waitMs, hasAudioEnabled]() {
        QThread::msleep(waitMs);

        // Room noise capture (only if audio was recorded)
        if (hasAudioEnabled) {
            captureRoomNoise();
        }

        processRecordings();
    });
    connect(m_processingThread, &QThread::finished, this, [this]() {
        m_processingThread->deleteLater();
        m_processingThread = nullptr;
    });
    m_processingThread->start();
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

    // Read logos — new array format
    m_opts.logos.clear();
    if (settings.contains("logos")) {
        auto logosArr = settings["logos"].toArray();
        for (const auto &val : logosArr) {
            auto lo = val.toObject();
            RecordingOptions::LogoOpts logo;
            logo.path = lo["path"].toString();
            logo.gifLoop = lo["gif_loop"].toInt(2);
            logo.gifLoopMax = lo["gif_loop_max"].toInt(3);
            logo.relX = lo["rel_x"].toDouble(0);
            logo.relY = lo["rel_y"].toDouble(0);
            logo.relW = lo["rel_w"].toDouble(0.15);
            logo.relH = lo["rel_h"].toDouble(0.15);
            m_opts.logos.append(logo);
        }
    }
    // Migration: old left_logo/right_logo/banner_logo format
    if (m_opts.logos.isEmpty()) {
        auto addLegacy = [&](const QString &key, double defX, double defY) {
            if (settings.contains(key)) {
                RecordingOptions::LogoOpts logo;
                logo.path = settings[key].toString();
                logo.relX = defX;
                logo.relY = defY;
                if (key == "banner_logo") {
                    logo.gifLoop = settings["banner_gif_loop"].toInt(2);
                    logo.gifLoopMax = settings["banner_gif_loop_max"].toInt(3);
                }
                m_opts.logos.append(logo);
            }
        };
        addLegacy("left_logo", 0.01, 0.01);
        addLegacy("right_logo", 0.85, 0.01);
        addLegacy("banner_logo", 0.4, 0.85);
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
    m_verticalFile.clear();

    // Load sync timestamps from recording.json
    m_partTimestamps.clear();
    auto filesObj = root["files"].toObject();
    if (filesObj.contains("part_timestamps")) {
        auto tsArr = filesObj["part_timestamps"].toArray();
        for (const auto &val : tsArr) {
            auto tsObj = val.toObject();
            PartTimestamps ts;
            ts.screenStartMs = tsObj["screen_start_ms"].toInteger(0);
            ts.audioStartMs = tsObj["audio_start_ms"].toInteger(0);
            ts.webcamStartMs = tsObj["webcam_start_ms"].toInteger(0);
            m_partTimestamps.append(ts);
        }
    }

    // Update status
    writeRecordingJson("processing");
    // Note: do NOT emit recordingStopped() here — the caller (mainwindow reprocess handler)
    // already navigates to the processing page and calls startMonitoring

    m_processingThread = QThread::create([this]() {
        processRecordings();
    });
    connect(m_processingThread, &QThread::finished, this, [this]() {
        m_processingThread->deleteLater();
        m_processingThread = nullptr;
    });
    m_processingThread->start();
}

void Recorder::cancelProcessing() {
    m_cancelRequested = true;
    if (m_processingThread && m_processingThread->isRunning()) {
        m_processingThread->requestInterruption();
    }
}

void Recorder::processRecordings() {
    m_processing = true;
    m_cancelRequested = false;
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

    if (m_cancelRequested) { m_processing = false; emit processingFinished(false); return; }

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

    if (m_cancelRequested) { m_processing = false; emit processingFinished(false); return; }

    // Step 1.5: Denoise and dereverb
    if (hasAudio && Config::instance().denoiseAudio) {
        QString roomNoiseFile = m_outputDir + "/room_noise.wav";
        QString denoisedFile = m_outputDir + "/audio_denoised.wav";
        bool hasRoomNoise = QFile::exists(roomNoiseFile);

        // Build filter chain:
        // - afftdn: adaptive noise reduction (uses noise floor from room noise sample)
        // - highpass at 80Hz: removes low rumble/room resonance
        // - lowpass at 13000Hz: tames harsh high-freq reverb tails
        QString filter;
        if (hasRoomNoise) {
            // Use noise sample for profile-based reduction (nr=20dB, nt=w for wiener filter)
            filter = "afftdn=nf=-25:tn=1:nr=20:nt=w,highpass=f=80,lowpass=f=13000";
        } else {
            // No room noise sample — use adaptive mode with moderate reduction
            filter = "afftdn=nf=-30:nr=12:nt=w,highpass=f=80,lowpass=f=13000";
        }

        QProcess proc;
        QStringList args = {"-y", "-i", audioToUse};
        if (hasRoomNoise) {
            // Feed room noise as a second input for noise profiling
            args << "-i" << roomNoiseFile
                 << "-filter_complex"
                 << QString("[1:a]asplit[noise];[0:a][noise]afftdn=nr=20:nt=w,highpass=f=80,lowpass=f=13000[aout]")
                 << "-map" << "[aout]";
        } else {
            args << "-af" << filter;
        }
        args << "-ar" << "48000" << denoisedFile;

        proc.start("ffmpeg", args);
        if (proc.waitForFinished(60000) && proc.exitCode() == 0) {
            audioToUse = denoisedFile;
            qDebug() << "Applied denoise/dereverb filter";
        } else {
            qDebug() << "Denoise failed, using unprocessed audio:" << proc.readAllStandardError();
        }
    }

    if (m_cancelRequested) { m_processing = false; emit processingFinished(false); return; }

    // Step 1.7: Mix start/end sound effects over the audio track
    bool hasStartSound = !m_opts.startSound.isEmpty() && QFile::exists(m_opts.startSound);
    bool hasEndSound = !m_opts.endSound.isEmpty() && QFile::exists(m_opts.endSound);

    if (hasAudio && (hasStartSound || hasEndSound)) {
        QString withSounds = m_outputDir + "/audio_with_sfx.wav";

        // Get duration of main audio to position end sound
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-show_entries", "format=duration",
                                "-of", "default=noprint_wrappers=1:nokey=1", audioToUse});
        double audioDuration = 0;
        if (probe.waitForFinished(5000))
            audioDuration = QString(probe.readAllStandardOutput().trimmed()).toDouble();

        // Build amix filter: overlay sounds at start (t=0) and end (t=duration-sfx_length)
        QStringList args;
        int inputIdx = 0;
        args << "-y" << "-i" << audioToUse;
        int mainIdx = inputIdx++;

        int startIdx = -1, endIdx = -1;
        if (hasStartSound) { args << "-i" << m_opts.startSound; startIdx = inputIdx++; }
        if (hasEndSound) { args << "-i" << m_opts.endSound; endIdx = inputIdx++; }

        // Get end sound duration to calculate its start offset
        double endSoundDur = 0;
        if (hasEndSound) {
            QProcess ep;
            ep.start("ffprobe", {"-v", "error", "-show_entries", "format=duration",
                                 "-of", "default=noprint_wrappers=1:nokey=1", m_opts.endSound});
            if (ep.waitForFinished(5000))
                endSoundDur = QString(ep.readAllStandardOutput().trimmed()).toDouble();
        }

        // Build filter_complex
        QString filter;
        QString currentMix = QString("[%1:a]").arg(mainIdx);

        if (hasStartSound) {
            // Mix start sound at t=0 over main audio
            filter += QString("[%1:a]adelay=0|0[sfx_start];").arg(startIdx);
            filter += QString("%1[sfx_start]amix=inputs=2:duration=longest:dropout_transition=0[mix1];").arg(currentMix);
            currentMix = "[mix1]";
        }

        if (hasEndSound && audioDuration > 0) {
            // Mix end sound at (duration - end_sound_duration) over main audio
            int delayMs = std::max(0, int((audioDuration - endSoundDur) * 1000));
            filter += QString("[%1:a]adelay=%2|%2[sfx_end];").arg(endIdx).arg(delayMs);
            filter += QString("%1[sfx_end]amix=inputs=2:duration=longest:dropout_transition=0[mix2];").arg(currentMix);
            currentMix = "[mix2]";
        }

        // Output
        filter += currentMix + "aformat=sample_fmts=s16:sample_rates=48000:channel_layouts=mono[aout]";
        args << "-filter_complex" << filter << "-map" << "[aout]" << withSounds;

        QProcess proc;
        proc.start("ffmpeg", args);
        if (proc.waitForFinished(60000) && proc.exitCode() == 0) {
            audioToUse = withSounds;
            qDebug() << "Mixed start/end sounds over audio track";
        } else {
            qDebug() << "Failed to mix sound effects:" << proc.readAllStandardError();
        }
    } else if (!hasAudio && (hasStartSound || hasEndSound)) {
        // No recorded audio — just use the sound effects concatenated
        if (hasStartSound && hasEndSound) {
            QString combined = m_outputDir + "/audio_sfx_only.wav";
            QString concatList = m_outputDir + "/sfx_concat.txt";
            QFile listFile(concatList);
            if (listFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&listFile);
                out << "file '" << m_opts.startSound << "'\n";
                out << "file '" << m_opts.endSound << "'\n";
                listFile.close();
            }
            QProcess proc;
            proc.start("ffmpeg", {"-y", "-f", "concat", "-safe", "0",
                                  "-i", concatList, "-c:a", "pcm_s16le", combined});
            if (proc.waitForFinished(30000) && proc.exitCode() == 0)
                audioToUse = combined;
            QFile::remove(concatList);
        } else {
            audioToUse = hasStartSound ? m_opts.startSound : m_opts.endSound;
        }
    }

    // Compute sync offsets from the first part's timestamps (screen is reference)
    double audioOffsetSec = 0.0;
    double webcamOffsetSec = 0.0;
    if (!m_partTimestamps.isEmpty()) {
        const auto &ts = m_partTimestamps.first();
        if (ts.screenStartMs > 0 && ts.audioStartMs > 0) {
            audioOffsetSec = (ts.audioStartMs - ts.screenStartMs) / 1000.0;
        }
        if (ts.screenStartMs > 0 && ts.webcamStartMs > 0) {
            webcamOffsetSec = (ts.webcamStartMs - ts.screenStartMs) / 1000.0;
        }
    }
    qDebug() << "Sync offsets: audio=" << audioOffsetSec << "s, webcam=" << webcamOffsetSec << "s";

    // Render based on canvas mode:
    // Mode 0 (landscape) = merged video only
    // Mode 1/2/3 (vertical/left split/right split) = vertical video only
    bool isVerticalMode = m_opts.canvasMode >= 1;

    if (m_cancelRequested) { m_processing = false; emit processingFinished(false); return; }

    // Step 2: Merged landscape video
    emit processingProgress(2, 0, "Merging video & audio");
    if (hasScreen && !isVerticalMode) {
        m_mergedFile = m_outputDir + "/" + Merger::outputFileName(m_opts.number, m_opts.title);
        qint64 durationUs = Merger::getVideoDurationUs(m_screenFile);

        Merger::MergeInputs in{m_screenFile, audioToUse, m_webcamFile, m_opts, audioOffsetSec, webcamOffsetSec};
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

    if (m_cancelRequested) { m_processing = false; emit processingFinished(false); return; }

    // Step 3: Vertical video
    emit processingProgress(3, 0, "Creating vertical video");
    if (hasScreen && isVerticalMode) {
        m_verticalFile = m_outputDir + "/" + Merger::outputFileName(m_opts.number, m_opts.title, "vertical");
        QString vertFile = m_verticalFile;
        qint64 durationUs = Merger::getVideoDurationUs(m_screenFile);

        Merger::MergeInputs in{m_screenFile, audioToUse, m_webcamFile, m_opts, audioOffsetSec, webcamOffsetSec};
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
