#include <QTest>
#include <QProcess>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include "merger/merger.h"

// ---------------------------------------------------------------------------
// Probe helper
// ---------------------------------------------------------------------------

struct ProbeResult {
    int width = 0, height = 0;
    double duration = 0;
    bool hasVideo = false;
    bool hasAudio = false;
    QString videoCodec;
    QString audioCodec;
};

static ProbeResult probeFile(const QString &path) {
    ProbeResult r;
    if (!QFile::exists(path)) return r;

    QProcess probe;
    probe.start("ffprobe", {"-v", "error",
        "-show_entries", "stream=codec_type,codec_name,width,height",
        "-show_entries", "format=duration",
        "-of", "json", path});
    probe.waitForFinished(10000);

    auto doc = QJsonDocument::fromJson(probe.readAllStandardOutput());
    auto root = doc.object();

    auto streams = root["streams"].toArray();
    for (const auto &s : streams) {
        auto obj = s.toObject();
        QString type = obj["codec_type"].toString();
        if (type == "video") {
            r.hasVideo = true;
            r.videoCodec = obj["codec_name"].toString();
            r.width = obj["width"].toInt();
            r.height = obj["height"].toInt();
        } else if (type == "audio") {
            r.hasAudio = true;
            r.audioCodec = obj["codec_name"].toString();
        }
    }

    auto format = root["format"].toObject();
    r.duration = format["duration"].toString().toDouble();
    return r;
}

// ---------------------------------------------------------------------------
// Synthetic media helpers
// ---------------------------------------------------------------------------

// Each element uses a distinct contrasting colour with a text label
// so visual review can clearly identify what's what:
//   Screen  = dark blue (#1a1a6e) with "SCREEN" label
//   Webcam  = bright green (#00cc44) with "WEBCAM" label
//   Logo    = bright yellow (#ffcc00) with "LOGO" label

static bool createTestVideo(const QString &path, int w, int h, double secs,
                             const QString &color, const QString &label) {
    QProcess p;
    int fontSize = qMax(12, h / 6);
    QString filter = QString(
        "color=c=%1:s=%2x%3:d=%4,drawtext=text='%5':"
        "fontsize=%6:fontcolor=white:x=(w-text_w)/2:y=(h-text_h)/2")
        .arg(color).arg(w).arg(h).arg(secs, 0, 'f', 1).arg(label).arg(fontSize);
    p.start("ffmpeg", {"-y", "-f", "lavfi", "-i", filter,
        "-c:v", "libx264", "-preset", "ultrafast", "-crf", "28",
        "-pix_fmt", "yuv420p", path});
    p.waitForFinished(30000);
    return p.exitCode() == 0 && QFile::exists(path);
}

static bool createTestAudio(const QString &path, double secs = 5.0) {
    QProcess p;
    p.start("ffmpeg", {"-y", "-f", "lavfi",
        "-i", QString("sine=frequency=440:duration=%1").arg(secs, 0, 'f', 1),
        "-ac", "2", path});
    p.waitForFinished(15000);
    return p.exitCode() == 0 && QFile::exists(path);
}

static bool createTestLogo(const QString &path, int w, int h,
                            const QString &color, const QString &label) {
    QProcess p;
    int fontSize = qMax(10, h / 4);
    QString filter = QString(
        "color=c=%1:s=%2x%3:d=0.04,drawtext=text='%4':"
        "fontsize=%5:fontcolor=black:x=(w-text_w)/2:y=(h-text_h)/2")
        .arg(color).arg(w).arg(h).arg(label).arg(fontSize);
    p.start("ffmpeg", {"-y", "-f", "lavfi", "-i", filter,
        "-frames:v", "1", path});
    p.waitForFinished(5000);
    return p.exitCode() == 0 && QFile::exists(path);
}

static QString testdataDir() {
#ifdef SRCDIR
    return QString(SRCDIR) + "/tests/testdata";
#else
    return QCoreApplication::applicationDirPath() + "/../tests/testdata";
#endif
}

