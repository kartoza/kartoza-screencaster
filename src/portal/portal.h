/**
 * @file portal.h
 * @brief xdg-desktop-portal wrappers for Screenshot and ScreenCast.
 *
 * Used on Wayland compositors that do not implement the wlr-screencopy
 * protocol — most notably GNOME (Mutter) and KDE (KWin). On wlroots
 * compositors we prefer grim/wl-screenrec because they are simpler and
 * avoid the portal permission flow.
 *
 * The portal protocol is fundamentally asynchronous: every interactive
 * call (Screenshot, Start) returns a Request object and the result
 * arrives later as a Response signal — often many seconds later because
 * the user has to authorise the request in a dialog. We expose this
 * directly: callers fire-and-forget and connect to the result signals.
 * Trying to make the API synchronous (nested QEventLoop) freezes the UI
 * for the duration of the portal dialog and lets canvas-refresh ticks
 * re-enter mid-wait, which corrupts shared state.
 */
#pragma once

#ifdef HAS_DBUS

#include <QDBusArgument>
#include <QDBusMessage>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVariantMap>

/**
 * @struct StartResults
 * @brief Demarshalled view of org.freedesktop.portal.ScreenCast.Start's
 *        Response signal payload.
 *
 * We can't let Qt auto-convert the response's `a{sv}` into a QVariantMap
 * because the `streams` value (a(ua{sv})) carries inner `(ii)` structs
 * (position, size) that crash QtDBus's auto-walk with
 * "type struct 114 not a basic type" -> SIGABRT. Instead we register
 * this type with `qDBusRegisterMetaType` so QtDBus uses our `operator>>`
 * for the slot's second argument — and that operator walks the dict by
 * hand, reading each value as a `QDBusVariant` so inner complex types
 * stay opaque.
 */
struct StartResults {
    /** First PipeWire node id from the streams array (the screencast). */
    uint nodeId = 0;
    /** Restore token to save in QSettings so the next run skips the picker. */
    QString restoreToken;
};

Q_DECLARE_METATYPE(StartResults)

QDBusArgument &operator<<(QDBusArgument &arg, const StartResults &v);
const QDBusArgument &operator>>(const QDBusArgument &arg, StartResults &v);

/**
 * @class Portal
 * @brief Asynchronous wrappers around xdg-desktop-portal D-Bus calls.
 *
 * Singleton — the ScreenCast session and its persistent restore token
 * must be reused across the app lifetime. All methods are non-blocking;
 * results arrive via the signals on this object.
 */
class Portal : public QObject {
    Q_OBJECT
public:
    static Portal &instance();

    /** @brief True if the portal D-Bus service is reachable. */
    bool isAvailable();

    /**
     * @brief Request a screenshot via org.freedesktop.portal.Screenshot.
     *
     * Returns immediately. Emits screenshotReady() with the local path
     * on success, or screenshotFailed() on cancel/error. If a screenshot
     * is already in flight, the call is silently dropped (a canvas
     * refresh tick during a slow portal prompt would otherwise queue up).
     */
    void requestScreenshot();

    /**
     * @brief Open a ScreenCast session.
     *
     * Returns immediately. Walks the CreateSession → SelectSources →
     * Start → OpenPipeWireRemote handshake; emits screenCastReady()
     * with the PipeWire node id and an FD when complete, or
     * screenCastFailed() on error / user cancel. If a session is
     * already active the existing values are re-emitted immediately
     * via screenCastReady().
     *
     * The restore token is persisted to QSettings so subsequent runs
     * skip the source-picker dialog.
     */
    void requestScreenCast();

    /** @brief Close the active ScreenCast session, if any (fire-and-forget). */
    void stopScreenCast();

    /** @brief Last PipeWire node id from a successful screencast, or 0. */
    uint pipeWireNodeId() const { return m_pwNodeId; }

    /**
     * @brief Private PipeWire connection FD from OpenPipeWireRemote().
     * @return A valid file descriptor (>=0) or -1 if no session is open.
     *
     * Portal-issued stream nodes are only consumable through this private
     * connection; the default PipeWire socket sees the node but the
     * permission grant is bound to this FD. The FD is owned by Portal —
     * consumers must dup() it before passing to a child process.
     */
    int pipeWireFd() const { return m_pwFd; }

    /** @brief True if a screencast session is currently active. */
    bool hasActiveScreenCast() const { return m_pwFd >= 0 && m_pwNodeId != 0; }

signals:
    /** @brief Screenshot finished; argument is an absolute local PNG path. */
    void screenshotReady(const QString &path);
    /** @brief Screenshot failed or was cancelled by the user. */
    void screenshotFailed(const QString &reason);

    /** @brief ScreenCast session active; PipeWire node id and private fd. */
    void screenCastReady(uint nodeId, int fd);
    /** @brief ScreenCast handshake failed or was cancelled. */
    void screenCastFailed(const QString &reason);

private slots:
    // Slots use Qt's auto-conversion (u, a{sv}) -> (uint, QVariantMap).
    // The previous QDBusMessage-based manual walk was hitting a Qt 6.10
    // bug where reading the dict key as QString through a copied
    // QDBusArgument returned empty — the value bytes were read correctly
    // but the key bytes were lost. Qt's own QVariantMap operator>> does
    // not hit this issue (presumably because it operates on the
    // canonical internal arg, not a value<>-extracted copy). The earlier
    // SIGABRT in screencast Start was caused by our streams demarshal
    // walking into the (ii) structs inside the per-stream props dict,
    // which finalizeScreenCastFromStart() now sidesteps with QDBusVariant.
    void onScreenshotResponse(uint code, const QVariantMap &results);
    void onCreateSessionResponse(uint code, const QVariantMap &results);
    void onSelectSourcesResponse(uint code, const QVariantMap &results);

    // Slot takes the raw QDBusMessage and we call qdbus_cast<StartResults>
    // ourselves. Two reasons we don't use (uint, StartResults) directly:
    //   (1) Qt 6.10's signal->slot dispatcher does not reliably pick up
    //       custom-demarshaller types for a{sv}, so it falls back to the
    //       broken QVariantMap walk anyway.
    //   (2) Doing the cast explicitly lets us log + diagnose if the
    //       walk produces an empty nodeId.
    void onStartResponse(const QDBusMessage &msg);

private:
    Portal();
    QString senderToken() const;
    QString nextToken(const QString &prefix);

    void disconnectResponse(const QString &path, const char *slot);
    void abortScreenCast(const QString &reason);
    void finalizeScreenCastFromStart();

    // Screenshot in-flight state.
    bool m_screenshotInFlight = false;
    QString m_screenshotPath;        // current Request object path

    // ScreenCast handshake state.
    bool m_screenCastInFlight = false;
    QString m_screenCastReqPath;     // current Request object path
    QString m_screenCastSessionToken; // for SelectSources / Start path generation

    // Cached ScreenCast session state, valid once screenCastReady has fired.
    QString m_sessionHandle;
    uint m_pwNodeId = 0;
    int m_pwFd = -1;
};

#endif // HAS_DBUS
