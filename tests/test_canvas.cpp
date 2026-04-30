#include <QTest>
#include <QSignalSpy>
#include <QApplication>
#include "gui/canvas.h"

class TestCanvas : public QObject {
  Q_OBJECT

private slots:
  // --- REGRESSION: setSelectedItem must emit selectionChanged ---
  void testSetSelectedItemEmitsSignal() {
    Canvas c;
    // Add a webcam item so there's something to select
    c.addWebcam("video0", "Test", 0);
    QCOMPARE(c.itemCount(), 1);

    QSignalSpy spy(&c, &Canvas::selectionChanged);
    c.setSelectedItem(0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toInt(), 0);
    QCOMPARE(c.selectedItem(), 0);
  }

  void testSetSelectedItemNegativeEmitsSignal() {
    Canvas c;
    c.addWebcam("video0", "Test", 0);
    c.setSelectedItem(0);

    QSignalSpy spy(&c, &Canvas::selectionChanged);
    c.setSelectedItem(-1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toInt(), -1);
  }

  // --- itemsChanged fires on addWebcam ---
  void testAddWebcamEmitsItemsChanged() {
    Canvas c;
    QSignalSpy spy(&c, &Canvas::itemsChanged);
    c.addWebcam("video0", "Test", 0);
    QVERIFY(spy.count() >= 1);
    QCOMPARE(c.itemCount(), 1);
  }

  // --- GIF loop setting ---
  void testSetItemGifLoop() {
    Canvas c;
    // Add a webcam item and test gifLoop on it (any item type works)
    c.addWebcam("video0", "Test", 0);
    int idx = c.itemCount() - 1;

    QSignalSpy spy(&c, &Canvas::itemsChanged);
    c.setItemGifLoop(idx, 3, 5);

    auto items = c.exportItems();
    QCOMPARE(items[idx].gifLoop, 3);
    QCOMPARE(items[idx].gifLoopMax, 5);
    QVERIFY(spy.count() >= 1);
  }

  // --- frameRect returns valid rect ---
  void testFrameRectLandscape() {
    Canvas c;
    c.setMode(0); // landscape
    QRect fr = c.frameRect();
    // In landscape, frame = full canvas (default 560x315)
    QCOMPARE(fr.x(), 0);
    QCOMPARE(fr.y(), 0);
    QVERIFY(fr.width() > 0);
    QVERIFY(fr.height() > 0);
  }

  void testFrameRectVertical() {
    Canvas c;
    c.setMode(1); // vertical 9:16
    QRect fr = c.frameRect();
    // Frame should be 9:16 aspect, centered
    QVERIFY(fr.width() > 0);
    QVERIFY(fr.height() > 0);
    double aspect = double(fr.width()) / fr.height();
    QVERIFY(aspect > 0.5 && aspect < 0.6); // 9/16 = 0.5625
  }

  // --- removeItem reduces count ---
  void testRemoveItem() {
    Canvas c;
    c.addWebcam("video0", "Test", 0);
    QCOMPARE(c.itemCount(), 1);
    c.removeItem(0);
    QCOMPARE(c.itemCount(), 0);
  }

  // --- clearAll resets everything ---
  void testClearAll() {
    Canvas c;
    c.addWebcam("video0", "Test1", 0);
    c.addWebcam("video1", "Test2", 1);
    QCOMPARE(c.itemCount(), 2);
    c.clearAll();
    QCOMPARE(c.itemCount(), 0);
    QCOMPARE(c.selectedItem(), -1);
  }
};

QTEST_MAIN(TestCanvas)
#include "test_canvas.moc"
