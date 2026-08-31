/**
 * @file test_platform.cpp
 * @brief Compositor detection and capture-backend classification.
 *
 * The classification in Platform::supportsWlrCapture() decides which screen
 * capture backend the recorder uses. Getting it wrong is silent and
 * expensive: COSMIC was previously treated as wlroots, so both wl-screenrec
 * and the wf-recorder fallback bailed on the missing
 * wlr-screencopy-unstable-v1 protocol while audio capture carried on,
 * yielding audio-only recordings that still reported success.
 */
#include <QTest>
#include "platform/platform.h"

class TestPlatform : public QObject {
    Q_OBJECT

    /** Set both desktop env vars Platform::compositor() consults. */
    static void setDesktop(const QByteArray &value) {
        qputenv("XDG_CURRENT_DESKTOP", value);
        qputenv("XDG_SESSION_DESKTOP", value);
    }

private slots:
    void cleanup() {
        qunsetenv("XDG_CURRENT_DESKTOP");
        qunsetenv("XDG_SESSION_DESKTOP");
    }

    void testCompositorDetection_data() {
        QTest::addColumn<QByteArray>("desktop");
        QTest::addColumn<int>("expected");

        QTest::newRow("hyprland") << QByteArray("Hyprland")  << int(Platform::Compositor::Wlroots);
        QTest::newRow("sway")     << QByteArray("sway")      << int(Platform::Compositor::Wlroots);
        QTest::newRow("river")    << QByteArray("river")     << int(Platform::Compositor::Wlroots);
        QTest::newRow("niri")     << QByteArray("niri")      << int(Platform::Compositor::Wlroots);
        QTest::newRow("cosmic")   << QByteArray("COSMIC")    << int(Platform::Compositor::Cosmic);
        QTest::newRow("gnome")    << QByteArray("ubuntu:GNOME") << int(Platform::Compositor::Mutter);
        QTest::newRow("kde")      << QByteArray("KDE")       << int(Platform::Compositor::KWin);
        QTest::newRow("plasma")   << QByteArray("plasma")    << int(Platform::Compositor::KWin);
        QTest::newRow("unknown")  << QByteArray("something") << int(Platform::Compositor::Unknown);
    }

    void testCompositorDetection() {
        QFETCH(QByteArray, desktop);
        QFETCH(int, expected);
        setDesktop(desktop);
        QCOMPARE(int(Platform::compositor()), expected);
    }

    void testWlrCaptureSupport_data() {
        QTest::addColumn<QByteArray>("desktop");
        QTest::addColumn<bool>("supported");

        // wlroots family: the native grim / wl-screenrec path.
        QTest::newRow("hyprland") << QByteArray("Hyprland") << true;
        QTest::newRow("sway")     << QByteArray("sway")     << true;

        // Everything else must route to xdg-desktop-portal.
        QTest::newRow("cosmic")   << QByteArray("COSMIC")   << false;
        QTest::newRow("gnome")    << QByteArray("GNOME")    << false;
        QTest::newRow("kde")      << QByteArray("KDE")      << false;
        QTest::newRow("unknown")  << QByteArray("weird-wm") << false;
    }

    void testWlrCaptureSupport() {
        QFETCH(QByteArray, desktop);
        QFETCH(bool, supported);
        setDesktop(desktop);
        QCOMPARE(Platform::supportsWlrCapture(), supported);
    }

    /**
     * Regression guard for the audio-only-recording bug. COSMIC must never be
     * classified as wlr-capable: it has no wlr-screencopy implementation, so
     * both capture backends fail and no video is ever written.
     */
    void testCosmicUsesPortalNotWlr() {
        setDesktop("COSMIC");
        QCOMPARE(Platform::compositor(), Platform::Compositor::Cosmic);
        QVERIFY2(!Platform::supportsWlrCapture(),
                 "COSMIC must use the xdg-desktop-portal capture path — it does "
                 "not implement wlr-screencopy-unstable-v1");
    }
};

QTEST_MAIN(TestPlatform)
#include "test_platform.moc"
