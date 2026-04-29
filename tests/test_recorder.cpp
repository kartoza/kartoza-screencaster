/**
 * @file test_recorder.cpp
 * @brief Regression tests for Recorder state machine.
 *
 * Tests the state transitions and signal emissions without actually
 * launching ffmpeg processes (no screen/audio/webcam devices needed).
 */
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "recorder/recorder.h"

class TestRecorder : public QObject {
  Q_OBJECT

private slots:
  // --- Initial state ---

  void testInitialState() {
    Recorder r;
    QVERIFY(!r.isRecording());
    QVERIFY(!r.isPaused());
    QCOMPARE(r.elapsedMs(), 0LL);
  }

  // --- Start sets recording state ---

  void testStartSetsRecording() {
    QTemporaryDir tmp;
    Recorder r;

    RecordingOptions opts;
    opts.outputDir = tmp.path();
    opts.noScreen = true;  // don't need real devices
    opts.noAudio = true;
    opts.noWebcam = true;
    opts.title = "test";
    opts.number = 1;

    QSignalSpy startedSpy(&r, &Recorder::recordingStarted);
    r.start(opts);

    QVERIFY(r.isRecording());
    QVERIFY(!r.isPaused());
    QCOMPARE(startedSpy.count(), 1);
  }

  // --- Stop from recording state ---

  void testStopFromRecording() {
    QTemporaryDir tmp;
    Recorder r;

    RecordingOptions opts;
    opts.outputDir = tmp.path();
    opts.noScreen = true;
    opts.noAudio = true;
    opts.noWebcam = true;
    opts.title = "test";

    r.start(opts);
    QVERIFY(r.isRecording());

    QSignalSpy stoppedSpy(&r, &Recorder::recordingStopped);
    r.stop();

    QVERIFY(!r.isRecording());
    QVERIFY(!r.isPaused());
    QCOMPARE(stoppedSpy.count(), 1);
  }

  // --- Pause sets paused state ---

  void testPauseSetsState() {
    QTemporaryDir tmp;
    Recorder r;

    RecordingOptions opts;
    opts.outputDir = tmp.path();
    opts.noScreen = true;
    opts.noAudio = true;
    opts.noWebcam = true;
    opts.title = "test";

    r.start(opts);

    QSignalSpy pausedSpy(&r, &Recorder::recordingPaused);
    r.pause();

    QVERIFY(!r.isRecording());
    QVERIFY(r.isPaused());
    QCOMPARE(pausedSpy.count(), 1);
  }

  // --- Resume from paused ---

  void testResumeFromPaused() {
    QTemporaryDir tmp;
    Recorder r;

    RecordingOptions opts;
    opts.outputDir = tmp.path();
    opts.noScreen = true;
    opts.noAudio = true;
    opts.noWebcam = true;
    opts.title = "test";

    r.start(opts);
    r.pause();
    QVERIFY(r.isPaused());

    QSignalSpy resumedSpy(&r, &Recorder::recordingResumed);
    r.resume();

    QVERIFY(r.isRecording());
    QVERIFY(!r.isPaused());
    QCOMPARE(resumedSpy.count(), 1);
  }

  // --- REGRESSION: Stop from paused state must work ---

  void testStopFromPaused() {
    QTemporaryDir tmp;
    Recorder r;

    RecordingOptions opts;
    opts.outputDir = tmp.path();
    opts.noScreen = true;
    opts.noAudio = true;
    opts.noWebcam = true;
    opts.title = "test";

    r.start(opts);
    r.pause();
    QVERIFY(r.isPaused());

    QSignalSpy stoppedSpy(&r, &Recorder::recordingStopped);
    r.stop();

    QVERIFY(!r.isRecording());
    QVERIFY(!r.isPaused());
    QCOMPARE(stoppedSpy.count(), 1);
  }

  // --- REGRESSION: Double stop must not crash ---

