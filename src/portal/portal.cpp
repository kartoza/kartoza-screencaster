/**
 * @file portal.cpp
 * @brief Implementation of xdg-desktop-portal wrappers.
 *
 * The portal protocol is fully asynchronous: every method takes a
 * `handle_token`, returns a Request object path, and later emits a
 * Response signal on that path. To present a synchronous API we:
 *
 *   1. Generate a unique handle_token.
 *   2. Compute the predictable Request path
 *      (/org/freedesktop/portal/desktop/request/<sender>/<token>).
 *   3. Subscribe to its Response signal BEFORE issuing the call so we
 *      cannot miss a fast response.
 *   4. Call the method, then spin a local QEventLoop until the slot or
 *      a timeout fires.
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
#include <QScopeGuard>
#include <fcntl.h>
#include <unistd.h>
#include <QEventLoop>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {
constexpr auto kPortalBus = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kRequestIface = "org.freedesktop.portal.Request";
constexpr auto kScreenshotIface = "org.freedesktop.portal.Screenshot";
constexpr auto kScreenCastIface = "org.freedesktop.portal.ScreenCast";
constexpr int kCallTimeoutMs = 30000;
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

void Portal::onResponse(uint response, const QVariantMap &results) {
    m_responseReceived = true;
    m_responseCode = response;
    m_responseResults = results;
}

QString Portal::screenshot() {
    if (m_callInFlight) {
        // Yielding instead of nesting: see isBusy() docs. Canvas just
        // keeps showing whatever shot is already on screen.
        return {};
    }
    m_callInFlight = true;
    auto guard = qScopeGuard([this]() { m_callInFlight = false; });

    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return {};

    QString token = nextToken("kscreen_shot_");
    QString requestPath = QString("/org/freedesktop/portal/desktop/request/%1/%2")
                              .arg(senderToken(), token);

    m_responseReceived = false;
    m_responseResults.clear();

    bool connected = bus.connect(kPortalBus, requestPath, kRequestIface,
                                 "Response", this,
                                 SLOT(onResponse(uint, QVariantMap)));
    if (!connected) {
        qWarning() << "Portal::screenshot: failed to subscribe to" << requestPath;
        return {};
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
        qWarning() << "Portal::screenshot: call failed:" << reply.errorMessage();
        bus.disconnect(kPortalBus, requestPath, kRequestIface, "Response",
                       this, SLOT(onResponse(uint, QVariantMap)));
        return {};
    }

    // Spin event loop until response arrives or we time out.
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [this, &loop]() {
        if (m_responseReceived) loop.quit();
    });
    timer.start(kCallTimeoutMs);
    poll.start(50);
    loop.exec();

    bus.disconnect(kPortalBus, requestPath, kRequestIface, "Response",
                   this, SLOT(onResponse(uint, QVariantMap)));

    if (!m_responseReceived || m_responseCode != 0) {
        qWarning() << "Portal::screenshot: no response or non-zero code"
                   << m_responseCode;
        return {};
    }

    QString uri = m_responseResults.value("uri").toString();
    if (uri.isEmpty()) return {};
    return QUrl(uri).toLocalFile();
}

uint Portal::startScreenCast() {
    if (m_callInFlight) {
        qWarning() << "Portal::startScreenCast called while another portal "
                      "call is in flight — refusing to re-enter.";
        return 0;
    }
    m_callInFlight = true;
    auto guard = qScopeGuard([this]() { m_callInFlight = false; });

    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return 0;
    if (m_pwNodeId != 0 && !m_sessionHandle.isEmpty()) {
        // Already active — reuse.
        return m_pwNodeId;
    }

    // --- 1. CreateSession ---
    QString reqToken = nextToken("kscreen_cs_");
    QString sessionToken = nextToken("kscreen_ss_");
    QString requestPath = QString("/org/freedesktop/portal/desktop/request/%1/%2")
                              .arg(senderToken(), reqToken);

    auto callPortal = [&](const QString &method,
                          const QVariantList &positional,
                          const QVariantMap &opts) -> bool {
        m_responseReceived = false;
        m_responseResults.clear();
        bool connected = bus.connect(kPortalBus, requestPath, kRequestIface,
                                     "Response", this,
                                     SLOT(onResponse(uint, QVariantMap)));
        if (!connected) return false;

        QDBusMessage msg = QDBusMessage::createMethodCall(
            kPortalBus, kPortalPath, kScreenCastIface, method);
        QVariantList args = positional;
        args.append(opts);
        msg.setArguments(args);

        QDBusMessage reply = bus.call(msg, QDBus::Block, 5000);
        if (reply.type() != QDBusMessage::ReplyMessage) {
            qWarning() << "Portal::startScreenCast" << method
                       << "failed:" << reply.errorMessage();
            bus.disconnect(kPortalBus, requestPath, kRequestIface, "Response",
                           this, SLOT(onResponse(uint, QVariantMap)));
            return false;
        }

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, [this, &loop]() {
            if (m_responseReceived) loop.quit();
        });
        timer.start(kCallTimeoutMs);
        poll.start(50);
        loop.exec();

        bus.disconnect(kPortalBus, requestPath, kRequestIface, "Response",
                       this, SLOT(onResponse(uint, QVariantMap)));
        return m_responseReceived && m_responseCode == 0;
    };

    QVariantMap createOpts;
    createOpts["handle_token"] = reqToken;
    createOpts["session_handle_token"] = sessionToken;
    if (!callPortal("CreateSession", {}, createOpts)) {
        qWarning() << "Portal: CreateSession failed";
        return 0;
    }
    QString session = m_responseResults.value("session_handle").toString();
    if (session.isEmpty()) {
        qWarning() << "Portal: CreateSession returned no session_handle";
        return 0;
    }
    m_sessionHandle = session;

    // --- 2. SelectSources ---
    reqToken = nextToken("kscreen_cs_");
    requestPath = QString("/org/freedesktop/portal/desktop/request/%1/%2")
                       .arg(senderToken(), reqToken);
    QSettings settings;
    QString restoreToken = settings.value("portal/screencast_restore_token").toString();

    QVariantMap selectOpts;
    selectOpts["handle_token"] = reqToken;
    selectOpts["types"] = uint(1);        // 1 = monitor
    selectOpts["multiple"] = false;
    selectOpts["cursor_mode"] = uint(2);  // 2 = embedded
    selectOpts["persist_mode"] = uint(2); // 2 = permanent across restarts
    if (!restoreToken.isEmpty()) {
        selectOpts["restore_token"] = restoreToken;
    }
    if (!callPortal("SelectSources",
                    QVariantList() << QVariant::fromValue(QDBusObjectPath(session)),
                    selectOpts)) {
        qWarning() << "Portal: SelectSources failed";
        m_sessionHandle.clear();
        return 0;
    }

    // --- 3. Start ---
    reqToken = nextToken("kscreen_cs_");
    requestPath = QString("/org/freedesktop/portal/desktop/request/%1/%2")
                       .arg(senderToken(), reqToken);
    QVariantMap startOpts;
    startOpts["handle_token"] = reqToken;
    if (!callPortal("Start",
                    QVariantList()
                        << QVariant::fromValue(QDBusObjectPath(session))
                        << QString(""),
                    startOpts)) {
        qWarning() << "Portal: Start failed";
        m_sessionHandle.clear();
        return 0;
    }

    // Persist restore_token for next session if the portal gave us one.
    QString newRestore = m_responseResults.value("restore_token").toString();
    if (!newRestore.isEmpty()) {
        settings.setValue("portal/screencast_restore_token", newRestore);
    }

    // results["streams"] is a(ua{sv}) — an array of (node_id, props) tuples.
    // QtDBus surfaces this as a QDBusArgument we must demarshal manually.
    //
    // We deliberately do NOT read the props dict into a QVariantMap. The
    // portal puts complex types in there ("position":(ii), "size":(ii),
    // "source_type":u, …) and QtDBus's auto-demarshalling of a{sv} crashes
    // on those nested structs ("type struct 114 not a basic type" → SIGABRT).
    // We don't need the props anyway — just the node id — so we walk the
    // map manually and read each value as an opaque QDBusVariant.
    QVariant streamsVar = m_responseResults.value("streams");
    if (!streamsVar.isValid()) {
        qWarning() << "Portal: Start returned no streams";
        m_sessionHandle.clear();
        return 0;
    }
    QDBusArgument arg = streamsVar.value<QDBusArgument>();
    arg.beginArray();
    uint nodeId = 0;
    while (!arg.atEnd()) {
        arg.beginStructure();
        uint id = 0;
        arg >> id;
        // Skip the a{sv} props dict by iterating entries with opaque values.
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
        qWarning() << "Portal: Start streams array was empty";
        m_sessionHandle.clear();
        return 0;
    }

    // --- 4. OpenPipeWireRemote ---
    // Portal screencast nodes are only consumable through a private
    // PipeWire connection opened here. The default PW socket sees the
    // node but the screencast permission grant is bound to this FD,
    // so a `pipewiresrc path=N` without `fd=…` connects but never
    // receives buffers — recordings come out empty.
    QDBusMessage openMsg = QDBusMessage::createMethodCall(
        kPortalBus, kPortalPath, kScreenCastIface, "OpenPipeWireRemote");
    openMsg << QVariant::fromValue(QDBusObjectPath(session))
            << QVariant::fromValue(QVariantMap{});

    QDBusMessage openReply = bus.call(openMsg, QDBus::Block, 5000);
    if (openReply.type() != QDBusMessage::ReplyMessage) {
        qWarning() << "Portal: OpenPipeWireRemote failed:"
                   << openReply.errorMessage();
        m_sessionHandle.clear();
        return 0;
    }
    QDBusUnixFileDescriptor pwFdWrap =
        openReply.arguments().value(0).value<QDBusUnixFileDescriptor>();
    if (!pwFdWrap.isValid()) {
        qWarning() << "Portal: OpenPipeWireRemote returned invalid fd";
        m_sessionHandle.clear();
        return 0;
    }
    // QDBusUnixFileDescriptor closes the wrapped fd in its destructor.
    // dup() so the fd outlives the wrapper and we can hand it to a
    // child process that will keep it open for the recording.
    int rawFd = pwFdWrap.fileDescriptor();
    int dupFd = ::dup(rawFd);
    if (dupFd < 0) {
        qWarning() << "Portal: dup() of PipeWire fd failed:" << errno;
        m_sessionHandle.clear();
        return 0;
    }
    // Make sure the fd survives fork+exec. (dup() copies the open file
    // entry but does NOT copy FD_CLOEXEC, so this is just belt-and-braces.)
    int flags = fcntl(dupFd, F_GETFD);
    if (flags >= 0) fcntl(dupFd, F_SETFD, flags & ~FD_CLOEXEC);

    if (m_pwFd >= 0) ::close(m_pwFd);
    m_pwFd = dupFd;
    m_pwNodeId = nodeId;
    qDebug() << "Portal: ScreenCast started, PipeWire node id =" << nodeId
             << "fd =" << m_pwFd;
    return nodeId;
}

void Portal::stopScreenCast() {
    if (m_sessionHandle.isEmpty() && m_pwFd < 0) return;
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
    m_pwNodeId = 0;
}

#endif // HAS_DBUS