// Set webcam size/position matching canvas conventions:
//   shape 0 (round):  1:1 aspect
//   shape 1 (square corners): 4:3 aspect, smaller
//   shape 2 (rect corners):   4:3 aspect, wider
static void setWebcam(RecordingOptions &opts, int shape,
                       double relX = 0.8, double relY = 0.7) {
    opts.webcamShape = shape;
    opts.webcamRelX = relX;
    opts.webcamRelY = relY;
    if (shape == 0) {
        opts.webcamRelW = 0.15; opts.webcamRelH = 0.15; // 1:1 round
    } else if (shape == 1) {
        opts.webcamRelW = 0.12; opts.webcamRelH = 0.12 * 3.0 / 4.0; // 4:3 small
    } else {
        opts.webcamRelW = 0.20; opts.webcamRelH = 0.20 * 3.0 / 4.0; // 4:3 wide
    }
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TestMergerExhaustive : public QObject {
    Q_OBJECT

    QString m_outputDir; // persistent output dir for visual review
    QString m_screen;
    QString m_webcam;
    QString m_audio;
    QString m_staticLogo;
    QString m_animGif;

    // Helper: run a merge and validate the output via probe
    // Outputs are kept in build/test_outputs/ for visual review
    void runMerge(const QString &tag, Merger::MergeInputs &in, bool landscape,
                  bool expectAudio, int expectW, int expectH) {
        QString output = m_outputDir + "/" + tag + ".mp4";

        QStringList args;
        if (landscape) {
            args = Merger::buildMergedArgs(in, output);
        } else {
            args = Merger::buildVerticalArgs(in, output);
        }

        qint64 durUs = Merger::getVideoDurationUs(in.screenFile);
        int exitCode = Merger::runFFmpegWithProgress(args, durUs);
        QCOMPARE(exitCode, 0);
        QVERIFY2(QFile::exists(output), qPrintable("Output missing: " + output));

        auto info = probeFile(output);
        QVERIFY2(info.hasVideo, qPrintable(tag + ": no video stream"));
        QVERIFY2(info.duration > 1.0, qPrintable(
            QString("%1: duration too short: %2").arg(tag).arg(info.duration)));

        if (expectAudio) {
            QVERIFY2(info.hasAudio, qPrintable(tag + ": expected audio"));
            QCOMPARE(info.audioCodec, QString("aac"));
        } else {
            QVERIFY2(!info.hasAudio, qPrintable(tag + ": unexpected audio"));
        }

        if (expectW > 0) {
            QCOMPARE(info.width, expectW);
        }
        if (expectH > 0) {
            QCOMPARE(info.height, expectH);
        }
        // Output kept for visual review — see build/test_outputs/
    }

private slots:
    void initTestCase() {
        // Create persistent output directory for visual review
        // Lives at project root (not in build/) so it survives clean rebuilds
#ifdef SRCDIR
        m_outputDir = QString(SRCDIR) + "/tests/test_outputs";
#else
        m_outputDir = QCoreApplication::applicationDirPath() + "/../tests/test_outputs";
#endif
        QDir().mkpath(m_outputDir);
        qInfo() << "Test outputs will be saved to:" << m_outputDir;

        // Create synthetic source media with distinct colours and labels
        m_screen = m_outputDir + "/source_screen_640x360.mp4";
        if (!QFile::exists(m_screen))
            QVERIFY(createTestVideo(m_screen, 640, 360, 2.0, "#1a1a6e", "SCREEN"));

        m_webcam = m_outputDir + "/source_webcam_320x240.mp4";
        if (!QFile::exists(m_webcam))
            QVERIFY(createTestVideo(m_webcam, 320, 240, 2.0, "#00cc44", "WEBCAM"));

        m_audio = m_outputDir + "/source_audio_440hz.wav";
        if (!QFile::exists(m_audio))
            QVERIFY(createTestAudio(m_audio, 2.0));

        m_staticLogo = m_outputDir + "/source_static_logo.png";
        if (!QFile::exists(m_staticLogo))
            QVERIFY(createTestLogo(m_staticLogo, 100, 100, "#ffcc00", "LOGO"));

        // Copy anim_icon.gif from testdata
        m_animGif = m_outputDir + "/source_anim_icon.gif";
        if (!QFile::exists(m_animGif)) {
            QString srcGif = testdataDir() + "/anim_icon.gif";
            if (QFile::exists(srcGif)) {
                QVERIFY(QFile::copy(srcGif, m_animGif));
            } else {
                qWarning() << "anim_icon.gif not found at" << srcGif << "- creating synthetic GIF";
                QProcess p;
                p.start("ffmpeg", {"-y", "-f", "lavfi",
                    "-i", "color=c=green:s=64x64:d=1:r=5",
                    "-loop", "0", m_animGif});
                p.waitForFinished(10000);
                QVERIFY(p.exitCode() == 0 && QFile::exists(m_animGif));
            }
        }
    }

    // =====================================================================
    // LANDSCAPE tests (17)
    // =====================================================================

    void landscape_screenOnly() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;
        runMerge("land_screenOnly", in, true, false, 0, 0);
    }

    void landscape_screenAudio() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.audioFile = m_audio;
        in.opts.noAudio = false;
        in.opts.noWebcam = true;
        runMerge("land_screenAudio", in, true, true, 0, 0);
    }

    void landscape_webcamRound() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.webcamFile = m_webcam;
        in.opts.noAudio = true;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 0);
        runMerge("land_webcamRound", in, true, false, 0, 0);
    }

    void landscape_webcamSquare() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.webcamFile = m_webcam;
        in.opts.noAudio = true;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 1);
        runMerge("land_webcamSquare", in, true, false, 0, 0);
    }

    void landscape_webcamRect() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.webcamFile = m_webcam;
        in.opts.noAudio = true;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 2);
        runMerge("land_webcamRect", in, true, false, 0, 0);
    }

    void landscape_audioWebcamRound() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.audioFile = m_audio;
        in.webcamFile = m_webcam;
        in.opts.noAudio = false;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 0);
        runMerge("land_audioWebcamRound", in, true, true, 0, 0);
    }

    void landscape_audioWebcamSquare() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.audioFile = m_audio;
        in.webcamFile = m_webcam;
        in.opts.noAudio = false;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 1);
        runMerge("land_audioWebcamSquare", in, true, true, 0, 0);
    }

    void landscape_audioWebcamRect() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.audioFile = m_audio;
        in.webcamFile = m_webcam;
        in.opts.noAudio = false;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 2);
        runMerge("land_audioWebcamRect", in, true, true, 0, 0);
    }

    void landscape_oneStaticLogo() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;
        RecordingOptions::LogoOpts lo;
        lo.path = m_staticLogo;
        lo.relX = 0.01; lo.relY = 0.01; lo.relW = 0.1; lo.relH = 0.1;
        in.opts.logos = {lo};
        runMerge("land_oneStaticLogo", in, true, false, 0, 0);
    }

    void landscape_oneGifContinuous() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;
        RecordingOptions::LogoOpts lo;
        lo.path = m_animGif;
        lo.gifLoop = 2; // continuous
        lo.relX = 0.01; lo.relY = 0.01; lo.relW = 0.1; lo.relH = 0.1;
        in.opts.logos = {lo};
        runMerge("land_oneGifContinuous", in, true, false, 0, 0);
    }

    void landscape_oneGifOnce() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;
        RecordingOptions::LogoOpts lo;
        lo.path = m_animGif;
        lo.gifLoop = 1; // once
        lo.relX = 0.01; lo.relY = 0.01; lo.relW = 0.1; lo.relH = 0.1;
        in.opts.logos = {lo};
        runMerge("land_oneGifOnce", in, true, false, 0, 0);
    }

    void landscape_oneGifFirstFrame() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;
        RecordingOptions::LogoOpts lo;
        lo.path = m_animGif;
        lo.gifLoop = 0; // first frame only
        lo.relX = 0.01; lo.relY = 0.01; lo.relW = 0.1; lo.relH = 0.1;
        in.opts.logos = {lo};
        runMerge("land_oneGifFirstFrame", in, true, false, 0, 0);
    }

    void landscape_twoLogos() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;
        RecordingOptions::LogoOpts lo1;
        lo1.path = m_staticLogo;
        lo1.relX = 0.01; lo1.relY = 0.01; lo1.relW = 0.1; lo1.relH = 0.1;
        RecordingOptions::LogoOpts lo2;
        lo2.path = m_animGif;
        lo2.gifLoop = 2;
        lo2.relX = 0.89; lo2.relY = 0.01; lo2.relW = 0.1; lo2.relH = 0.1;
        in.opts.logos = {lo1, lo2};
        runMerge("land_twoLogos", in, true, false, 0, 0);
    }

    void landscape_threeLogos() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;
        RecordingOptions::LogoOpts lo1;
        lo1.path = m_staticLogo;
        lo1.relX = 0.01; lo1.relY = 0.01; lo1.relW = 0.08; lo1.relH = 0.08;
        RecordingOptions::LogoOpts lo2;
        lo2.path = m_animGif;
        lo2.gifLoop = 2; // continuous
        lo2.relX = 0.89; lo2.relY = 0.01; lo2.relW = 0.08; lo2.relH = 0.08;
        RecordingOptions::LogoOpts lo3;
        lo3.path = m_animGif;
        lo3.gifLoop = 1; // once
        lo3.relX = 0.45; lo3.relY = 0.01; lo3.relW = 0.08; lo3.relH = 0.08;
        in.opts.logos = {lo1, lo2, lo3};
        runMerge("land_threeLogos", in, true, false, 0, 0);
    }

    void landscape_fullCombo() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.audioFile = m_audio;
        in.webcamFile = m_webcam;
        in.opts.noAudio = false;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 0);
        RecordingOptions::LogoOpts lo1;
        lo1.path = m_staticLogo;
        lo1.relX = 0.01; lo1.relY = 0.01; lo1.relW = 0.1; lo1.relH = 0.1;
        RecordingOptions::LogoOpts lo2;
        lo2.path = m_animGif;
        lo2.gifLoop = 2;
        lo2.relX = 0.89; lo2.relY = 0.01; lo2.relW = 0.1; lo2.relH = 0.1;
        in.opts.logos = {lo1, lo2};
        runMerge("land_fullCombo", in, true, true, 0, 0);
    }

    void landscape_logoWebcamSquareAudio() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.audioFile = m_audio;
        in.webcamFile = m_webcam;
        in.opts.noAudio = false;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 1);
        RecordingOptions::LogoOpts lo;
        lo.path = m_animGif;
        lo.gifLoop = 0; // first frame
        lo.relX = 0.01; lo.relY = 0.01; lo.relW = 0.1; lo.relH = 0.1;
        in.opts.logos = {lo};
        runMerge("land_logoWebcamSquareAudio", in, true, true, 0, 0);
    }

    void landscape_logoWebcamRectNoAudio() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.webcamFile = m_webcam;
        in.opts.noAudio = true;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 2);
        RecordingOptions::LogoOpts lo;
        lo.path = m_staticLogo;
        lo.relX = 0.01; lo.relY = 0.01; lo.relW = 0.1; lo.relH = 0.1;
        in.opts.logos = {lo};
        runMerge("land_logoWebcamRectNoAudio", in, true, false, 0, 0);
    }

    // =====================================================================
    // VERTICAL tests (17)
    // =====================================================================

    void vertical_screenOnly() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;
        in.opts.title = "Screen Only";
        in.opts.titleColor = "#FFFFFF";
        runMerge("vert_screenOnly", in, false, false, 1080, 1920);
    }

    void vertical_screenAudio() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.audioFile = m_audio;
        in.opts.noAudio = false;
        in.opts.noWebcam = true;
        in.opts.title = "Screen Audio";
        in.opts.titleColor = "#FFFFFF";
        runMerge("vert_screenAudio", in, false, true, 1080, 1920);
    }

    void vertical_webcamRound() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.webcamFile = m_webcam;
        in.opts.noAudio = true;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 0, 0.7, 0.5);
        in.opts.title = "Webcam Round";
        in.opts.titleColor = "#62A4C7";
        runMerge("vert_webcamRound", in, false, false, 1080, 1920);
    }

    void vertical_webcamSquare() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.webcamFile = m_webcam;
        in.opts.noAudio = true;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 1, 0.7, 0.5);
        in.opts.title = "Webcam Square";
        in.opts.titleColor = "#62A4C7";
        runMerge("vert_webcamSquare", in, false, false, 1080, 1920);
    }

    void vertical_webcamRect() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.webcamFile = m_webcam;
        in.opts.noAudio = true;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 2, 0.7, 0.5);
        in.opts.title = "Webcam Rect";
        in.opts.titleColor = "#62A4C7";
        runMerge("vert_webcamRect", in, false, false, 1080, 1920);
    }

    void vertical_audioWebcamRound() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.audioFile = m_audio;
        in.webcamFile = m_webcam;
        in.opts.noAudio = false;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 0, 0.7, 0.5);
        in.opts.title = "Audio Webcam Round";
        in.opts.titleColor = "#62A4C7";
        runMerge("vert_audioWebcamRound", in, false, true, 1080, 1920);
    }

    void vertical_audioWebcamSquare() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.audioFile = m_audio;
        in.webcamFile = m_webcam;
        in.opts.noAudio = false;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 1, 0.7, 0.5);
        in.opts.title = "Audio Webcam Square";
        in.opts.titleColor = "#62A4C7";
        runMerge("vert_audioWebcamSquare", in, false, true, 1080, 1920);
    }

    void vertical_audioWebcamRect() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.audioFile = m_audio;
        in.webcamFile = m_webcam;
        in.opts.noAudio = false;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 2, 0.7, 0.5);
        in.opts.title = "Audio Webcam Rect";
        in.opts.titleColor = "#62A4C7";
        runMerge("vert_audioWebcamRect", in, false, true, 1080, 1920);
    }

    void vertical_oneStaticLogo() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;
        in.opts.title = "Static Logo";
        in.opts.titleColor = "#FFFFFF";
        RecordingOptions::LogoOpts lo;
        lo.path = m_staticLogo;
        lo.relX = 0.01; lo.relY = 0.7; lo.relW = 0.1; lo.relH = 0.1;
        in.opts.logos = {lo};
        runMerge("vert_oneStaticLogo", in, false, false, 1080, 1920);
    }

    void vertical_oneGifContinuous() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;
        in.opts.title = "GIF Continuous";
        in.opts.titleColor = "#FFFFFF";
        RecordingOptions::LogoOpts lo;
        lo.path = m_animGif;
        lo.gifLoop = 2;
        lo.relX = 0.01; lo.relY = 0.7; lo.relW = 0.1; lo.relH = 0.1;
        in.opts.logos = {lo};
        runMerge("vert_oneGifContinuous", in, false, false, 1080, 1920);
    }

    void vertical_oneGifOnce() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;
        in.opts.title = "GIF Once";
        in.opts.titleColor = "#FFFFFF";
        RecordingOptions::LogoOpts lo;
        lo.path = m_animGif;
        lo.gifLoop = 1;
        lo.relX = 0.01; lo.relY = 0.7; lo.relW = 0.1; lo.relH = 0.1;
        in.opts.logos = {lo};
        runMerge("vert_oneGifOnce", in, false, false, 1080, 1920);
    }

    void vertical_oneGifFirstFrame() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;
        in.opts.title = "GIF First Frame";
        in.opts.titleColor = "#FFFFFF";
        RecordingOptions::LogoOpts lo;
        lo.path = m_animGif;
        lo.gifLoop = 0;
        lo.relX = 0.01; lo.relY = 0.7; lo.relW = 0.1; lo.relH = 0.1;
        in.opts.logos = {lo};
        runMerge("vert_oneGifFirstFrame", in, false, false, 1080, 1920);
    }

    void vertical_twoLogos() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;
        in.opts.title = "Two Logos";
        in.opts.titleColor = "#FFFFFF";
        RecordingOptions::LogoOpts lo1;
        lo1.path = m_staticLogo;
        lo1.relX = 0.01; lo1.relY = 0.7; lo1.relW = 0.1; lo1.relH = 0.1;
        RecordingOptions::LogoOpts lo2;
        lo2.path = m_animGif;
        lo2.gifLoop = 2;
        lo2.relX = 0.89; lo2.relY = 0.7; lo2.relW = 0.1; lo2.relH = 0.1;
        in.opts.logos = {lo1, lo2};
        runMerge("vert_twoLogos", in, false, false, 1080, 1920);
    }

    void vertical_threeLogos() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;
        in.opts.title = "Three Logos";
        in.opts.titleColor = "#FFFFFF";
        RecordingOptions::LogoOpts lo1;
        lo1.path = m_staticLogo;
        lo1.relX = 0.01; lo1.relY = 0.7; lo1.relW = 0.08; lo1.relH = 0.08;
        RecordingOptions::LogoOpts lo2;
        lo2.path = m_animGif;
        lo2.gifLoop = 2;
        lo2.relX = 0.89; lo2.relY = 0.7; lo2.relW = 0.08; lo2.relH = 0.08;
        RecordingOptions::LogoOpts lo3;
        lo3.path = m_animGif;
        lo3.gifLoop = 1;
        lo3.relX = 0.45; lo3.relY = 0.7; lo3.relW = 0.08; lo3.relH = 0.08;
        in.opts.logos = {lo1, lo2, lo3};
        runMerge("vert_threeLogos", in, false, false, 1080, 1920);
    }

    void vertical_fullCombo() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.audioFile = m_audio;
        in.webcamFile = m_webcam;
        in.opts.noAudio = false;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 0, 0.7, 0.5);
        in.opts.title = "Full Combo";
        in.opts.titleColor = "#62A4C7";
        RecordingOptions::LogoOpts lo1;
        lo1.path = m_staticLogo;
        lo1.relX = 0.01; lo1.relY = 0.7; lo1.relW = 0.1; lo1.relH = 0.1;
        RecordingOptions::LogoOpts lo2;
        lo2.path = m_animGif;
        lo2.gifLoop = 2;
        lo2.relX = 0.89; lo2.relY = 0.7; lo2.relW = 0.1; lo2.relH = 0.1;
        in.opts.logos = {lo1, lo2};
        runMerge("vert_fullCombo", in, false, true, 1080, 1920);
    }

    void vertical_logoWebcamSquareAudio() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.audioFile = m_audio;
        in.webcamFile = m_webcam;
        in.opts.noAudio = false;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 1, 0.7, 0.5);
        in.opts.title = "Logo Square Audio";
        in.opts.titleColor = "#62A4C7";
        RecordingOptions::LogoOpts lo;
        lo.path = m_animGif;
        lo.gifLoop = 0;
        lo.relX = 0.01; lo.relY = 0.7; lo.relW = 0.1; lo.relH = 0.1;
        in.opts.logos = {lo};
        runMerge("vert_logoWebcamSquareAudio", in, false, true, 1080, 1920);
    }

    void vertical_logoWebcamRectNoAudio() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.webcamFile = m_webcam;
        in.opts.noAudio = true;
        in.opts.noWebcam = false;
        setWebcam(in.opts, 2, 0.7, 0.5);
        in.opts.title = "Logo Rect No Audio";
        in.opts.titleColor = "#62A4C7";
        RecordingOptions::LogoOpts lo;
        lo.path = m_staticLogo;
        lo.relX = 0.01; lo.relY = 0.7; lo.relW = 0.1; lo.relH = 0.1;
        in.opts.logos = {lo};
        runMerge("vert_logoWebcamRectNoAudio", in, false, false, 1080, 1920);
    }

    // =====================================================================
    // UTILITY tests (11)
    // =====================================================================

    void util_audioAnalysis() {
        auto params = Merger::analyzeAudio(m_audio);
        QVERIFY(params.valid);
        QVERIFY(params.filterString.contains("loudnorm"));
        QVERIFY(params.filterString.contains("measured_I="));
    }

    void util_audioNormalization() {
        auto params = Merger::analyzeAudio(m_audio);
        QVERIFY(params.valid);

        QString normalized = m_outputDir + "/util_normalized_audio.wav";
        bool ok = Merger::normalizeAudio(m_audio, normalized, params);
        QVERIFY(ok);
        QVERIFY(QFile::exists(normalized));

        auto info = probeFile(normalized);
        QVERIFY(info.hasAudio);
        QVERIFY(info.duration >= 1.0);
    }

    void util_audioNormalization_invalidParams() {
        Merger::LoudnormParams invalid;
        invalid.valid = false;
        QString out = m_outputDir + "/util_invalid_norm.wav";
        bool ok = Merger::normalizeAudio(m_audio, out, invalid);
        QVERIFY(!ok);
    }

    void util_concatenateTwoParts() {
        QString part0 = m_outputDir + "/util_concat_part0.mp4";
        QString part1 = m_outputDir + "/util_concat_part1.mp4";
        QVERIFY(createTestVideo(part0, 640, 360, 2.0, "#1a1a6e", "PART0"));
        QVERIFY(createTestVideo(part1, 640, 360, 2.0, "#1a1a6e", "PART1"));

        QString output = m_outputDir + "/util_concat_combined.mp4";
        QString result = Merger::concatenateParts({part0, part1}, output, "concat_exhaust");

        QCOMPARE(result, output);
        QVERIFY(QFile::exists(output));

        auto info = probeFile(output);
        QVERIFY(info.hasVideo);
        QCOMPARE(info.width, 640);
        QCOMPARE(info.height, 360);
        QVERIFY(info.duration >= 2.0);
    }

    void util_concatenateSinglePart() {
        QString result = Merger::concatenateParts({"/tmp/single_nonexistent.mp4"}, "/tmp/out.mp4");
        QCOMPARE(result, QString("/tmp/single_nonexistent.mp4"));
    }

    void util_concatenateEmpty() {
        QString result = Merger::concatenateParts({}, "/tmp/out.mp4");
        QVERIFY(result.isEmpty());
    }

    void util_getVideoDuration() {
        qint64 durUs = Merger::getVideoDurationUs(m_screen);
        // Should be approximately 2 seconds (2,000,000 microseconds)
        QVERIFY(durUs > 1500000);
        QVERIFY(durUs < 3000000);
    }

    void util_getVideoDimensions() {
        auto dim = Merger::getVideoDimensions(m_screen);
        QCOMPARE(dim.width, 640);
        QCOMPARE(dim.height, 360);
    }

    void util_getVideoDuration_nonexistent() {
        qint64 durUs = Merger::getVideoDurationUs("/tmp/no_such_video_12345.mp4");
        QCOMPARE(durUs, 0LL);
    }

    void util_getVideoDimensions_nonexistent() {
        auto dim = Merger::getVideoDimensions("/tmp/no_such_video_12345.mp4");
        QCOMPARE(dim.width, 0);
        QCOMPARE(dim.height, 0);
    }

    void util_progressCallback() {
        Merger::MergeInputs in;
        in.screenFile = m_screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;

        QString output = m_outputDir + "/util_progress_callback.mp4";
        QStringList args = Merger::buildMergedArgs(in, output);

        int maxProgress = 0;
        int callCount = 0;
        int exitCode = Merger::runFFmpegWithProgress(args, Merger::getVideoDurationUs(m_screen),
            [&maxProgress, &callCount](int pct) {
                if (pct > maxProgress) maxProgress = pct;
                callCount++;
            });

        QCOMPARE(exitCode, 0);
        QVERIFY(callCount > 0);
        QVERIFY(maxProgress > 0);
    }
};

QTEST_MAIN(TestMergerExhaustive)
#include "test_merger_exhaustive.moc"
