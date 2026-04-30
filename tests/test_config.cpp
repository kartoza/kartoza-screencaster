#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "config/config.h"

class TestConfig : public QObject {
    Q_OBJECT

private slots:
    void testNextRecordingNumber_emptyDir() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        auto &cfg = Config::instance();
        QString oldDir = cfg.outputDir;
        cfg.outputDir = tmp.path();
        QCOMPARE(cfg.nextRecordingNumber(), 1);
        cfg.outputDir = oldDir;
    }

    void testNextRecordingNumber_withFolders() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QDir(tmp.path()).mkdir("001-first_video");
        QDir(tmp.path()).mkdir("003-third_video");
        QDir(tmp.path()).mkdir("005-fifth_video");
        QDir(tmp.path()).mkdir("random-folder"); // should be ignored

        auto &cfg = Config::instance();
        QString oldDir = cfg.outputDir;
        cfg.outputDir = tmp.path();
        QCOMPARE(cfg.nextRecordingNumber(), 6); // 5 + 1
        cfg.outputDir = oldDir;
    }

    void testNextRecordingNumber_noMatchingFolders() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QDir(tmp.path()).mkdir("some-folder");
        QDir(tmp.path()).mkdir("another-thing");

        auto &cfg = Config::instance();
        QString oldDir = cfg.outputDir;
        cfg.outputDir = tmp.path();
        QCOMPARE(cfg.nextRecordingNumber(), 1);
        cfg.outputDir = oldDir;
    }

    void testCanvasStateSerialization() {
        Config &cfg = Config::instance();
        cfg.canvasState.mode = "vertical";
        cfg.canvasState.audioEnabled = false;
        cfg.canvasState.presenter = "Test Person";

        CanvasItemState item;
        item.type = "webcam";
        item.label = "Test Webcam";
        item.rx = 0.5; item.ry = 0.6;
        item.rw = 0.3; item.rh = 0.4;
        item.device = "video0";
        item.shape = 1;
        item.gifLoop = 0;
        cfg.canvasState.items = {item};

        // Save and reload via singleton
        cfg.save();
        cfg.canvasState.items.clear();
        cfg.load();

        // Verify round-trip didn't crash and mode persisted
        QCOMPARE(cfg.canvasState.mode, QString("vertical"));
        QCOMPARE(cfg.canvasState.audioEnabled, false);
        QCOMPARE(cfg.canvasState.presenter, QString("Test Person"));
    }
};

QTEST_MAIN(TestConfig)
#include "test_config.moc"
