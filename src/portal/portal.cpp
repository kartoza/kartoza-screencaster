/**
 * @file portal.cpp
 * @brief Implementation of asynchronous xdg-desktop-portal wrappers.
 *
 * The portal flow is:
 *
 *   1. Application calls a method like Screenshot or Start. It passes a
 *      `handle_token` in the options dict and receives a Request object
 *      path back.
 *   2. The Request path is predictable as
 *      /org/freedesktop/portal/desktop/request/<sender>/<token>
 *      so we subscribe to its `Response(u, a{sv})` signal BEFORE issuing
 *      the call (otherwise we could miss a fast response).
 *   3. The portal eventually emits Response — which can take seconds or
 *      minutes if the user is at lunch when the dialog appears.
 *
 * The synchronous approach (nested QEventLoop) doesn't work for two
 * reasons: it freezes the UI, and the inner loop dispatches queued
 * events such as the canvas's periodic screenshot request, which then
 * shares this Portal's state with whatever call started the outer wait.
 * Async wiring removes both problems.
 */
#include "portal/portal.h"

#ifdef HAS_DBUS

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QDebug>
#include <QRandomGenerator>
#include <QSettings>
#include <QUrl>
#include <fcntl.h>
#include <unistd.h>

namespace {
constexpr auto kPortalBus = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kRequestIface = "org.freedesktop.portal.Request";
constexpr auto kScreenshotIface = "org.freedesktop.portal.Screenshot";
constexpr auto kScreenCastIface = "org.freedesktop.portal.ScreenCast";
}

Portal &Portal::instance() {
    static Portal p;
    return p;
}

Portal::Portal() = default;

bool Portal::isAvailable() {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        kPortalBus, kPortalPath, "org.freedesktop.DBus.Peer", "Ping");
    QDBusMessage reply = bus.call(msg, QDBus::Block, 2000);
    return reply.type() == QDBusMessage::ReplyMessage;
}

QString Portal::senderToken() const {
    // QDBusConnection unique name looks like ":1.42"; the portal Request
    // path uses it without the leading ":" and with "." replaced by "_".
    QString s = QDBusConnection::sessionBus().baseService();
    if (s.startsWith(':')) s.remove(0, 1);
    s.replace('.', '_');
    return s;
}

QString Portal::nextToken(const QString &prefix) {
    return prefix + QString::number(QRandomGenerator::global()->generate());
}

void Portal::disconnectResponse(const QString &path, const char *slot) {
    if (path.isEmpty()) return;
    QDBusConnection::sessionBus().disconnect(
        kPortalBus, path, kRequestIface, "Response", this, slot);
}

// Unwrap a value that may have come back wrapped in QDBusVariant.
// QtDBus's `a{sv}` -> QVariantMap auto-conversion stores each value as
// `QDBusVariant` rather than unwrapping to the inner QVariant — calling
// .toString() on the outer variant returns empty in that case, which is
// why "uri" looked missing after the slot-signature change.
static QVariant unwrapDBusVariant(const QVariant &v) {
    if (v.userType() == qMetaTypeId<QDBusVariant>()) {
        return v.value<QDBusVariant>().variant();
    }
    return v;
}

bool Portal::decodeResponse(const QDBusMessage &msg,
                            uint &code, QVariantMap &results) {
    const QList<QVariant> args = msg.arguments();
    if (args.size() < 2) return false;
    code = args.at(0).toUInt();

    const QVariant raw = args.at(1);
    qDebug() << "Portal::decodeResponse: code=" << code
             << "args[1] typeName=" << raw.typeName()
             << "userType=" << raw.userType();

    if (raw.userType() == QMetaType::QVariantMap) {
        // QtDBus auto-converted the a{sv} but stored each value as a
        // QDBusVariant; unwrap them so downstream `.toString()` works.
        const QVariantMap raw_map = raw.toMap();
        for (auto it = raw_map.cbegin(); it != raw_map.cend(); ++it) {
            results.insert(it.key(), unwrapDBusVariant(it.value()));
        }
        qDebug() << "Portal::decodeResponse: QVariantMap path, keys=" << results.keys();
        return true;
    }

    // Manual walk — QtDBus left the dict opaque (typical for slots that
    // take QDBusMessage). Reading each value as QDBusVariant lets us
    // capture both basic and complex inner types without recursing into
    // them (which is what trips the "type struct 114" abort).
    QDBusArgument arg = raw.value<QDBusArgument>();
    qDebug() << "Portal::decodeResponse: QDBusArgument path, signature="
             << arg.currentSignature();
    arg.beginMap();
    int n = 0;
    while (!arg.atEnd()) {
        arg.beginMapEntry();
        QString key;
        QDBusVariant value;
        arg >> key >> value;
        arg.endMapEntry();
        QVariant inner = value.variant();
        qDebug() << "  entry" << n++ << "key=" << key
                 << "valueType=" << inner.typeName();
        results.insert(key, inner);
    }
    arg.endMap();
    return true;
}