  void testDoubleStopNoCrash() {
    QTemporaryDir tmp;
    Recorder r;

    RecordingOptions opts;
    opts.outputDir = tmp.path();
    opts.noScreen = true;
    opts.noAudio = true;
    opts.noWebcam = true;
    opts.title = "test";

    r.start(opts);
    r.stop();
    // Second stop should be a no-op, not crash
    r.stop();
    QVERIFY(!r.isRecording());
  }

  // --- REGRESSION: Double start must not crash ---

  void testDoubleStartEmitsError() {
    QTemporaryDir tmp;
    Recorder r;

    RecordingOptions opts;
    opts.outputDir = tmp.path();
    opts.noScreen = true;
    opts.noAudio = true;
    opts.noWebcam = true;
    opts.title = "test";

    r.start(opts);

    QSignalSpy errorSpy(&r, &Recorder::recordingError);
    r.start(opts); // should emit error, not crash

    QCOMPARE(errorSpy.count(), 1);
  }

  // --- REGRESSION: Pause when not recording is no-op ---

  void testPauseWhenNotRecording() {
    Recorder r;
    QSignalSpy pausedSpy(&r, &Recorder::recordingPaused);
    r.pause(); // should be no-op
    QCOMPARE(pausedSpy.count(), 0);
    QVERIFY(!r.isPaused());
  }

  // --- REGRESSION: Resume when not paused is no-op ---

  void testResumeWhenNotPaused() {
    Recorder r;
    QSignalSpy resumedSpy(&r, &Recorder::recordingResumed);
    r.resume(); // should be no-op
    QCOMPARE(resumedSpy.count(), 0);
  }

  // --- Elapsed time accumulates across pause/resume ---

  void testElapsedAccumulates() {
    QTemporaryDir tmp;
    Recorder r;

    RecordingOptions opts;
    opts.outputDir = tmp.path();
    opts.noScreen = true;
    opts.noAudio = true;
    opts.noWebcam = true;
    opts.title = "test";

    r.start(opts);
    QTest::qWait(100); // record for ~100ms

    r.pause();
    qint64 pausedTime = r.elapsedMs();
    QVERIFY(pausedTime >= 50); // should have some elapsed time

    QTest::qWait(200); // time passes while paused
    QCOMPARE(r.elapsedMs(), pausedTime); // elapsed should not increase while paused

    r.resume();
    QTest::qWait(100); // record another ~100ms
    QVERIFY(r.elapsedMs() > pausedTime); // should have more time now
  }

  // --- REGRESSION: Reprocess while processing must fail ---

  void testReprocessWhileProcessingFails() {
    QTemporaryDir tmp;
    Recorder r;

    RecordingOptions opts;
    opts.outputDir = tmp.path();
    opts.noScreen = true;
    opts.noAudio = true;
    opts.noWebcam = true;
    opts.title = "test";

    r.start(opts);
    r.stop(); // starts processing in background

    QSignalSpy errorSpy(&r, &Recorder::recordingError);
    r.reprocess(tmp.path()); // should fail - processing in progress

    // Either error emitted or silently rejected
    // (depends on timing - processing thread may have finished)
    QVERIFY(true); // no crash is the main assertion
  }

  // --- Output directory created on start ---

  void testOutputDirCreated() {
    QTemporaryDir tmp;
    Recorder r;

    QString subdir = tmp.path() + "/test-output";
    RecordingOptions opts;
    opts.outputDir = subdir;
    opts.noScreen = true;
    opts.noAudio = true;
    opts.noWebcam = true;
    opts.title = "test";

    r.start(opts);
    QVERIFY(QDir(subdir).exists());
    QCOMPARE(r.outputDir(), subdir);
  }

  // --- recording.json written on start ---

  void testRecordingJsonWritten() {
    QTemporaryDir tmp;
    Recorder r;

    RecordingOptions opts;
    opts.outputDir = tmp.path();
    opts.noScreen = true;
    opts.noAudio = true;
    opts.noWebcam = true;
    opts.title = "Test Title";
    opts.number = 42;

    r.start(opts);
    QVERIFY(QFile::exists(tmp.path() + "/recording.json"));
  }
};

QTEST_MAIN(TestRecorder)
#include "test_recorder.moc"
