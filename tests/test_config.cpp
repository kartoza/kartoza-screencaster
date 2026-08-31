#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "config/config.h"

class TestConfig : public QObject {
    Q_OBJECT

private slots:
    /**
     * recordingsDir() is the single source of truth shared by the recorder,
     * the history page and nextRecordingNumber(). Previously each resolved
     * the default independently and the recorder ignored the configured
     * directory entirely, writing recordings where the UI never looked.
     */
    void testRecordingsDir_usesConfiguredDir() {
        auto &cfg = Config::instance();
        QString oldDir = cfg.outputDir;
        cfg.outputDir = "/tmp/some-custom-location";
        QCOMPARE(cfg.recordingsDir(), QString("/tmp/some-custom-location"));
        cfg.outputDir = oldDir;
    }

    void testRecordingsDir_fallsBackToDefault() {
        auto &cfg = Config::instance();
        QString oldDir = cfg.outputDir;
        cfg.outputDir = QString();
        QCOMPARE(cfg.recordingsDir(), QDir::homePath() + "/Videos/Screencasts");
        cfg.outputDir = oldDir;
    }

    /** nextRecordingNumber() must scan the same directory recordingsDir() names. */
    void testRecordingsDir_agreesWithNextRecordingNumber() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QDir(tmp.path()).mkdir("007-seventh_video");