// ---------------------------------------------------------------------------
// Screenshot
// ---------------------------------------------------------------------------

void Portal::requestScreenshot() {
    if (m_screenshotInFlight) {
        // Canvas refresh tick raced with one already pending — drop it.
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit screenshotFailed("session bus not connected");
        return;
    }

    QString token = nextToken("kscreen_shot_");
    m_screenshotPath = QString("/org/freedesktop/portal/desktop/request/%1/%2")
                            .arg(senderToken(), token);

    if (!bus.connect(kPortalBus, m_screenshotPath, kRequestIface, "Response",
                     this, SLOT(onScreenshotResponse(QDBusMessage)))) {
        emit screenshotFailed("failed to subscribe to Response signal");
        m_screenshotPath.clear();
        return;
    }

    QVariantMap options;
    options["handle_token"] = token;
    options["modal"] = false;
    options["interactive"] = false;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        kPortalBus, kPortalPath, kScreenshotIface, "Screenshot");
    msg << QString("") << options;

    QDBusMessage reply = bus.call(msg, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        QString err = reply.errorMessage();
        disconnectResponse(m_screenshotPath,
                           SLOT(onScreenshotResponse(QDBusMessage)));
        m_screenshotPath.clear();
        emit screenshotFailed("method call failed: " + err);
        return;
    }
    m_screenshotInFlight = true;
    // Now we wait for onScreenshotResponse to fire (could be minutes if the
    // user takes their time at the permission dialog).
}

void Portal::onScreenshotResponse(const QDBusMessage &msg) {
    disconnectResponse(m_screenshotPath,
                       SLOT(onScreenshotResponse(QDBusMessage)));
    m_screenshotPath.clear();
    m_screenshotInFlight = false;

    uint code = 0;
    QVariantMap results;
    if (!decodeResponse(msg, code, results)) {
        emit screenshotFailed("malformed Response message");
        return;
    }

    if (code != 0) {
        emit screenshotFailed(code == 1 ? "user cancelled"
                                        : QString("portal error %1").arg(code));
        return;
    }
    QString uri = results.value("uri").toString();
    if (uri.isEmpty()) {
        emit screenshotFailed("portal returned no URI");
        return;
    }
    QString localPath = QUrl(uri).toLocalFile();
    if (localPath.isEmpty()) {
        emit screenshotFailed("URI was not local: " + uri);
        return;
    }
    emit screenshotReady(localPath);
}

// ---------------------------------------------------------------------------
// ScreenCast — three-step async state machine
// ---------------------------------------------------------------------------

void Portal::requestScreenCast() {
    if (hasActiveScreenCast()) {
        // Already authorised and live — surface the cached values so
        // callers don't need a separate "is it ready" check.
        emit screenCastReady(m_pwNodeId, m_pwFd);
        return;
    }
    if (m_screenCastInFlight) {
        // Already negotiating — caller will receive the existing signal
        // when it eventually fires.
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit screenCastFailed("session bus not connected");
        return;
    }

    QString reqToken = nextToken("kscreen_cs_");
    m_screenCastSessionToken = nextToken("kscreen_ss_");
    m_screenCastReqPath = QString("/org/freedesktop/portal/desktop/request/%1/%2")
                              .arg(senderToken(), reqToken);

    if (!bus.connect(kPortalBus, m_screenCastReqPath, kRequestIface,
                     "Response", this,
                     SLOT(onCreateSessionResponse(QDBusMessage)))) {
        emit screenCastFailed("failed to subscribe to CreateSession Response");
        m_screenCastReqPath.clear();
        return;
    }

    QVariantMap opts;
    opts["handle_token"] = reqToken;
    opts["session_handle_token"] = m_screenCastSessionToken;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        kPortalBus, kPortalPath, kScreenCastIface, "CreateSession");
    msg.setArguments({opts});

    QDBusMessage reply = bus.call(msg, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        QString err = reply.errorMessage();
        disconnectResponse(m_screenCastReqPath,
                           SLOT(onCreateSessionResponse(QDBusMessage)));
        m_screenCastReqPath.clear();
        emit screenCastFailed("CreateSession call failed: " + err);
        return;
    }
    m_screenCastInFlight = true;
}

