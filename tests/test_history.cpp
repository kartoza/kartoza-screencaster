/**
 * @file test_history.cpp
 * @brief Tests for history page video discovery and playback readiness.
 */
#include <QTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include "gui/historypage.h"

class TestHistory : public QObject {
    Q_OBJECT

    // Helper: create a dummy mp4 file (just needs to exist, not be valid video)
    void createDummyFile(const QString &path) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("fake video data");
        f.close();
    }

    // Helper: write a recording.json into a folder
    void writeRecordingJson(const QString &folder,
                            const QString &mergedFile = {},
                            const QString &verticalFile = {},
                            const QString &screenFile = {},
                            const QString &title = "Test Recording") {
        QJsonObject root;
        root["status"] = "completed";

        QJsonObject meta;
        meta["title"] = title;
        root["metadata"] = meta;

        QJsonObject files;
        if (!mergedFile.isEmpty()) files["merged_file"] = mergedFile;
        if (!verticalFile.isEmpty()) files["vertical_file"] = verticalFile;
        if (!screenFile.isEmpty()) files["video_file"] = screenFile;
        root["files"] = files;

        QFile f(folder + "/recording.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(root).toJson());
        f.close();
    }

private slots:

    // =========================================================================
    // 1. findBestVideo with explicit paths (8 tests)
    // =========================================================================

    void testFindBestVideoPrefersMerged() {
        QTemporaryDir dir;
        QString merged = dir.path() + "/001-test.mp4";
        QString screen = dir.path() + "/screen_part000.mp4";
        createDummyFile(merged);
        createDummyFile(screen);

        RecordingEntry rec;
        rec.mergedFile = merged;
        rec.screenFile = screen;

        QCOMPARE(HistoryPage::findBestVideo(rec), merged);
    }

    void testFindBestVideoFallsBackToVertical() {
        QTemporaryDir dir;
        QString vertical = dir.path() + "/001-test-vertical.mp4";
        QString screen = dir.path() + "/screen_part000.mp4";
        createDummyFile(vertical);
        createDummyFile(screen);

        RecordingEntry rec;
        rec.verticalFile = vertical;
        rec.screenFile = screen;

        QCOMPARE(HistoryPage::findBestVideo(rec), vertical);
    }

    void testFindBestVideoFallsBackToScreen() {
        QTemporaryDir dir;
        QString screen = dir.path() + "/screen_part000.mp4";
        createDummyFile(screen);

        RecordingEntry rec;
        rec.screenFile = screen;

        QCOMPARE(HistoryPage::findBestVideo(rec), screen);
    }

    void testFindBestVideoReturnsEmptyWhenNoFiles() {
        RecordingEntry rec;
        QVERIFY(HistoryPage::findBestVideo(rec).isEmpty());
    }

    void testFindBestVideoSkipsMissingMerged() {
        QTemporaryDir dir;
        QString screen = dir.path() + "/screen_part000.mp4";
        createDummyFile(screen);

        RecordingEntry rec;
        rec.mergedFile = dir.path() + "/nonexistent.mp4"; // doesn't exist
        rec.screenFile = screen;

        QCOMPARE(HistoryPage::findBestVideo(rec), screen);
    }

    void testFindBestVideoSkipsMissingVertical() {
        QTemporaryDir dir;
        QString screen = dir.path() + "/screen_part000.mp4";
        createDummyFile(screen);

        RecordingEntry rec;
        rec.verticalFile = dir.path() + "/nonexistent-vertical.mp4";
        rec.screenFile = screen;

        QCOMPARE(HistoryPage::findBestVideo(rec), screen);
    }

    void testFindBestVideoPriorityOrder() {
        // All three exist: merged wins
        QTemporaryDir dir;
        QString merged = dir.path() + "/001-test.mp4";
        QString vertical = dir.path() + "/001-test-vertical.mp4";
        QString screen = dir.path() + "/screen_part000.mp4";
        createDummyFile(merged);
        createDummyFile(vertical);
        createDummyFile(screen);

        RecordingEntry rec;
        rec.mergedFile = merged;
        rec.verticalFile = vertical;
        rec.screenFile = screen;

        QCOMPARE(HistoryPage::findBestVideo(rec), merged);
    }

    void testFindBestVideoVerticalOverScreen() {
        // Vertical and screen exist, no merged: vertical wins
        QTemporaryDir dir;
        QString vertical = dir.path() + "/001-test-vertical.mp4";
        QString screen = dir.path() + "/screen_part000.mp4";
        createDummyFile(vertical);
        createDummyFile(screen);

        RecordingEntry rec;
        rec.verticalFile = vertical;
        rec.screenFile = screen;

        QCOMPARE(HistoryPage::findBestVideo(rec), vertical);
    }

    // =========================================================================
    // 2. RecordingEntry struct defaults (3 tests)
    // =========================================================================

    void testRecordingEntryDefaults() {
        RecordingEntry rec;
        QVERIFY(rec.folder.isEmpty());
        QVERIFY(rec.mergedFile.isEmpty());
        QVERIFY(rec.verticalFile.isEmpty());
        QVERIFY(rec.screenFile.isEmpty());
        QCOMPARE(rec.duration, 0);
        QCOMPARE(rec.totalSize, 0);
    }

    void testRecordingEntryFieldsSet() {
        RecordingEntry rec;
        rec.folder = "/tmp/test";
        rec.mergedFile = "/tmp/test/merged.mp4";
        rec.verticalFile = "/tmp/test/vertical.mp4";
        rec.screenFile = "/tmp/test/screen.mp4";
        rec.title = "My Recording";
        rec.duration = 5000000000; // 5 seconds in ns

        QCOMPARE(rec.title, "My Recording");
        QCOMPARE(rec.duration, 5000000000);
    }

    void testRecordingEntryEmptyPathsAreNotFound() {
        RecordingEntry rec;
        // Empty paths should cause findBestVideo to return empty
        QVERIFY(HistoryPage::findBestVideo(rec).isEmpty());
    }

    // =========================================================================
    // 3. Simulated recording directory scenarios (6 tests)
    // =========================================================================

    void testLandscapeRecordingWithMergedFile() {
        // Simulates a completed landscape recording
        QTemporaryDir dir;
        QString folder = dir.path() + "/001-2024-01-01_12-00-00";
        QDir().mkpath(folder);

        QString merged = folder + "/001-test.mp4";
        QString screen = folder + "/screen_part000.mp4";
        createDummyFile(merged);
        createDummyFile(screen);
        writeRecordingJson(folder, merged, {}, screen);

        RecordingEntry rec;
        rec.folder = folder;
        rec.mergedFile = merged;
        rec.screenFile = screen;

        QString video = HistoryPage::findBestVideo(rec);
        QVERIFY2(!video.isEmpty(), "Landscape recording should have a playable video");
        QCOMPARE(video, merged);
    }

    void testVerticalRecordingWithVerticalFile() {
        // Simulates a completed vertical recording
        QTemporaryDir dir;
        QString folder = dir.path() + "/002-2024-01-01_12-00-00";
        QDir().mkpath(folder);

        QString vertical = folder + "/002-test-vertical.mp4";
        QString screen = folder + "/screen_part000.mp4";
        createDummyFile(vertical);
        createDummyFile(screen);
        writeRecordingJson(folder, {}, vertical, screen);

        RecordingEntry rec;
        rec.folder = folder;
        rec.verticalFile = vertical;
        rec.screenFile = screen;

        QString video = HistoryPage::findBestVideo(rec);
        QVERIFY2(!video.isEmpty(), "Vertical recording should have a playable video");
        QCOMPARE(video, vertical);
    }

    void testRecordingWithOnlyRawScreen() {
        // Recording where processing hasn't completed yet
        QTemporaryDir dir;
        QString folder = dir.path() + "/003-2024-01-01_12-00-00";
        QDir().mkpath(folder);

        QString screen = folder + "/screen_part000.mp4";
        createDummyFile(screen);

        RecordingEntry rec;
        rec.folder = folder;
        rec.screenFile = screen;

        QString video = HistoryPage::findBestVideo(rec);
        QVERIFY2(!video.isEmpty(), "Recording with only raw screen should still be playable");
        QCOMPARE(video, screen);
    }

    void testRecordingWithDeletedMergedFile() {
        // Merged file path in JSON but file was deleted from disk
        QTemporaryDir dir;
        QString folder = dir.path() + "/004-2024-01-01_12-00-00";
        QDir().mkpath(folder);

        QString screen = folder + "/screen_part000.mp4";
        createDummyFile(screen);

        RecordingEntry rec;
        rec.folder = folder;
        rec.mergedFile = folder + "/deleted.mp4"; // doesn't exist
        rec.screenFile = screen;

        QString video = HistoryPage::findBestVideo(rec);
        QVERIFY2(!video.isEmpty(), "Should fall back to screen when merged is deleted");
        QCOMPARE(video, screen);
    }

    void testEmptyRecordingFolder() {
        QTemporaryDir dir;
        QString folder = dir.path() + "/005-2024-01-01_12-00-00";
        QDir().mkpath(folder);

        RecordingEntry rec;
        rec.folder = folder;

        QVERIFY(HistoryPage::findBestVideo(rec).isEmpty());
    }

    void testVerticalFileFromJson() {
        // Verify that vertical_file from recording.json is properly used
        QTemporaryDir dir;
        QString folder = dir.path() + "/006-2024-01-01_12-00-00";
        QDir().mkpath(folder);

        QString vertical = folder + "/006-test-vertical.mp4";
        createDummyFile(vertical);
        writeRecordingJson(folder, {}, vertical);

        // Simulate what loadRecordings does: read JSON
        QFile jsonFile(folder + "/recording.json");
        QVERIFY(jsonFile.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(jsonFile.readAll()).object();
        jsonFile.close();

        RecordingEntry rec;
        rec.folder = folder;
        auto files = root["files"].toObject();
        rec.mergedFile = files["merged_file"].toString();
        rec.verticalFile = files["vertical_file"].toString();
        rec.screenFile = files["video_file"].toString();

        QCOMPARE(rec.verticalFile, vertical);
        QString video = HistoryPage::findBestVideo(rec);
        QVERIFY2(!video.isEmpty(), "Vertical file from JSON should be found");
        QCOMPARE(video, vertical);
    }
};

QTEST_MAIN(TestHistory)
#include "test_history.moc"
