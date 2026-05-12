#include <QTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include "monitor/monitor.h"

// Helper: parse xrandr output the same way listMonitorsX11 does,
// so we can unit-test the regex parsing without needing a live X server.
static QList<MonitorInfo> parseXrandrOutput(const QString &output) {
    QList<MonitorInfo> monitors;
    QRegularExpression re(
        R"(^(\S+)\s+connected\s+(primary\s+)?(\d+)x(\d+)\+(\d+)\+(\d+))",
        QRegularExpression::MultilineOption);

    auto it = re.globalMatch(output);
    while (it.hasNext()) {
        auto match = it.next();
        MonitorInfo mon;
        mon.name = match.captured(1);
        mon.focused = !match.captured(2).isEmpty();
        mon.width = match.captured(3).toInt();
        mon.height = match.captured(4).toInt();
        mon.x = match.captured(5).toInt();
        mon.y = match.captured(6).toInt();
        monitors.append(mon);
    }
    return monitors;
}

class TestMonitor : public QObject {
    Q_OBJECT

private slots:
    void testMonitorInfoDefaults() {
        MonitorInfo mon;
        QVERIFY(mon.name.isEmpty());
        QCOMPARE(mon.width, 0);
        QCOMPARE(mon.height, 0);
        QCOMPARE(mon.x, 0);
        QCOMPARE(mon.y, 0);
        QCOMPARE(mon.focused, false);
    }

    void testListMonitors_returnsAtLeastFallback() {
        // On any system, listMonitors should return at least a fallback
        auto monitors = Monitor::listMonitors();
        QVERIFY(!monitors.isEmpty());
    }

    void testStripAnsi() {
        QString input = "\x1b[31mRed Text\x1b[0m Normal";
        QString expected = "Red Text Normal";
        QCOMPARE(Monitor::stripAnsi(input), expected);
    }

    void testStripAnsi_noAnsi() {
        QString input = "Plain text with no ANSI";
        QCOMPARE(Monitor::stripAnsi(input), input);
    }

    void testStripAnsi_empty() {
        QCOMPARE(Monitor::stripAnsi(""), QString(""));
    }

    void testXrandrParsing_singlePrimary() {
        QString output =
            "Screen 0: minimum 8 x 8, current 3840 x 1080, maximum 32767 x 32767\n"
            "DP-0 connected primary 1920x1080+0+0 (normal left inverted right x axis y axis) 698mm x 393mm\n"
            "   1920x1080     60.00*+  50.00    59.94\n"
            "HDMI-0 disconnected (normal left inverted right x axis y axis)\n";

        auto monitors = parseXrandrOutput(output);
        QCOMPARE(monitors.size(), 1);
        QCOMPARE(monitors[0].name, QString("DP-0"));
        QCOMPARE(monitors[0].width, 1920);
        QCOMPARE(monitors[0].height, 1080);
        QCOMPARE(monitors[0].x, 0);
        QCOMPARE(monitors[0].y, 0);
        QCOMPARE(monitors[0].focused, true);
    }

    void testXrandrParsing_multiMonitor() {
        QString output =
            "Screen 0: minimum 8 x 8, current 5760 x 1080, maximum 32767 x 32767\n"
            "DP-0 connected primary 1920x1080+0+0 (normal left inverted right x axis y axis) 698mm x 393mm\n"
            "   1920x1080     60.00*+\n"
            "DP-1 connected 2560x1440+1920+0 (normal left inverted right x axis y axis) 597mm x 336mm\n"
            "   2560x1440     59.95*+\n"
            "HDMI-0 connected 1920x1080+4480+0 (normal left inverted right x axis y axis) 521mm x 293mm\n"
            "   1920x1080     60.00*+\n";

        auto monitors = parseXrandrOutput(output);
        QCOMPARE(monitors.size(), 3);

        QCOMPARE(monitors[0].name, QString("DP-0"));
        QCOMPARE(monitors[0].focused, true);
        QCOMPARE(monitors[0].width, 1920);
        QCOMPARE(monitors[0].x, 0);

        QCOMPARE(monitors[1].name, QString("DP-1"));
        QCOMPARE(monitors[1].focused, false);
        QCOMPARE(monitors[1].width, 2560);
        QCOMPARE(monitors[1].height, 1440);
        QCOMPARE(monitors[1].x, 1920);
        QCOMPARE(monitors[1].y, 0);

        QCOMPARE(monitors[2].name, QString("HDMI-0"));
        QCOMPARE(monitors[2].focused, false);
        QCOMPARE(monitors[2].x, 4480);
    }

    void testXrandrParsing_verticalStack() {
        QString output =
            "eDP-1 connected primary 1920x1080+0+0 (normal) 344mm x 194mm\n"
            "   1920x1080     60.01*+\n"
            "HDMI-1 connected 1920x1080+0+1080 (normal) 527mm x 296mm\n"
            "   1920x1080     60.00*+\n";

        auto monitors = parseXrandrOutput(output);
        QCOMPARE(monitors.size(), 2);
        QCOMPARE(monitors[0].name, QString("eDP-1"));
        QCOMPARE(monitors[0].y, 0);
        QCOMPARE(monitors[1].name, QString("HDMI-1"));
        QCOMPARE(monitors[1].y, 1080);
    }

    void testXrandrParsing_noConnected() {
        QString output =
            "Screen 0: minimum 8 x 8, current 1920 x 1080, maximum 32767 x 32767\n"
            "DP-0 disconnected (normal left inverted right x axis y axis)\n"
            "HDMI-0 disconnected (normal left inverted right x axis y axis)\n";

        auto monitors = parseXrandrOutput(output);
        QCOMPARE(monitors.size(), 0);
    }

    void testXrandrParsing_empty() {
        auto monitors = parseXrandrOutput("");
        QCOMPARE(monitors.size(), 0);
    }
};

QTEST_MAIN(TestMonitor)
#include "test_monitor.moc"