void Portal::onCreateSessionResponse(const QDBusMessage &msg) {
    disconnectResponse(m_screenCastReqPath,
                       SLOT(onCreateSessionResponse(QDBusMessage)));
    m_screenCastReqPath.clear();

    uint code = 0;
    QVariantMap results;
    if (!decodeResponse(msg, code, results)) {
        abortScreenCast("malformed CreateSession Response");
        return;
    }

    if (code != 0) {
        abortScreenCast(code == 1 ? "user cancelled CreateSession"
                                  : QString("portal error %1").arg(code));
        return;
    }
    QString session = results.value("session_handle").toString();
    if (session.isEmpty()) {
        abortScreenCast("CreateSession returned no session_handle");
        return;
    }
    m_sessionHandle = session;

    // --- 2. SelectSources ---
    auto bus = QDBusConnection::sessionBus();
    QString reqToken = nextToken("kscreen_cs_");
    m_screenCastReqPath = QString("/org/freedesktop/portal/desktop/request/%1/%2")
                              .arg(senderToken(), reqToken);

    if (!bus.connect(kPortalBus, m_screenCastReqPath, kRequestIface,
                     "Response", this,
                     SLOT(onSelectSourcesResponse(QDBusMessage)))) {
        abortScreenCast("failed to subscribe to SelectSources Response");
        return;
    }

    QSettings settings;
    QString restoreToken =
        settings.value("portal/screencast_restore_token").toString();

    QVariantMap opts;
    opts["handle_token"] = reqToken;
    opts["types"] = uint(1);         // MONITOR
    opts["multiple"] = false;
    opts["cursor_mode"] = uint(2);   // EMBEDDED
    opts["persist_mode"] = uint(2);  // PERMANENT
    if (!restoreToken.isEmpty()) opts["restore_token"] = restoreToken;

    QDBusMessage call = QDBusMessage::createMethodCall(
        kPortalBus, kPortalPath, kScreenCastIface, "SelectSources");
    call.setArguments({QVariant::fromValue(QDBusObjectPath(session)), opts});

    QDBusMessage reply = bus.call(call, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        abortScreenCast("SelectSources call failed: " + reply.errorMessage());
    }
}

void Portal::onSelectSourcesResponse(const QDBusMessage &msg) {
    disconnectResponse(m_screenCastReqPath,
                       SLOT(onSelectSourcesResponse(QDBusMessage)));
    m_screenCastReqPath.clear();

    uint code = 0;
    QVariantMap unused;
    if (!decodeResponse(msg, code, unused)) {
        abortScreenCast("malformed SelectSources Response");
        return;
    }

    if (code != 0) {
        abortScreenCast(code == 1 ? "user cancelled SelectSources"
                                  : QString("portal error %1").arg(code));
        return;
    }

    // --- 3. Start --- (this is the step that shows the source picker)
    auto bus = QDBusConnection::sessionBus();
    QString reqToken = nextToken("kscreen_cs_");
    m_screenCastReqPath = QString("/org/freedesktop/portal/desktop/request/%1/%2")
                              .arg(senderToken(), reqToken);

    if (!bus.connect(kPortalBus, m_screenCastReqPath, kRequestIface,
                     "Response", this,
                     SLOT(onStartResponse(QDBusMessage)))) {
        abortScreenCast("failed to subscribe to Start Response");
        return;
    }

    QVariantMap opts;
    opts["handle_token"] = reqToken;

    QDBusMessage call = QDBusMessage::createMethodCall(
        kPortalBus, kPortalPath, kScreenCastIface, "Start");
    call.setArguments({
        QVariant::fromValue(QDBusObjectPath(m_sessionHandle)),
        QString(""),  // parent_window — Wayland handles its own focus
        opts,
    });

    QDBusMessage reply = bus.call(call, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        abortScreenCast("Start call failed: " + reply.errorMessage());
    }
    // The actual user-facing dialog appears now. We may sit waiting for
    // the Response signal for as long as the user takes to act.
}

void Portal::onStartResponse(const QDBusMessage &msg) {
    disconnectResponse(m_screenCastReqPath,
                       SLOT(onStartResponse(QDBusMessage)));
    m_screenCastReqPath.clear();

    uint code = 0;
    QVariantMap results;
    if (!decodeResponse(msg, code, results)) {
        abortScreenCast("malformed Start Response");
        return;
    }

    if (code != 0) {
        abortScreenCast(code == 1 ? "user cancelled Start"
                                  : QString("portal error %1").arg(code));
        return;
    }

    QSettings settings;
    QString newRestore = results.value("restore_token").toString();
    if (!newRestore.isEmpty()) {
        settings.setValue("portal/screencast_restore_token", newRestore);
    }

    finalizeScreenCastFromStart(results);
}

