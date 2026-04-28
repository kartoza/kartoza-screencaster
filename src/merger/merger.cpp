#include "merger/merger.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace Merger {

// --- Shared helpers ---

QString titleSlug(const QString &title) {
    QString slug = title.toLower()
        .replace(QRegularExpression("[^a-z0-9]+"), "_")
        .replace(QRegularExpression("^_+|_+$"), "");
    return slug.isEmpty() ? "recording" : slug;
}

QString outputFileName(int number, const QString &title, const QString &suffix) {
    QString slug = titleSlug(title);
    QString name = QString("%1-%2").arg(number, 3, 10, QChar('0')).arg(slug);
    if (!suffix.isEmpty()) name += "-" + suffix;
    return name + ".mp4";
}

void appendLogoInputArgs(QStringList &args, const RecordingOptions::LogoOpts &logo) {
    if (logo.isGif()) {
        if (logo.gifLoop == 1) {
            args << "-ignore_loop" << "1" << "-i" << logo.path;
        } else if (logo.gifLoop == 2) {
            args << "-ignore_loop" << "0" << "-i" << logo.path;
        } else {
            // gifLoop == 0: first frame only
            args << "-i" << logo.path;
        }
    } else {
        args << "-i" << logo.path;
    }
}

// --- FFmpeg utilities ---

VideoDimensions getVideoDimensions(const QString &filePath) {
    VideoDimensions d;
    QProcess probe;
    probe.start("ffprobe", {"-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=width,height",
        "-of", "csv=p=0:s=x", filePath});
    probe.waitForFinished(10000);
    QString out = probe.readAllStandardOutput().trimmed();
    QStringList parts = out.split('x');
    if (parts.size() == 2) {
        d.width = parts[0].toInt();
        d.height = parts[1].toInt();
    }
    return d;
}

qint64 getVideoDurationUs(const QString &filePath) {
    QProcess probe;
    probe.start("ffprobe", {"-v", "error", "-show_entries", "format=duration",
                            "-of", "default=noprint_wrappers=1:nokey=1", filePath});
    probe.waitForFinished(10000);
    double secs = probe.readAllStandardOutput().trimmed().toDouble();
    return static_cast<qint64>(secs * 1000000.0);
}

QString concatenateParts(const QStringList &parts, const QString &outputFile,
                         const QString &listPrefix) {
    if (parts.isEmpty()) return {};
    if (parts.size() == 1) return parts.first();

    QStringList existing;
    for (const auto &p : parts) {
        if (QFile::exists(p)) existing.append(p);
    }
    if (existing.isEmpty()) return {};
    if (existing.size() == 1) return existing.first();

    QString dir = QFileInfo(outputFile).absolutePath();
    QString listFile = dir + "/" + listPrefix + "_list.txt";

    QFile list(listFile);
    if (!list.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    for (const auto &p : existing) {
        list.write(QString("file '%1'\n").arg(p).toUtf8());
    }
    list.close();

    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-y", "-f", "concat", "-safe", "0",
                            "-i", listFile, "-c", "copy", outputFile});
    ffmpeg.waitForFinished(-1);
    QFile::remove(listFile);

    if (ffmpeg.exitCode() == 0 && QFile::exists(outputFile)) {
        return outputFile;
    }
    qWarning() << "Concat failed:" << ffmpeg.readAllStandardError().left(200);
    return existing.first();
}

// --- Audio processing ---

LoudnormParams analyzeAudio(const QString &audioFile) {
    LoudnormParams result;

    QProcess analyze;
    analyze.start("ffmpeg", {"-y", "-i", audioFile,
        "-af", "loudnorm=I=-18:TP=-1.5:LRA=11:print_format=json",
        "-f", "null", "-"});
    analyze.waitForFinished(-1);

    QString output = analyze.readAllStandardError();

    auto extractVal = [&output](const QString &key) -> QString {
        int idx = output.indexOf("\"" + key + "\"");
        if (idx < 0) return {};
        int colon = output.indexOf(':', idx);
        int q1 = output.indexOf('"', colon);
        int q2 = output.indexOf('"', q1 + 1);
        if (q1 < 0 || q2 < 0) return {};
        return output.mid(q1 + 1, q2 - q1 - 1);
    };

    QString measI = extractVal("input_i");
    if (measI.isEmpty()) return result;

    result.valid = true;
    result.filterString = QString("loudnorm=I=-18:TP=-1.5:LRA=11:"
        "measured_I=%1:measured_TP=%2:measured_LRA=%3:"
        "measured_thresh=%4:offset=%5:linear=true")
        .arg(measI, extractVal("input_tp"), extractVal("input_lra"),
             extractVal("input_thresh"), extractVal("target_offset"));
    return result;
}

