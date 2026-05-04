/**
 * @file dbusservice.h
 * @brief D-Bus interface for remote control of recording actions.
 *
 * Exposes methods on the session bus that desktop environments can bind
 * to global keyboard shortcuts: StartRecording, StopRecording,
 * PauseRecording, ToggleRecording.
 */
#pragma once

#include <QObject>

class RecordPage;

/**
 * @class DBusService
 * @brief Session D-Bus service for controlling recordings from external shortcuts.
 *
 * Registers on org.kartoza.Screencaster with path /Screencaster.
 * Desktop environments (COSMIC, GNOME, KDE, Hyprland) can invoke these
 * methods via dbus-send or bind them to keyboard shortcuts.
 */
class DBusService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kartoza.Screencaster")

public:
    explicit DBusService(RecordPage *recordPage, QObject *parent = nullptr);

public slots:
    /** @brief Start a new recording (with countdown). */
    Q_SCRIPTABLE void StartRecording();
    /** @brief Stop the current recording. */
    Q_SCRIPTABLE void StopRecording();
    /** @brief Toggle pause/resume on the current recording. */
    Q_SCRIPTABLE void PauseRecording();
    /** @brief Toggle: start if idle, pause if recording, resume if paused. */
    Q_SCRIPTABLE void ToggleRecording();
    /** @brief Return current state: "idle", "recording", "paused", "processing". */
    Q_SCRIPTABLE QString Status();

private:
    RecordPage *m_recordPage;
};
