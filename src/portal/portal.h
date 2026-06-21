/**
 * @file portal.h
 * @brief xdg-desktop-portal wrappers for Screenshot and ScreenCast.
 *
 * Used on Wayland compositors that do not implement the wlr-screencopy
 * protocol — most notably GNOME (Mutter) and KDE (KWin). On wlroots
 * compositors we prefer grim/wl-screenrec because they are simpler and
 * avoid the portal permission flow.
 */
#pragma once

#ifdef HAS_DBUS

#include <QObject>
#include <QString>
#include <QVariantMap>

/**
 * @class Portal
 * @brief Synchronous wrappers around xdg-desktop-portal D-Bus calls.
 *
 * The portal is asynchronous (each call returns a Request object whose
 * Response signal carries the result). We hide that behind blocking
 * methods backed by a local QEventLoop.
 *
 * Singleton because the ScreenCast session and persistent restore token
 * must be reused across the app lifetime.
 */
class Portal : public QObject {
    Q_OBJECT
public:
    static Portal &instance();

    /** @brief True if the portal D-Bus service is reachable. */
    bool isAvailable();

    /**
     * @brief Take a screenshot via org.freedesktop.portal.Screenshot.
     * @return Absolute path to a PNG, or empty string on failure/cancellation.
     *
     * Blocks until the portal responds (timeout 30s). On GNOME the user
     * is prompted to authorise screenshot access on the first call only;
     * subsequent calls are silent for the session.
     */
    QString screenshot();

    /**
     * @brief Open a ScreenCast session and return the PipeWire node id.
     * @return PipeWire node id (>0) on success, 0 on failure.
     *
     * The session and a restore token are cached so the user is only
     * shown the source-picker dialog on first run. The restore token
     * is persisted to QSettings so it survives restarts.
     */
    uint startScreenCast();

    /** @brief Close the active ScreenCast session, if any. */
    void stopScreenCast();

    /** @brief Last PipeWire node id from startScreenCast(), or 0. */
    uint pipeWireNodeId() const { return m_pwNodeId; }

    /**
     * @brief Private PipeWire connection FD from OpenPipeWireRemote().
     * @return A valid file descriptor (>=0) or -1 if no session is open.
     *
     * Portal-issued stream nodes are only consumable through this private
     * connection; the default PipeWire socket sees the node but the
     * permission grant is bound to this FD. The FD is owned by Portal —
     * consumers should `dup()` it before passing to a child process.
     */
    int pipeWireFd() const { return m_pwFd; }

private slots:
    void onResponse(uint response, const QVariantMap &results);

private:
    Portal();
    QString senderToken() const;
    QString nextToken(const QString &prefix);

    // Pending-call state. Only one call may be in flight at a time.
    QString m_pendingPath;
    bool m_responseReceived = false;
    uint m_responseCode = 0;
    QVariantMap m_responseResults;

    // Cached ScreenCast session state.
    QString m_sessionHandle;
    uint m_pwNodeId = 0;
    int m_pwFd = -1;
};

#endif // HAS_DBUS
