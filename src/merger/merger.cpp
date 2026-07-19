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

// Escape a string for use inside an FFmpeg drawtext text='...' value.
static QString escapeDrawtext(const QString &in) {
    QString s = in;
    s.replace("\\", "\\\\");
    s.replace("'", "\\'");
    s.replace(":", "\\:");
    s.replace("%", "\\%");
    return s;
}

void appendTextBoxFilters(QString &filter, QString &current, int &vIdx,
                          const QVector<RecordingOptions::TextBox> &boxes,
                          int outW, int outH) {
    for (const auto &tb : boxes) {
        if (tb.text.trimmed().isEmpty()) continue;

        // Font size derives from the box height, matching the canvas (visH*2/3).
        int fontSize = qMax(10, static_cast<int>(tb.relH * outH * 2.0 / 3.0));
        int x = static_cast<int>(tb.relX * outW);
        int y = static_cast<int>(tb.relY * outH);
        QString color = tb.color.isEmpty() ? QStringLiteral("white") : tb.color;

        // Prefer a resolved font file (carries the requested weight); otherwise
        // hand the family name to fontconfig.
        QString fontSel;
        if (!tb.fontFile.isEmpty()) {
            QString ff = tb.fontFile;
            ff.replace("\\", "\\\\");
            ff.replace(":", "\\:");
            fontSel = QString("fontfile='%1'").arg(ff);
        } else {
            fontSel = QString("font='%1'").arg(tb.fontFamily);
        }

        QString tag = QString("v%1").arg(++vIdx);
        filter += QString("%1drawtext=%2:text='%3':fontsize=%4:fontcolor=%5:x=%6:y=%7[%8];")
                      .arg(current, fontSel, escapeDrawtext(tb.text),
                           QString::number(fontSize), color,
                           QString::number(x), QString::number(y), tag);
        current = QString("[%1]").arg(tag);
    }
}

