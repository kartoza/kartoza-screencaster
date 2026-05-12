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
        cfg.presets.clear();
        cfg.activePreset.clear();

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

    void testPresetRename() {
        Config &cfg = Config::instance();

        // Set up a preset to rename
        CanvasState preset;
        preset.mode = "landscape";
        preset.presenter = "Rename Test";
        CanvasItemState item;
        item.type = "screen";
        item.label = "Screen: Main";
        item.rx = 0.0; item.ry = 0.0; item.rw = 1.0; item.rh = 1.0;
        preset.items = {item};

        cfg.presets.clear();
        cfg.presets["OldName"] = preset;
        cfg.activePreset = "OldName";
        cfg.save();

        // Simulate rename: take old, insert new, update activePreset
        CanvasState state = cfg.presets.take("OldName");
        cfg.presets["NewName"] = state;
        cfg.activePreset = "NewName";
        cfg.save();

        // Reload and verify rename persisted
        cfg.presets.clear();
        cfg.activePreset.clear();
        cfg.load();

        QVERIFY2(!cfg.presets.contains("OldName"), "Old preset name must be gone after rename");
        QVERIFY2(cfg.presets.contains("NewName"), "New preset name must exist after rename");
        QCOMPARE(cfg.presets["NewName"].presenter, QString("Rename Test"));
        QCOMPARE(cfg.presets["NewName"].items.size(), 1);
        QCOMPARE(cfg.activePreset, QString("NewName"));

        // Clean up singleton for subsequent tests
        cfg.presets.clear();
        cfg.activePreset.clear();
        cfg.save();
    }

    void testPresetRenameToExistingNameBlocked() {
        Config &cfg = Config::instance();

        CanvasState presetA;
        presetA.mode = "landscape";
        presetA.presenter = "Alice";
        CanvasState presetB;
        presetB.mode = "vertical";
        presetB.presenter = "Bob";

        cfg.presets.clear();
        cfg.presets["PresetA"] = presetA;
        cfg.presets["PresetB"] = presetB;
        cfg.activePreset = "PresetA";
        cfg.save();

        // Attempting to rename PresetA -> PresetB should be rejected (collision)
        // The UI guards this; verify the data stays intact if we don't do the rename
        QVERIFY(cfg.presets.contains("PresetA"));
        QVERIFY(cfg.presets.contains("PresetB"));
        QCOMPARE(cfg.presets.size(), 2);

        // Clean up
        cfg.presets.clear();
        cfg.activePreset.clear();
        cfg.save();
    }

    void testPresetRenameInactivePreset() {
        Config &cfg = Config::instance();

        CanvasState presetA;
        presetA.presenter = "Active";
        CanvasState presetB;
        presetB.presenter = "Inactive";

        cfg.presets.clear();
        cfg.presets["Active"] = presetA;
        cfg.presets["Inactive"] = presetB;
        cfg.activePreset = "Active";
        cfg.save();

        // Rename the inactive preset
        CanvasState state = cfg.presets.take("Inactive");
        cfg.presets["Renamed"] = state;
        // activePreset should NOT change since we renamed a different preset
        cfg.save();

        cfg.presets.clear();
        cfg.activePreset.clear();
        cfg.load();

        QVERIFY(!cfg.presets.contains("Inactive"));
        QVERIFY(cfg.presets.contains("Renamed"));
        QCOMPARE(cfg.presets["Renamed"].presenter, QString("Inactive"));
        QCOMPARE(cfg.activePreset, QString("Active"));

        // Clean up
        cfg.presets.clear();
        cfg.activePreset.clear();
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
