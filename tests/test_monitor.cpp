#include <QTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "monitor/monitor.h"

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
};

QTEST_MAIN(TestMonitor)
#include "test_monitor.moc"
