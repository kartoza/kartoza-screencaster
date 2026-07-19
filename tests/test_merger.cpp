#include <QTest>
#include "merger/merger.h"

class TestMerger : public QObject {
    Q_OBJECT

private slots:
    void testTitleSlug() {
        QCOMPARE(Merger::titleSlug("My Tutorial"), QString("my_tutorial"));
        QCOMPARE(Merger::titleSlug("Hello, World!"), QString("hello_world"));
        QCOMPARE(Merger::titleSlug("  spaces  "), QString("spaces"));
        QCOMPARE(Merger::titleSlug("---dashes---"), QString("dashes"));
        QCOMPARE(Merger::titleSlug(""), QString("recording"));
        QCOMPARE(Merger::titleSlug("___"), QString("recording"));
        QCOMPARE(Merger::titleSlug("CamelCase123"), QString("camelcase123"));
        QCOMPARE(Merger::titleSlug("already_valid"), QString("already_valid"));
    }

    void testOutputFileName() {
        QCOMPARE(Merger::outputFileName(1, "My Video"), QString("001-my_video.mp4"));
        QCOMPARE(Merger::outputFileName(42, "test"), QString("042-test.mp4"));
        QCOMPARE(Merger::outputFileName(1, "test", "vertical"), QString("001-test-vertical.mp4"));
        QCOMPARE(Merger::outputFileName(5, "", "vertical"), QString("005-recording-vertical.mp4"));
    }

    void testAppendLogoInputArgs_static() {
        RecordingOptions::LogoOpts logo;
        logo.path = "/tmp/logo.png";
        logo.gifLoop = 0;

        QStringList args;
        Merger::appendLogoInputArgs(args, logo);
        QCOMPARE(args, QStringList({"-i", "/tmp/logo.png"}));
    }

    void testAppendLogoInputArgs_gifContinuous() {
        RecordingOptions::LogoOpts logo;
        logo.path = "/tmp/anim.gif";
        logo.gifLoop = 2;

        QStringList args;
        Merger::appendLogoInputArgs(args, logo);
        QCOMPARE(args, QStringList({"-i", "/tmp/anim.gif"}));
    }

    void testAppendLogoInputArgs_gifOnce() {
        RecordingOptions::LogoOpts logo;
        logo.path = "/tmp/anim.gif";
        logo.gifLoop = 1;

        QStringList args;
        Merger::appendLogoInputArgs(args, logo);
        QCOMPARE(args, QStringList({"-ignore_loop", "1", "-i", "/tmp/anim.gif"}));
    }

    void testAppendLogoInputArgs_gifFirstFrame() {
        RecordingOptions::LogoOpts logo;
        logo.path = "/tmp/anim.gif";
        logo.gifLoop = 0;

        QStringList args;
        Merger::appendLogoInputArgs(args, logo);
        QCOMPARE(args, QStringList({"-i", "/tmp/anim.gif"}));
    }

    void testBuildMergedArgs_screenOnly() {
        Merger::MergeInputs in;
        in.screenFile = "/tmp/screen.mp4";
        in.opts.noScreen = false;
        in.opts.noAudio = true;

        QStringList args = Merger::buildMergedArgs(in, "/tmp/output.mp4");
        QVERIFY(args.contains("-y"));
        QVERIFY(args.contains("-an"));
        QVERIFY(args.contains("/tmp/output.mp4"));
        QVERIFY(!args.contains("-shortest"));
    }

    void testBuildVerticalArgs_hasScreenInput() {
        Merger::MergeInputs in;
        in.screenFile = "/tmp/screen.mp4";
        in.opts.noScreen = false;
        in.opts.title = "Test Video";
        in.opts.titleColor = "#FF0000";

        QStringList args = Merger::buildVerticalArgs(in, "/tmp/vert.mp4");
        QVERIFY(args.contains("-filter_complex"));
        QVERIFY(args.contains("1080x1920"));
        QVERIFY(args.contains("/tmp/vert.mp4"));

        // Check title color appears in filter
        int fcIdx = args.indexOf("-filter_complex");
        QVERIFY(fcIdx >= 0);
        QString filter = args[fcIdx + 1];
        QVERIFY(filter.contains("#FF0000"));
        QVERIFY(filter.contains("Test Video"));
    }

    void testConcatenateParts_singlePart() {
        // Single part should return the file directly without running ffmpeg
        QString result = Merger::concatenateParts({"/tmp/nonexistent.mp4"}, "/tmp/out.mp4");
        // Returns first part (even if file doesn't exist, it's just returned as-is)
        QCOMPARE(result, QString("/tmp/nonexistent.mp4"));
    }

    void testAppendTextBoxFilters_rendersBox() {
        QVector<RecordingOptions::TextBox> boxes;
        RecordingOptions::TextBox tb;
        tb.text = "Hello"; tb.color = "#00FF00"; tb.fontFamily = "Sans";
        tb.relX = 0.1; tb.relY = 0.2; tb.relW = 0.3; tb.relH = 0.1;
        boxes.append(tb);

        QString filter;
        QString current = "[in]";
        int vIdx = 0;
        Merger::appendTextBoxFilters(filter, current, vIdx, boxes, 1920, 1080);

        QVERIFY(filter.contains("drawtext"));
        QVERIFY(filter.contains("Hello"));
        QVERIFY(filter.contains("#00FF00"));
        QCOMPARE(current, QString("[v1]"));
    }

    void testAppendTextBoxFilters_skipsEmpty() {
        QVector<RecordingOptions::TextBox> boxes;
        RecordingOptions::TextBox tb; tb.text = "   ";
        boxes.append(tb);
        QString filter;
        QString current = "[in]";
        int vIdx = 0;
        Merger::appendTextBoxFilters(filter, current, vIdx, boxes, 1920, 1080);
        QVERIFY(filter.isEmpty());
        QCOMPARE(current, QString("[in]"));
    }

    void testBuildMergedArgs_textBoxForcesFilter() {
        Merger::MergeInputs in;
        in.screenFile = "/tmp/screen.mp4";
        in.opts.noScreen = false;
        RecordingOptions::TextBox tb;
        tb.text = "Label"; tb.color = "#abcdef";
        in.opts.textBoxes.append(tb);

        QStringList args = Merger::buildMergedArgs(in, "/tmp/output.mp4");
        QVERIFY(args.contains("-filter_complex"));
        int fc = args.indexOf("-filter_complex");
        QVERIFY(args[fc + 1].contains("Label"));
        QVERIFY(args[fc + 1].contains("[outv]"));
    }

    void testBuildVerticalArgs_textBox() {
        Merger::MergeInputs in;
        in.screenFile = "/tmp/screen.mp4";
        in.opts.noScreen = false;
        RecordingOptions::TextBox tb;
        tb.text = "Caption"; tb.color = "#123456";
        tb.relX = 0.1; tb.relY = 0.1; tb.relW = 0.5; tb.relH = 0.1;
        in.opts.textBoxes.append(tb);

        QStringList args = Merger::buildVerticalArgs(in, "/tmp/vert.mp4");
        int fc = args.indexOf("-filter_complex");
        QVERIFY(fc >= 0);
        QString filter = args[fc + 1];
        QVERIFY(filter.contains("Caption"));
        QVERIFY(filter.contains("#123456"));
        QVERIFY(filter.contains("[outv]"));
    }

    void testConcatenateParts_empty() {
        QString result = Merger::concatenateParts({}, "/tmp/out.mp4");
        QVERIFY(result.isEmpty());
    }
};

QTEST_MAIN(TestMerger)
#include "test_merger.moc"
