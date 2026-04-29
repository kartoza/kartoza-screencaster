#include <QTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "merger/merger.h"

// Helper to probe video properties
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

    // Get streams
    QProcess probe;
    probe.start("ffprobe", {"-v", "error",
        "-show_entries", "stream=codec_type,codec_name,width,height",
        "-show_entries", "format=duration",
        "-of", "json", path});
    probe.waitForFinished(10000);

    auto doc = QJsonDocument::fromJson(probe.readAllStandardOutput());
    auto root = doc.object();

    // Parse streams
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

    // Parse format duration
    auto format = root["format"].toObject();
    r.duration = format["duration"].toString().toDouble();

    return r;
}

// Helper: create a synthetic test video (color bars, 2 seconds)
static bool createTestVideo(const QString &path, int w, int h, double secs = 2.0) {
    QProcess p;
    p.start("ffmpeg", {"-y", "-f", "lavfi",
        "-i", QString("color=c=blue:s=%1x%2:d=%3").arg(w).arg(h).arg(secs, 0, 'f', 1),
        "-c:v", "libx264", "-preset", "ultrafast", "-crf", "28",
        "-pix_fmt", "yuv420p", path});
    p.waitForFinished(15000);
    return p.exitCode() == 0 && QFile::exists(path);
}

// Helper: create a synthetic test audio (sine wave, 2 seconds)
static bool createTestAudio(const QString &path, double secs = 2.0) {
    QProcess p;
    p.start("ffmpeg", {"-y", "-f", "lavfi",
        "-i", QString("sine=frequency=440:duration=%1").arg(secs, 0, 'f', 1),
        "-ac", "2", path});
    p.waitForFinished(10000);
    return p.exitCode() == 0 && QFile::exists(path);
}

// Helper: create a small test PNG logo
static bool createTestLogo(const QString &path, int w = 100, int h = 100) {
    QProcess p;
    p.start("ffmpeg", {"-y", "-f", "lavfi",
        "-i", QString("color=c=red:s=%1x%2:d=0.04").arg(w).arg(h),
        "-frames:v", "1", path});
    p.waitForFinished(5000);
    return p.exitCode() == 0 && QFile::exists(path);
}

class TestPipeline : public QObject {
    Q_OBJECT

private slots:
    // --- Concatenation ---

    void testConcatenateTwoParts() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QString part0 = tmp.filePath("screen_part000.mp4");
        QString part1 = tmp.filePath("screen_part001.mp4");
        QVERIFY(createTestVideo(part0, 640, 480, 1.0));
        QVERIFY(createTestVideo(part1, 640, 480, 1.0));

        QString output = tmp.filePath("screen_combined.mp4");
        QString result = Merger::concatenateParts({part0, part1}, output, "concat_screen");

        QCOMPARE(result, output);
        QVERIFY(QFile::exists(output));