void appendLogoInputArgs(QStringList &args, const RecordingOptions::LogoOpts &logo) {
    if (logo.isGif()) {
        if (logo.gifLoop == 1) {
            // Play once: tell FFmpeg to ignore the GIF's loop and play once
            args << "-ignore_loop" << "1" << "-i" << logo.path;
        } else if (logo.gifLoop == 2) {
            // Continuous: let FFmpeg respect the GIF's internal loop setting.
            // Default overlay eof_action=repeat keeps the last frame visible.
            args << "-i" << logo.path;
        } else {
            // gifLoop == 0: first frame only — just load the image
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

    if (opts.webcamShape == 0) {
        // Round bubble: scale to fill a square (preserving aspect ratio) then crop to circle
        int diameter = qMin(wcW, wcH);
        diameter = (diameter / 2) * 2; // ensure even
        int r = diameter / 2;
        // Scale preserving aspect ratio to fill the square, then crop center
        filter += QString("[%1:v]scale=%2:%2:force_original_aspect_ratio=increase,"
                          "crop=%2:%2,setsar=1[wcam];")
            .arg(webcamInput).arg(diameter);
        // Circular mask
        filter += QString("color=black:%1x%1:d=0.04,"
                          "geq=lum='if(lt(hypot(X-%2,Y-%2),%2),255,0)':cb=128:cr=128[cmask];")
            .arg(diameter).arg(r);
        filter += "[wcam][cmask]alphamerge=eof_action=repeat[wcam_shaped];";
        // Center the circle at the overlay position
        wcX = wcX + (wcW - diameter) / 2;
        wcY = wcY + (wcH - diameter) / 2;
    } else {
        // Rectangle/square: scale preserving aspect ratio, pad to fit target box
        filter += QString("[%1:v]scale=%2:%3:force_original_aspect_ratio=decrease,"
                          "pad=%2:%3:(ow-iw)/2:(oh-ih)/2:color=black,setsar=1[wcam_shaped];")
            .arg(webcamInput).arg(wcW).arg(wcH);
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
    bool mergeWebcam = hasWebcam && !in.opts.noScreen;

    // Collect valid logos and their FFmpeg input indices
    QVector<int> logoInputs;

    QStringList args;
    args << "-y" << "-i" << in.screenFile;
    int nextInput = 1;
    int audioInput = -1, webcamInput = -1;
    if (hasAudio) {
        // Positive itsoffset delays audio to align with screen (audio started later)
        if (qAbs(in.audioOffsetSec) > 0.01) {
            args << "-itsoffset" << QString::number(in.audioOffsetSec, 'f', 3);
        }
        audioInput = nextInput++;
        args << "-i" << in.audioFile;
    }
    if (mergeWebcam) {
        if (qAbs(in.webcamOffsetSec) > 0.01) {
            args << "-itsoffset" << QString::number(in.webcamOffsetSec, 'f', 3);
        }
        webcamInput = nextInput++;
        args << "-i" << in.webcamFile;
    }
    for (int i = 0; i < in.opts.logos.size(); ++i) {
        const auto &logo = in.opts.logos[i];
        if (!logo.path.isEmpty() && QFile::exists(logo.path)) {
            logoInputs.append(nextInput++);
            appendLogoInputArgs(args, logo);
        } else {
            logoInputs.append(-1);
        }
    }

    bool needsFilter = !logoInputs.isEmpty() || mergeWebcam;
    // Check if any logo inputs are actually valid
    bool hasAnyLogo = false;
    for (int idx : logoInputs) { if (idx >= 0) { hasAnyLogo = true; break; } }
    needsFilter = hasAnyLogo || mergeWebcam;

    // Check if screen needs cropping
    bool needsCrop = in.opts.screenCropTop > 0 || in.opts.screenCropBottom > 0 ||
                     in.opts.screenCropLeft > 0 || in.opts.screenCropRight > 0;
    needsFilter = needsFilter || needsCrop;

    // Text boxes also require a filter graph (drawtext) in landscape mode.
    bool hasTextBoxes = false;
    for (const auto &tb : in.opts.textBoxes) {
        if (!tb.text.trimmed().isEmpty()) { hasTextBoxes = true; break; }
    }
    needsFilter = needsFilter || hasTextBoxes;

    if (needsFilter) {
        QString filter;
        QString current = "[0:v]";
        QString logoEnable = "enable='between(t,0,15)'";
        int vIdx = 0;

        auto dim = getVideoDimensions(in.screenFile);
        int cw = dim.width > 0 ? dim.width : 1920;
        int ch = dim.height > 0 ? dim.height : 1080;

        // Apply screen crop if set
        if (needsCrop) {
            int cropX = int(in.opts.screenCropLeft * cw);
            int cropY = int(in.opts.screenCropTop * ch);
            int cropW = cw - int(in.opts.screenCropLeft * cw) - int(in.opts.screenCropRight * cw);
            int cropH = ch - int(in.opts.screenCropTop * ch) - int(in.opts.screenCropBottom * ch);
            // Ensure even dimensions for h264
            cropW = cropW & ~1;
            cropH = cropH & ~1;
            filter += QString("%1crop=%2:%3:%4:%5[cropped];")
                .arg(current).arg(cropW).arg(cropH).arg(cropX).arg(cropY);
            current = "[cropped]";
            cw = cropW;
            ch = cropH;
        }

        for (int i = 0; i < in.opts.logos.size(); ++i) {
            if (i < logoInputs.size() && logoInputs[i] >= 0) {
                QString tag = QString("logo%1").arg(i);
                appendLogoFilter(filter, current, vIdx, logoInputs[i], in.opts.logos[i], tag, cw, ch, logoEnable);
            }
        }
        if (mergeWebcam) appendWebcamFilter(filter, current, vIdx, webcamInput, in.opts, cw, ch);

        // Text boxes (WYSIWYG) rendered on top at their relative positions.
        appendTextBoxFilters(filter, current, vIdx, in.opts.textBoxes, cw, ch);

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

    QStringList args;
    args << "-y" << "-i" << in.screenFile;
    int nextInput = 1;
    int webcamInput = -1, audioInput = -1;

    if (hasWebcam) {
        if (qAbs(in.webcamOffsetSec) > 0.01) {
            args << "-itsoffset" << QString::number(in.webcamOffsetSec, 'f', 3);
        }
        webcamInput = nextInput++;
        args << "-i" << in.webcamFile;
    }
    if (hasAudio) {
        if (qAbs(in.audioOffsetSec) > 0.01) {
            args << "-itsoffset" << QString::number(-in.audioOffsetSec, 'f', 3);
        }
        audioInput = nextInput++;
        args << "-i" << in.audioFile;
    }

    // Register logo inputs
    struct LogoInput { int inputIdx; int logoIdx; };
    QVector<LogoInput> validLogos;
    for (int i = 0; i < in.opts.logos.size(); ++i) {
        const auto &logo = in.opts.logos[i];
        if (!logo.path.isEmpty() && QFile::exists(logo.path)) {
            int idx = nextInput++;
            appendLogoInputArgs(args, logo);
            validLogos.append({idx, i});
        }
    }

    QString f;

    // Apply screen crop if set
    bool needsCrop = in.opts.screenCropTop > 0 || in.opts.screenCropBottom > 0 ||
                     in.opts.screenCropLeft > 0 || in.opts.screenCropRight > 0;
    if (needsCrop) {
        auto dim = getVideoDimensions(in.screenFile);
        int sw = dim.width > 0 ? dim.width : 1920;
        int sh = dim.height > 0 ? dim.height : 1080;
        int cropX = int(in.opts.screenCropLeft * sw);
        int cropY = int(in.opts.screenCropTop * sh);
        int cropW = sw - int(in.opts.screenCropLeft * sw) - int(in.opts.screenCropRight * sw);
        int cropH = sh - int(in.opts.screenCropTop * sh) - int(in.opts.screenCropBottom * sh);
        cropW = cropW & ~1;
        cropH = cropH & ~1;
        f += QString("[0:v]crop=%1:%2:%3:%4,scale=1080:-2,setsar=1[screen];")
            .arg(cropW).arg(cropH).arg(cropX).arg(cropY);
    } else {
        f += "[0:v]scale=1080:-2,setsar=1[screen];";
    }
    f += "[screen]pad=1080:1920:(ow-iw)/2:0:black[padded];";
    f += "[padded]drawbox=y=1280:w=1080:h=640:c=white:t=fill[canvas];";

    QString current = "[canvas]";
    int vIdx = 0;

    if (hasWebcam) appendWebcamFilter(f, current, vIdx, webcamInput, in.opts, 1080, 1920);

    // Logos at their relative positions
    for (const auto &vl : validLogos) {
        const auto &logo = in.opts.logos[vl.logoIdx];
        int lw = qMax(20, static_cast<int>(logo.relW * 1080));
        int lx = static_cast<int>(logo.relX * 1080);
        int ly = static_cast<int>(logo.relY * 1920);
        QString tag = QString("logo%1").arg(vl.logoIdx);
        f += QString("[%1:v]scale=%2:-1[%3];").arg(vl.inputIdx).arg(lw).arg(tag);
        f += QString("%1[%2]overlay=%3:%4[v%5];").arg(current).arg(tag).arg(lx).arg(ly).arg(++vIdx);
        current = QString("[v%1]").arg(vIdx);
    }

    // Text boxes (WYSIWYG) — each drawn with its own font/weight/colour/position.
    // Backward compatibility: a recording that carries only a legacy title (no
    // text boxes) still gets one centred title box near the bottom, matching the
    // previous single-drawtext behaviour.
    QVector<RecordingOptions::TextBox> boxes = in.opts.textBoxes;
    if (boxes.isEmpty() && !in.opts.title.trimmed().isEmpty()) {
        RecordingOptions::TextBox tb;
        tb.text = in.opts.title;
        tb.color = in.opts.titleColor.isEmpty() ? "white" : in.opts.titleColor;
        tb.relX = 0.28; tb.relY = 0.96; tb.relW = 0.44; tb.relH = 0.03;
        boxes.append(tb);
    }
    appendTextBoxFilters(f, current, vIdx, boxes, 1080, 1920);
    finalizeFilter(f);

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