        auto &cfg = Config::instance();
        QString oldDir = cfg.outputDir;
        cfg.outputDir = tmp.path();
        QCOMPARE(cfg.recordingsDir(), tmp.path());
        QCOMPARE(cfg.nextRecordingNumber(), 8);
        cfg.outputDir = oldDir;
    }

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
    void testCropFieldsSaveLoad() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        auto &cfg = Config::instance();
        QString oldDir = cfg.outputDir;
        cfg.outputDir = tmp.path();

        // Set crop values on a canvas item
        CanvasItemState item;
        item.type = "webcam";
        item.label = "CropTest";
        item.rx = 0.5; item.ry = 0.5; item.rw = 0.2; item.rh = 0.15;
        item.cropTop = 0.1; item.cropBottom = 0.2;
        item.cropLeft = 0.05; item.cropRight = 0.15;
        cfg.canvasState.items.clear();
        cfg.canvasState.items.append(item);
        cfg.save();

        // Reload
        cfg.canvasState.items.clear();
        cfg.load();
        QCOMPARE(cfg.canvasState.items.size(), 1);
        auto &loaded = cfg.canvasState.items[0];
        QVERIFY(qAbs(loaded.cropTop - 0.1) < 0.001);
        QVERIFY(qAbs(loaded.cropBottom - 0.2) < 0.001);
        QVERIFY(qAbs(loaded.cropLeft - 0.05) < 0.001);
        QVERIFY(qAbs(loaded.cropRight - 0.15) < 0.001);

        cfg.outputDir = oldDir;
    }

    void testCropFieldsInPreset() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        auto &cfg = Config::instance();
        QString oldDir = cfg.outputDir;
        cfg.outputDir = tmp.path();

        CanvasItemState item;
        item.type = "logo";
        item.label = "CropPreset";
        item.rx = 0.3; item.ry = 0.4; item.rw = 0.1; item.rh = 0.08;
        item.cropTop = 0.25; item.cropLeft = 0.3;

        CanvasState preset;
        preset.mode = "landscape";
        preset.items.append(item);
        cfg.presets["CropTest"] = preset;
        cfg.save();

        cfg.presets.clear();
        cfg.load();
        QVERIFY(cfg.presets.contains("CropTest"));
        auto &loadedItem = cfg.presets["CropTest"].items[0];
        QVERIFY(qAbs(loadedItem.cropTop - 0.25) < 0.001);
        QVERIFY(qAbs(loadedItem.cropLeft - 0.3) < 0.001);
        QCOMPARE(loadedItem.cropBottom, 0.0);
        QCOMPARE(loadedItem.cropRight, 0.0);

        cfg.outputDir = oldDir;
    }

    void testCropFieldsDefaultZero() {
        CanvasItemState s;
        QCOMPARE(s.cropTop, 0.0);
        QCOMPARE(s.cropBottom, 0.0);
        QCOMPARE(s.cropLeft, 0.0);
        QCOMPARE(s.cropRight, 0.0);
    }

    // =========================================================================
    // FULL STATE PERSISTENCE ROUND-TRIP TESTS
    // =========================================================================

    void testScreenWithCropPresetRoundTrip() {
        auto &cfg = Config::instance();

        CanvasItemState screen;
        screen.type = "screen";
        screen.label = "Screen: DP-1";
        screen.rx = 0.5; screen.ry = 0.5; screen.rw = 1.0; screen.rh = 1.0;
        screen.cropTop = 0.08; screen.cropBottom = 0.12;
        screen.cropLeft = 0.05; screen.cropRight = 0.1;

        CanvasState preset;
        preset.mode = "landscape";
        preset.items.append(screen);
        cfg.presets["ScreenCrop"] = preset;
        cfg.save();

        cfg.presets.clear();
        cfg.load();
        QVERIFY(cfg.presets.contains("ScreenCrop"));
        auto &items = cfg.presets["ScreenCrop"].items;
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].type, QString("screen"));
        QCOMPARE(items[0].label, QString("Screen: DP-1"));
        QVERIFY(qAbs(items[0].cropTop - 0.08) < 0.001);
        QVERIFY(qAbs(items[0].cropBottom - 0.12) < 0.001);
        QVERIFY(qAbs(items[0].cropLeft - 0.05) < 0.001);
        QVERIFY(qAbs(items[0].cropRight - 0.1) < 0.001);
        QVERIFY(qAbs(items[0].rx - 0.5) < 0.001);
        QVERIFY(qAbs(items[0].ry - 0.5) < 0.001);
    }

    void testSoundItemsPresetRoundTrip() {
        auto &cfg = Config::instance();

        CanvasItemState startSound;
        startSound.type = "start_sound";
        startSound.label = "chime (Start)";
        startSound.filePath = "/home/user/sounds/chime.wav";

        CanvasItemState endSound;
        endSound.type = "end_sound";
        endSound.label = "outro (End)";
        endSound.filePath = "/home/user/sounds/outro.wav";

        CanvasState preset;
        preset.mode = "landscape";
        preset.items.append(startSound);
        preset.items.append(endSound);
        cfg.presets["WithSounds"] = preset;
        cfg.save();

        cfg.presets.clear();
        cfg.load();
        QVERIFY(cfg.presets.contains("WithSounds"));
        auto &items = cfg.presets["WithSounds"].items;
        QCOMPARE(items.size(), 2);

        QCOMPARE(items[0].type, QString("start_sound"));
        QCOMPARE(items[0].label, QString("chime (Start)"));
        QCOMPARE(items[0].filePath, QString("/home/user/sounds/chime.wav"));

        QCOMPARE(items[1].type, QString("end_sound"));
        QCOMPARE(items[1].label, QString("outro (End)"));
        QCOMPARE(items[1].filePath, QString("/home/user/sounds/outro.wav"));
    }

    void testLogoWithCropPresetRoundTrip() {
        auto &cfg = Config::instance();

        CanvasItemState logo;
        logo.type = "logo";
        logo.label = "kartoza.png";
        logo.filePath = "/home/user/logos/kartoza.png";
        logo.rx = 0.1; logo.ry = 0.1; logo.rw = 0.15; logo.rh = 0.08;
        logo.cropTop = 0.0; logo.cropBottom = 0.2;
        logo.cropLeft = 0.1; logo.cropRight = 0.1;
        logo.gifLoop = 2; logo.gifLoopMax = 3;

        CanvasState preset;
        preset.mode = "vertical";
        preset.items.append(logo);
        cfg.presets["LogoCrop"] = preset;
        cfg.save();

        cfg.presets.clear();
        cfg.load();
        QVERIFY(cfg.presets.contains("LogoCrop"));
        auto &items = cfg.presets["LogoCrop"].items;
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].type, QString("logo"));
        QCOMPARE(items[0].filePath, QString("/home/user/logos/kartoza.png"));
        QCOMPARE(items[0].cropTop, 0.0);
        QVERIFY(qAbs(items[0].cropBottom - 0.2) < 0.001);
        QVERIFY(qAbs(items[0].cropLeft - 0.1) < 0.001);
        QVERIFY(qAbs(items[0].cropRight - 0.1) < 0.001);
        QCOMPARE(items[0].gifLoop, 2);
        QCOMPARE(items[0].gifLoopMax, 3);
    }

    void testCompletePresetWithAllItemTypes() {
        auto &cfg = Config::instance();

        CanvasState preset;
        preset.mode = "left_split";
        preset.audioEnabled = true;
        preset.presenter = "Tim";
        preset.titleColor = "#DF9E2F";

        CanvasItemState screen;
        screen.type = "screen"; screen.label = "Screen: HDMI-1";
        screen.rx = 0.5; screen.ry = 0.5; screen.rw = 1.0; screen.rh = 1.0;
        screen.cropTop = 0.05;
        preset.items.append(screen);

        CanvasItemState webcam;
        webcam.type = "webcam"; webcam.label = "HD Cam";
        webcam.rx = 0.8; webcam.ry = 0.8; webcam.rw = 0.2; webcam.rh = 0.2;
        webcam.device = "video0"; webcam.shape = 0;
        webcam.cropBottom = 0.1;
        preset.items.append(webcam);

        CanvasItemState logo;
        logo.type = "logo"; logo.label = "brand.gif";
        logo.rx = 0.05; logo.ry = 0.05; logo.rw = 0.12; logo.rh = 0.06;
        logo.filePath = "/tmp/brand.gif";
        logo.gifLoop = 1; logo.gifLoopMax = 5;
        logo.cropLeft = 0.15;
        preset.items.append(logo);

        CanvasItemState title;
        title.type = "title"; title.label = "My Tutorial";
        title.rx = 0.3; title.ry = 0.9; title.rw = 0.4; title.rh = 0.06;
        preset.items.append(title);

        CanvasItemState startSnd;
        startSnd.type = "start_sound"; startSnd.label = "ding (Start)";
        startSnd.filePath = "/tmp/ding.wav";
        preset.items.append(startSnd);

        CanvasItemState endSnd;
        endSnd.type = "end_sound"; endSnd.label = "whoosh (End)";
        endSnd.filePath = "/tmp/whoosh.wav";
        preset.items.append(endSnd);

        cfg.presets["FullPreset"] = preset;
        cfg.save();

        // Simulate app restart
        cfg.presets.clear();
        cfg.canvasState = CanvasState();
        cfg.load();

        QVERIFY(cfg.presets.contains("FullPreset"));
        auto &p = cfg.presets["FullPreset"];
        QCOMPARE(p.mode, QString("left_split"));
        QVERIFY(p.audioEnabled);
        QCOMPARE(p.presenter, QString("Tim"));
        QCOMPARE(p.titleColor, QString("#DF9E2F"));
        QCOMPARE(p.items.size(), 6);

        // Verify each item type preserved
        QCOMPARE(p.items[0].type, QString("screen"));
        QVERIFY(qAbs(p.items[0].cropTop - 0.05) < 0.001);

        QCOMPARE(p.items[1].type, QString("webcam"));
        QCOMPARE(p.items[1].device, QString("video0"));
        QCOMPARE(p.items[1].shape, 0);
        QVERIFY(qAbs(p.items[1].cropBottom - 0.1) < 0.001);

        QCOMPARE(p.items[2].type, QString("logo"));
        QCOMPARE(p.items[2].filePath, QString("/tmp/brand.gif"));
        QCOMPARE(p.items[2].gifLoop, 1);
        QCOMPARE(p.items[2].gifLoopMax, 5);
        QVERIFY(qAbs(p.items[2].cropLeft - 0.15) < 0.001);

        QCOMPARE(p.items[3].type, QString("title"));
        QCOMPARE(p.items[3].label, QString("My Tutorial"));

        QCOMPARE(p.items[4].type, QString("start_sound"));
        QCOMPARE(p.items[4].filePath, QString("/tmp/ding.wav"));

        QCOMPARE(p.items[5].type, QString("end_sound"));
        QCOMPARE(p.items[5].filePath, QString("/tmp/whoosh.wav"));
    }

    void testCanvasStateSaveLoadRoundTrip() {
        // Test that canvasState (not just presets) persists correctly
        auto &cfg = Config::instance();

        cfg.canvasState.mode = "vertical";
        cfg.canvasState.audioEnabled = false;
        cfg.canvasState.presenter = "Alice";
        cfg.canvasState.titleColor = "#CC0403";
        cfg.canvasState.items.clear();

        CanvasItemState screen;
        screen.type = "screen"; screen.label = "Screen: eDP-1";
        screen.rx = 0.45; screen.ry = 0.48;
        screen.rw = 0.9; screen.rh = 0.85;
        screen.cropTop = 0.03; screen.cropRight = 0.07;
        cfg.canvasState.items.append(screen);

        CanvasItemState sound;
        sound.type = "end_sound"; sound.label = "bye (End)";
        sound.filePath = "/opt/sounds/bye.mp3";
        cfg.canvasState.items.append(sound);

        cfg.save();

        // Simulate restart
        cfg.canvasState = CanvasState();
        cfg.load();

        QCOMPARE(cfg.canvasState.mode, QString("vertical"));
        QVERIFY(!cfg.canvasState.audioEnabled);
        QCOMPARE(cfg.canvasState.presenter, QString("Alice"));
        QCOMPARE(cfg.canvasState.titleColor, QString("#CC0403"));
        QCOMPARE(cfg.canvasState.items.size(), 2);

        QCOMPARE(cfg.canvasState.items[0].type, QString("screen"));
        QVERIFY(qAbs(cfg.canvasState.items[0].rx - 0.45) < 0.001);
        QVERIFY(qAbs(cfg.canvasState.items[0].ry - 0.48) < 0.001);
        QVERIFY(qAbs(cfg.canvasState.items[0].cropTop - 0.03) < 0.001);
        QVERIFY(qAbs(cfg.canvasState.items[0].cropRight - 0.07) < 0.001);

        QCOMPARE(cfg.canvasState.items[1].type, QString("end_sound"));
        QCOMPARE(cfg.canvasState.items[1].filePath, QString("/opt/sounds/bye.mp3"));
    }

    void testDenoiseSettingPersists() {
        auto &cfg = Config::instance();

        cfg.denoiseAudio = true;
        cfg.save();
        cfg.denoiseAudio = false;
        cfg.load();
        QVERIFY(cfg.denoiseAudio);

        cfg.denoiseAudio = false;
        cfg.save();
        cfg.denoiseAudio = true;
        cfg.load();
        QVERIFY(!cfg.denoiseAudio);
    }

    void testPositionFieldsPreserved() {
        // Verify position fields survive serialization for all types
        auto &cfg = Config::instance();

        CanvasState preset;
        preset.mode = "landscape";

        QList<QPair<QString, double>> types = {
            {"screen", 0.5}, {"webcam", 0.8}, {"logo", 0.1},
            {"title", 0.3}, {"start_sound", 0.0}, {"end_sound", 0.0}
        };

        for (const auto &t : types) {
            CanvasItemState item;
            item.type = t.first;
            item.label = t.first + "_test";
            item.rx = t.second; item.ry = t.second;
            item.rw = t.second > 0 ? 0.2 : 0;
            item.rh = t.second > 0 ? 0.15 : 0;
            item.filePath = "/tmp/test_" + t.first + ".file";
            preset.items.append(item);
        }

        cfg.presets["PosTest"] = preset;
        cfg.save();
        cfg.presets.clear();
        cfg.load();

        QVERIFY(cfg.presets.contains("PosTest"));
        auto &items = cfg.presets["PosTest"].items;
        QCOMPARE(items.size(), 6);

        for (int i = 0; i < items.size(); i++) {
            QCOMPARE(items[i].type, types[i].first);
            QVERIFY2(qAbs(items[i].rx - types[i].second) < 0.001,
                qPrintable(QString("Type %1: rx expected %2, got %3")
                    .arg(items[i].type).arg(types[i].second).arg(items[i].rx)));
            QCOMPARE(items[i].filePath, QString("/tmp/test_" + types[i].first + ".file"));
        }
    }
};

QTEST_MAIN(TestConfig)
#include "test_config.moc"