        auto info = probeFile(output);
        QVERIFY(info.hasVideo);
        QCOMPARE(info.width, 640);
        QCOMPARE(info.height, 480);
        QVERIFY(info.duration >= 1.5); // ~2 seconds combined
    }

    // --- Audio analysis ---

    void testAnalyzeAudio() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QString audio = tmp.filePath("test_audio.wav");
        QVERIFY(createTestAudio(audio, 2.0));

        auto params = Merger::analyzeAudio(audio);
        QVERIFY(params.valid);
        QVERIFY(params.filterString.contains("loudnorm"));
        QVERIFY(params.filterString.contains("measured_I="));
    }

    void testNormalizeAudio() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QString audio = tmp.filePath("test_audio.wav");
        QVERIFY(createTestAudio(audio, 2.0));

        auto params = Merger::analyzeAudio(audio);
        QVERIFY(params.valid);

        QString normalized = tmp.filePath("audio_normalized.wav");
        bool ok = Merger::normalizeAudio(audio, normalized, params);
        QVERIFY(ok);
        QVERIFY(QFile::exists(normalized));

        auto info = probeFile(normalized);
        QVERIFY(info.hasAudio);
        QVERIFY(info.duration >= 1.5);
    }

    // --- Merged video: screen only ---

    void testMergedVideo_screenOnly() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QString screen = tmp.filePath("screen.mp4");
        QVERIFY(createTestVideo(screen, 1920, 1080, 2.0));

        Merger::MergeInputs in;
        in.screenFile = screen;
        in.opts.noAudio = true;
        in.opts.noWebcam = true;

        QString output = tmp.filePath("merged.mp4");
        QStringList args = Merger::buildMergedArgs(in, output);

        int exitCode = Merger::runFFmpegWithProgress(args, Merger::getVideoDurationUs(screen));
        QCOMPARE(exitCode, 0);
        QVERIFY(QFile::exists(output));

        auto info = probeFile(output);
        QVERIFY(info.hasVideo);
        QVERIFY(!info.hasAudio);
        QVERIFY(info.duration >= 1.5);
    }

    // --- Merged video: screen + audio ---

    void testMergedVideo_screenPlusAudio() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QString screen = tmp.filePath("screen.mp4");
        QString audio = tmp.filePath("audio.wav");
        QVERIFY(createTestVideo(screen, 1280, 720, 2.0));
        QVERIFY(createTestAudio(audio, 2.0));

        Merger::MergeInputs in;
        in.screenFile = screen;
        in.audioFile = audio;
        in.opts.noAudio = false;
        in.opts.noWebcam = true;

        QString output = tmp.filePath("merged.mp4");
        QStringList args = Merger::buildMergedArgs(in, output);

        int exitCode = Merger::runFFmpegWithProgress(args, Merger::getVideoDurationUs(screen));
        QCOMPARE(exitCode, 0);

        auto info = probeFile(output);
        QVERIFY(info.hasVideo);
        QVERIFY(info.hasAudio);
        QCOMPARE(info.audioCodec, QString("aac"));
    }

    // --- Merged video: screen + audio + webcam ---

    void testMergedVideo_withWebcam() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QString screen = tmp.filePath("screen.mp4");
        QString audio = tmp.filePath("audio.wav");
        QString webcam = tmp.filePath("webcam.mp4");
        QVERIFY(createTestVideo(screen, 1920, 1080, 2.0));
        QVERIFY(createTestAudio(audio, 2.0));
        QVERIFY(createTestVideo(webcam, 640, 480, 2.0));

        Merger::MergeInputs in;
        in.screenFile = screen;
        in.audioFile = audio;
        in.webcamFile = webcam;
        in.opts.noScreen = false;
        in.opts.noAudio = false;
        in.opts.noWebcam = false;
        in.opts.webcamShape = 0; // round bubble
        in.opts.webcamRelX = 0.8;
        in.opts.webcamRelY = 0.75;
        in.opts.webcamRelW = 0.15;
        in.opts.webcamRelH = 0.2;

        QString output = tmp.filePath("merged.mp4");
        QStringList args = Merger::buildMergedArgs(in, output);

        // Verify filter_complex is present (webcam overlay)
        QVERIFY(args.contains("-filter_complex"));

        int exitCode = Merger::runFFmpegWithProgress(args, Merger::getVideoDurationUs(screen));
        QCOMPARE(exitCode, 0);

        auto info = probeFile(output);
        QVERIFY(info.hasVideo);
        QVERIFY(info.hasAudio);
    }

    // --- Merged video: with logos ---

    void testMergedVideo_withLogos() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QString screen = tmp.filePath("screen.mp4");
        QString leftLogo = tmp.filePath("left.png");
        QString rightLogo = tmp.filePath("right.png");
        QVERIFY(createTestVideo(screen, 1920, 1080, 2.0));
        QVERIFY(createTestLogo(leftLogo, 200, 200));
        QVERIFY(createTestLogo(rightLogo, 200, 200));

        Merger::MergeInputs in;
        in.screenFile = screen;
        in.opts.noAudio = true;
        in.opts.leftLogo.path = leftLogo;
        in.opts.leftLogo.relX = 0.01; in.opts.leftLogo.relY = 0.01;
        in.opts.leftLogo.relW = 0.1; in.opts.leftLogo.relH = 0.1;
        in.opts.rightLogo.path = rightLogo;
        in.opts.rightLogo.relX = 0.89; in.opts.rightLogo.relY = 0.01;
        in.opts.rightLogo.relW = 0.1; in.opts.rightLogo.relH = 0.1;

        QString output = tmp.filePath("merged.mp4");
        QStringList args = Merger::buildMergedArgs(in, output);
        QVERIFY(args.contains("-filter_complex"));

        int exitCode = Merger::runFFmpegWithProgress(args, Merger::getVideoDurationUs(screen));
        QCOMPARE(exitCode, 0);

        auto info = probeFile(output);
        QVERIFY(info.hasVideo);
    }

    // --- Vertical video: 1080x1920 output ---

    void testVerticalVideo_screenOnly() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QString screen = tmp.filePath("screen.mp4");
        QVERIFY(createTestVideo(screen, 1920, 1080, 2.0));

        Merger::MergeInputs in;
        in.screenFile = screen;
        in.opts.noAudio = true;
        in.opts.title = "Test Title";
        in.opts.titleColor = "#FFFFFF";

        QString output = tmp.filePath("vertical.mp4");
        QStringList args = Merger::buildVerticalArgs(in, output);

        int exitCode = Merger::runFFmpegWithProgress(args, Merger::getVideoDurationUs(screen));
        QCOMPARE(exitCode, 0);

        auto info = probeFile(output);
        QVERIFY(info.hasVideo);
        QCOMPARE(info.width, 1080);
        QCOMPARE(info.height, 1920);
    }

    void testVerticalVideo_withAudioAndWebcam() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QString screen = tmp.filePath("screen.mp4");
        QString audio = tmp.filePath("audio.wav");
        QString webcam = tmp.filePath("webcam.mp4");
        QVERIFY(createTestVideo(screen, 1920, 1080, 2.0));
        QVERIFY(createTestAudio(audio, 2.0));
        QVERIFY(createTestVideo(webcam, 640, 480, 2.0));

        Merger::MergeInputs in;
        in.screenFile = screen;
        in.audioFile = audio;
        in.webcamFile = webcam;
        in.opts.noScreen = false;
        in.opts.noAudio = false;
        in.opts.noWebcam = false;
        in.opts.title = "Webcam Test";
        in.opts.titleColor = "#62A4C7";
        in.opts.webcamShape = 0;
        in.opts.webcamRelX = 0.7;
        in.opts.webcamRelY = 0.5;
        in.opts.webcamRelW = 0.2;
        in.opts.webcamRelH = 0.15;

        QString output = tmp.filePath("vertical.mp4");
        QStringList args = Merger::buildVerticalArgs(in, output);

        int exitCode = Merger::runFFmpegWithProgress(args, Merger::getVideoDurationUs(screen));
        QCOMPARE(exitCode, 0);

        auto info = probeFile(output);
        QVERIFY(info.hasVideo);
        QVERIFY(info.hasAudio);
        QCOMPARE(info.width, 1080);
        QCOMPARE(info.height, 1920);
        QCOMPARE(info.audioCodec, QString("aac"));
    }

    // --- Progress callback ---

    void testProgressCallback() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QString screen = tmp.filePath("screen.mp4");
        QVERIFY(createTestVideo(screen, 640, 480, 2.0));

        Merger::MergeInputs in;
        in.screenFile = screen;
        in.opts.noAudio = true;

        QString output = tmp.filePath("merged.mp4");
        QStringList args = Merger::buildMergedArgs(in, output);

        int maxProgress = 0;
        int callCount = 0;
        int exitCode = Merger::runFFmpegWithProgress(args, Merger::getVideoDurationUs(screen),
            [&maxProgress, &callCount](int pct) {
                if (pct > maxProgress) maxProgress = pct;
                callCount++;
            });

        QCOMPARE(exitCode, 0);
        QVERIFY(callCount > 0); // callback was invoked
        QVERIFY(maxProgress > 0); // progress was reported
    }

    // --- getDuration ---

    void testGetVideoDuration() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QString video = tmp.filePath("test.mp4");
        QVERIFY(createTestVideo(video, 320, 240, 3.0));

        qint64 durationUs = Merger::getVideoDurationUs(video);
        // Should be approximately 3 seconds (3,000,000 microseconds)
        QVERIFY(durationUs > 2500000);
        QVERIFY(durationUs < 4000000);
    }

    void testGetVideoDuration_nonexistent() {
        qint64 durationUs = Merger::getVideoDurationUs("/tmp/nonexistent_video.mp4");
        QCOMPARE(durationUs, 0LL);
    }
};

QTEST_MAIN(TestPipeline)
#include "test_pipeline.moc"
