#include "dbus/dbusservice.h"
#include "gui/recordpage.h"
#include <QDBusConnection>
#include <QDebug>

DBusService::DBusService(RecordPage *recordPage, QObject *parent)
    : QObject(parent), m_recordPage(recordPage) {

    QDBusConnection bus = QDBusConnection::sessionBus();

    if (!bus.registerService("org.kartoza.Screencaster")) {
        qWarning() << "D-Bus: failed to register service (another instance running?)";
        return;
    }
    if (!bus.registerObject("/Screencaster", this, QDBusConnection::ExportScriptableSlots)) {
        qWarning() << "D-Bus: failed to register object";
        return;
    }

    qDebug() << "D-Bus service registered: org.kartoza.Screencaster /Screencaster";
}

void DBusService::StartRecording() {
    auto *rec = m_recordPage->recorder();
    if (!rec->isRecording() && !rec->isPaused()) {
        QMetaObject::invokeMethod(m_recordPage, "onStartClicked", Qt::QueuedConnection);
    }
}

void DBusService::StopRecording() {
    auto *rec = m_recordPage->recorder();
    if (rec->isRecording() || rec->isPaused()) {
        QMetaObject::invokeMethod(m_recordPage, "onStopClicked", Qt::QueuedConnection);
    }
}

void DBusService::PauseRecording() {
    auto *rec = m_recordPage->recorder();
    if (rec->isRecording() || rec->isPaused()) {
        QMetaObject::invokeMethod(m_recordPage, "onPauseClicked", Qt::QueuedConnection);
    }
}

void DBusService::ToggleRecording() {
    auto *rec = m_recordPage->recorder();
    if (rec->isRecording()) {
        QMetaObject::invokeMethod(m_recordPage, "onPauseClicked", Qt::QueuedConnection);
    } else if (rec->isPaused()) {
        QMetaObject::invokeMethod(m_recordPage, "onPauseClicked", Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(m_recordPage, "onStartClicked", Qt::QueuedConnection);
    }
}

QString DBusService::Status() {
    auto *rec = m_recordPage->recorder();
    if (rec->isRecording()) return "recording";
    if (rec->isPaused()) return "paused";
    return "idle";
}
