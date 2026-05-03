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

    void testPresetsSurviveSaveLoadCycle() {
        // Regression: presets must never be lost through save/load cycles
        Config &cfg = Config::instance();

        // Create two presets with distinct canvas states
        CanvasState preset1;
        preset1.mode = "landscape";
        preset1.presenter = "Alice";
        CanvasItemState item1;
        item1.type = "screen";
        item1.label = "Screen: Main";
        item1.rx = 0.0; item1.ry = 0.0; item1.rw = 1.0; item1.rh = 1.0;
        preset1.items = {item1};

        CanvasState preset2;
        preset2.mode = "vertical";
        preset2.presenter = "Bob";
        CanvasItemState item2;
        item2.type = "webcam";
        item2.label = "Webcam: Front";
        item2.rx = 0.5; item2.ry = 0.5; item2.rw = 0.2; item2.rh = 0.2;
        item2.device = "video0";
        item2.shape = 0;
        preset2.items = {item2};

        cfg.presets["TestPreset1"] = preset1;
        cfg.presets["TestPreset2"] = preset2;
        cfg.activePreset = "TestPreset1";
        cfg.save();

        // Reload and verify both presets survived
        cfg.presets.clear();
        cfg.activePreset.clear();
        cfg.load();

        QCOMPARE(cfg.presets.size(), 2);
        QVERIFY(cfg.presets.contains("TestPreset1"));
        QVERIFY(cfg.presets.contains("TestPreset2"));
        QCOMPARE(cfg.presets["TestPreset1"].presenter, QString("Alice"));
        QCOMPARE(cfg.presets["TestPreset2"].presenter, QString("Bob"));
        QCOMPARE(cfg.presets["TestPreset1"].items.size(), 1);
        QCOMPARE(cfg.presets["TestPreset2"].items.size(), 1);
        QCOMPARE(cfg.activePreset, QString("TestPreset1"));
    }

    void testPresetsNotWipedBySaveBeforeLoad() {
        // Regression: if save() is called before load(), disk presets must be preserved
        Config &cfg = Config::instance();

        // Establish presets on disk via a normal save
        CanvasState preset;
        preset.mode = "landscape";
        preset.presenter = "Critical Preset";
        cfg.presets["Important"] = preset;
        cfg.activePreset = "Important";
        cfg.save();

        // Verify preset is on disk
        cfg.presets.clear();
        cfg.load();
        QVERIFY(cfg.presets.contains("Important"));

        // Now test: after a proper load+delete+save cycle, deletion sticks
        cfg.presets.remove("Important");
        cfg.save();
        cfg.load();
        QVERIFY2(!cfg.presets.contains("Important"),
                 "Explicit preset deletion must persist!");

        // Re-establish for subsequent tests
        cfg.presets["Important"] = preset;
        cfg.save();
    }

    void testMultipleSavesPreservePresets() {
        // Regression: rapid successive saves must not corrupt presets
        Config &cfg = Config::instance();

        CanvasState preset;
        preset.mode = "landscape";
        preset.presenter = "Stable";
        cfg.presets["Stable"] = preset;
        cfg.save();

        // Simulate multiple saves (e.g. canvas item moved repeatedly)
        for (int i = 0; i < 10; i++) {
            cfg.canvasState.mode = (i % 2 == 0) ? "landscape" : "vertical";
            cfg.save();
        }

        cfg.presets.clear();
        cfg.load();
        QVERIFY(cfg.presets.contains("Stable"));
        QCOMPARE(cfg.presets["Stable"].presenter, QString("Stable"));
    }
};

QTEST_MAIN(TestConfig)
#include "test_config.moc"