bool normalizeAudio(const QString &inputFile, const QString &outputFile,
                    const LoudnormParams &params) {
    if (!params.valid) return false;
    QProcess norm;
    norm.start("ffmpeg", {"-y", "-i", inputFile,
        "-af", params.filterString,
        "-ar", "48000", "-ac", "2", outputFile});
    norm.waitForFinished(-1);
    return norm.exitCode() == 0 && QFile::exists(outputFile);
}

// --- FFmpeg progress runner ---

int runFFmpegWithProgress(const QStringList &args, qint64 durationUs,
                          const ProgressCallback &onProgress) {
    QProcess proc;
    QStringList fullArgs = args;
    fullArgs.insert(1, "-progress");
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
            if (line.startsWith("out_time_us=") && durationUs > 0 && onProgress) {
                qint64 timeUs = line.mid(12).toLongLong();
                int pct = qBound(0, static_cast<int>(timeUs * 100 / durationUs), 99);
                onProgress(pct);
            }
        }
    }
    proc.waitForFinished(-1);
    return proc.exitCode();
}

// --- Filter building: merged landscape video ---

static void appendLogoFilter(QString &filter, QString &current, int &vIdx,
                              int inputIdx, const RecordingOptions::LogoOpts &logo,
                              const QString &tag, int canvasW, int canvasH,
                              const QString &enable = {}) {
    int logoW = qMax(20, static_cast<int>(logo.relW * canvasW));
    int logoX = static_cast<int>(logo.relX * canvasW);
    int logoY = static_cast<int>(logo.relY * canvasH);

    filter += QString("[%1:v]scale=%2:-1[%3];").arg(inputIdx).arg(logoW).arg(tag);

    QString pos = QString("%1:%2").arg(logoX).arg(logoY);
    if (enable.isEmpty()) {
        filter += QString("%1[%2]overlay=%3[v%4];").arg(current, tag, pos).arg(++vIdx);
    } else {
        filter += QString("%1[%2]overlay=%3:%4[v%5];").arg(current, tag, pos, enable).arg(++vIdx);
    }
    current = QString("[v%1]").arg(vIdx);
}

static void appendWebcamFilter(QString &filter, QString &current, int &vIdx,
                                int webcamInput, const RecordingOptions &opts,
                                int canvasW, int canvasH) {
    int wcW = qMax(50, static_cast<int>(opts.webcamRelW * canvasW));
    int wcH = qMax(50, static_cast<int>(opts.webcamRelH * canvasH));
    wcW = (wcW / 2) * 2; wcH = (wcH / 2) * 2;
    int wcX = static_cast<int>(opts.webcamRelX * canvasW);
    int wcY = static_cast<int>(opts.webcamRelY * canvasH);

    filter += QString("[%1:v]scale=%2:%3,setsar=1[wcam];").arg(webcamInput).arg(wcW).arg(wcH);
    if (opts.webcamShape == 0) {
        int r = qMin(wcW, wcH) / 2;
        filter += QString("color=black:%1x%2,geq=lum='if(lt(hypot(X-%3,Y-%4),%5),255,0)':cb=128:cr=128[cmask];")
            .arg(wcW).arg(wcH).arg(wcW/2).arg(wcH/2).arg(r);
        filter += "[wcam][cmask]alphamerge[wcam_shaped];";
    } else {
        filter += "[wcam]null[wcam_shaped];";
    }
    filter += QString("%1[wcam_shaped]overlay=%2:%3[v%4];").arg(current).arg(wcX).arg(wcY).arg(++vIdx);
    current = QString("[v%1]").arg(vIdx);
}