void Portal::finalizeScreenCastFromStart(const QVariantMap &results) {
    // results["streams"] is a(ua{sv}). We must demarshal manually — and
    // we deliberately skip the props dict because Mutter puts (ii)
    // structs inside it (position/size) which crash QtDBus's auto
    // QVariantMap conversion.
    QVariant streamsVar = results.value("streams");
    if (!streamsVar.isValid()) {
        abortScreenCast("Start returned no streams field");
        return;
    }
    QDBusArgument arg = streamsVar.value<QDBusArgument>();
    arg.beginArray();
    uint nodeId = 0;
    while (!arg.atEnd()) {
        arg.beginStructure();
        uint id = 0;
        arg >> id;
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            QString key;
            QDBusVariant value;
            arg >> key >> value;
            arg.endMapEntry();
        }
        arg.endMap();
        arg.endStructure();
        if (nodeId == 0) nodeId = id;
    }
    arg.endArray();

    if (nodeId == 0) {
        abortScreenCast("Start streams array was empty");
        return;
    }

    // OpenPipeWireRemote is a plain method call (no Request indirection)
    // so we can issue it synchronously here.
    auto bus = QDBusConnection::sessionBus();
    QDBusMessage openMsg = QDBusMessage::createMethodCall(
        kPortalBus, kPortalPath, kScreenCastIface, "OpenPipeWireRemote");
    openMsg.setArguments({
        QVariant::fromValue(QDBusObjectPath(m_sessionHandle)),
        QVariantMap{},
    });
    QDBusMessage openReply = bus.call(openMsg, QDBus::Block, 5000);
    if (openReply.type() != QDBusMessage::ReplyMessage) {
        abortScreenCast("OpenPipeWireRemote failed: " + openReply.errorMessage());
        return;
    }
    QDBusUnixFileDescriptor pwFdWrap =
        openReply.arguments().value(0).value<QDBusUnixFileDescriptor>();
    if (!pwFdWrap.isValid()) {
        abortScreenCast("OpenPipeWireRemote returned invalid fd");
        return;
    }
    // dup() so the fd outlives the QDBusUnixFileDescriptor wrapper.
    int dupFd = ::dup(pwFdWrap.fileDescriptor());
    if (dupFd < 0) {
        abortScreenCast(QString("dup() of PipeWire fd failed: errno=%1").arg(errno));
        return;
    }
    int flags = fcntl(dupFd, F_GETFD);
    if (flags >= 0) fcntl(dupFd, F_SETFD, flags & ~FD_CLOEXEC);

    if (m_pwFd >= 0) ::close(m_pwFd);
    m_pwFd = dupFd;
    m_pwNodeId = nodeId;
    m_screenCastInFlight = false;
    qDebug() << "Portal: ScreenCast ready, node id =" << nodeId
             << "fd =" << m_pwFd;
    emit screenCastReady(m_pwNodeId, m_pwFd);
}

void Portal::abortScreenCast(const QString &reason) {
    qWarning() << "Portal: screencast aborted —" << reason;
    if (!m_sessionHandle.isEmpty()) {
        auto bus = QDBusConnection::sessionBus();
        QDBusMessage msg = QDBusMessage::createMethodCall(
            kPortalBus, m_sessionHandle,
            "org.freedesktop.portal.Session", "Close");
        bus.call(msg, QDBus::Block, 2000);
    }
    m_sessionHandle.clear();
    m_screenCastReqPath.clear();
    m_screenCastSessionToken.clear();
    m_screenCastInFlight = false;
    if (m_pwFd >= 0) {
        ::close(m_pwFd);
        m_pwFd = -1;
    }
    m_pwNodeId = 0;
    emit screenCastFailed(reason);
}

void Portal::stopScreenCast() {
    if (m_sessionHandle.isEmpty() && m_pwFd < 0 && !m_screenCastInFlight) {
        return;
    }
    if (!m_sessionHandle.isEmpty()) {
        auto bus = QDBusConnection::sessionBus();
        QDBusMessage msg = QDBusMessage::createMethodCall(
            kPortalBus, m_sessionHandle,
            "org.freedesktop.portal.Session", "Close");
        bus.call(msg, QDBus::Block, 2000);
    }
    if (m_pwFd >= 0) {
        ::close(m_pwFd);
        m_pwFd = -1;
    }
    m_sessionHandle.clear();
    m_screenCastReqPath.clear();
    m_screenCastSessionToken.clear();
    m_screenCastInFlight = false;
    m_pwNodeId = 0;
}

#endif // HAS_DBUS