static void finalizeFilter(QString &filter) {
    // Replace last output tag with [outv]
    filter.chop(1);
    int lastBracket = filter.lastIndexOf('[');
    filter = filter.left(lastBracket) + "[outv]";
}

QStringList buildMergedArgs(const MergeInputs &in, const QString &outputFile) {
    bool hasAudio = !in.audioFile.isEmpty() && QFile::exists(in.audioFile);
    bool hasWebcam = !in.webcamFile.isEmpty() && QFile::exists(in.webcamFile);
    bool hasLeftLogo = !in.opts.leftLogo.path.isEmpty() && QFile::exists(in.opts.leftLogo.path);
    bool hasRightLogo = !in.opts.rightLogo.path.isEmpty() && QFile::exists(in.opts.rightLogo.path);
    bool hasBannerLogo = !in.opts.bannerLogo.path.isEmpty() && QFile::exists(in.opts.bannerLogo.path);
    bool mergeWebcam = hasWebcam && !in.opts.noScreen;
    bool needsFilter = hasLeftLogo || hasRightLogo || hasBannerLogo || mergeWebcam;

    QStringList args;
    args << "-y" << "-i" << in.screenFile;
    int nextInput = 1;
    int audioInput = -1, webcamInput = -1;
    if (hasAudio) { audioInput = nextInput++; args << "-i" << in.audioFile; }
    if (mergeWebcam) { webcamInput = nextInput++; args << "-i" << in.webcamFile; }
    int leftLogoIn = -1, rightLogoIn = -1, bannerLogoIn = -1;
    if (hasLeftLogo) { leftLogoIn = nextInput++; appendLogoInputArgs(args, in.opts.leftLogo); }
    if (hasRightLogo) { rightLogoIn = nextInput++; appendLogoInputArgs(args, in.opts.rightLogo); }
    if (hasBannerLogo) { bannerLogoIn = nextInput++; appendLogoInputArgs(args, in.opts.bannerLogo); }

    if (needsFilter) {
        QString filter;
        QString current = "[0:v]";
        QString logoEnable = "enable='between(t,0,15)'";
        int vIdx = 0;

        auto dim = getVideoDimensions(in.screenFile);
        int cw = dim.width > 0 ? dim.width : 1920;
        int ch = dim.height > 0 ? dim.height : 1080;

        if (hasLeftLogo) appendLogoFilter(filter, current, vIdx, leftLogoIn, in.opts.leftLogo, "ll", cw, ch, logoEnable);
        if (hasRightLogo) appendLogoFilter(filter, current, vIdx, rightLogoIn, in.opts.rightLogo, "rl", cw, ch, logoEnable);
        if (hasBannerLogo) appendLogoFilter(filter, current, vIdx, bannerLogoIn, in.opts.bannerLogo, "bl", cw, ch, logoEnable);
        if (mergeWebcam) appendWebcamFilter(filter, current, vIdx, webcamInput, in.opts, cw, ch);

        finalizeFilter(filter);
        args << "-filter_complex" << filter << "-map" << "[outv]";
        if (hasAudio) args << "-map" << QString("%1:a").arg(audioInput);
    }

    args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "18" << "-r" << "30";
    if (hasAudio) {
        args << "-c:a" << "aac" << "-b:a" << "320k" << "-shortest";
    } else {
        args << "-an";
    }
    args << outputFile;
    return args;
}

// --- Filter building: vertical (9:16) video ---

QStringList buildVerticalArgs(const MergeInputs &in, const QString &outputFile) {
    bool hasAudio = !in.audioFile.isEmpty() && QFile::exists(in.audioFile);
    bool hasWebcam = !in.webcamFile.isEmpty() && QFile::exists(in.webcamFile);
    bool hasLeftLogo = !in.opts.leftLogo.path.isEmpty() && QFile::exists(in.opts.leftLogo.path);
    bool hasRightLogo = !in.opts.rightLogo.path.isEmpty() && QFile::exists(in.opts.rightLogo.path);
    bool hasBannerLogo = !in.opts.bannerLogo.path.isEmpty() && QFile::exists(in.opts.bannerLogo.path);

    QString titleColor = in.opts.titleColor.isEmpty() ? "white" : in.opts.titleColor;
    QString escapedTitle = in.opts.title;
    escapedTitle.replace("'", "\\'");
    escapedTitle.replace(":", "\\:");

    QStringList args;
    args << "-y" << "-i" << in.screenFile;
    int nextInput = 1;
    int webcamInput = -1, audioInput = -1;
    int leftLogoIn = -1, rightLogoIn = -1, bannerLogoIn = -1;

    if (hasWebcam) { webcamInput = nextInput++; args << "-i" << in.webcamFile; }
    if (hasAudio) { audioInput = nextInput++; args << "-i" << in.audioFile; }
    if (hasLeftLogo) { leftLogoIn = nextInput++; appendLogoInputArgs(args, in.opts.leftLogo); }
    if (hasRightLogo) { rightLogoIn = nextInput++; appendLogoInputArgs(args, in.opts.rightLogo); }
    if (hasBannerLogo) { bannerLogoIn = nextInput++; appendLogoInputArgs(args, in.opts.bannerLogo); }

    QString f;
    f += "[0:v]scale=1080:-2,setsar=1[screen];";
    f += "[screen]pad=1080:1920:(ow-iw)/2:0:black[padded];";
    f += "[padded]drawbox=y=1280:w=1080:h=640:c=white:t=fill[canvas];";

    QString current = "[canvas]";
    int vIdx = 0;

    if (hasWebcam) appendWebcamFilter(f, current, vIdx, webcamInput, in.opts, 1080, 1920);

    // Logos at fixed vertical positions
    if (hasLeftLogo) {
        int lw = qMax(20, static_cast<int>(in.opts.leftLogo.relW * 1080));
        int lx = static_cast<int>(in.opts.leftLogo.relX * 1080);
        int ly = static_cast<int>(in.opts.leftLogo.relY * 1920);
        f += QString("[%1:v]scale=%2:-1[ll];").arg(leftLogoIn).arg(lw);
        f += QString("%1[ll]overlay=%2:%3[v%4];").arg(current).arg(lx).arg(ly).arg(++vIdx);
        current = QString("[v%1]").arg(vIdx);
    }
    if (hasRightLogo) {
        int rw = qMax(20, static_cast<int>(in.opts.rightLogo.relW * 1080));
        int rx = static_cast<int>(in.opts.rightLogo.relX * 1080);
        int ry = static_cast<int>(in.opts.rightLogo.relY * 1920);
        f += QString("[%1:v]scale=%2:-1[rl];").arg(rightLogoIn).arg(rw);
        f += QString("%1[rl]overlay=%2:%3[v%4];").arg(current).arg(rx).arg(ry).arg(++vIdx);
        current = QString("[v%1]").arg(vIdx);
    }
    if (hasBannerLogo) {
        int bw = qMax(20, static_cast<int>(in.opts.bannerLogo.relW * 1080));
        int bx = static_cast<int>(in.opts.bannerLogo.relX * 1080);
        int by = static_cast<int>(in.opts.bannerLogo.relY * 1920);
        f += QString("[%1:v]scale=%2:-1[bl];").arg(bannerLogoIn).arg(bw);
        f += QString("%1[bl]overlay=%2:%3[v%4];").arg(current).arg(bx).arg(by).arg(++vIdx);
        current = QString("[v%1]").arg(vIdx);
    }

    f += QString("%1drawtext=text='%2':fontsize=36:fontcolor=%3:"
        "x=(w-text_w)/2:y=1850[outv]")
        .arg(current, escapedTitle, titleColor);

    args << "-filter_complex" << f << "-map" << "[outv]";
    if (hasAudio) {
        args << "-map" << QString("%1:a").arg(audioInput);
        args << "-c:a" << "aac" << "-b:a" << "320k" << "-shortest";
    } else {
        args << "-an";
    }
    args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "18" << "-r" << "30"
         << "-s" << "1080x1920" << outputFile;
    return args;
}

} // namespace Merger
