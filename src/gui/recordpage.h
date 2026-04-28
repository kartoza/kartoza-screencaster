/**
 * @file recordpage.h
 * @brief Recording configuration and control page.
 */

#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QRadioButton>
#include <QComboBox>
#include <QButtonGroup>
#include <QListWidget>
#include "gui/canvas.h"
#include "config/config.h"
#include "recorder/recorder.h"
#include "monitor/monitor.h"
#include "webcam/webcam.h"

/**
 * @class RecordPage
 * @brief Page widget for configuring and controlling screen recordings.
 *
 * Provides metadata input fields (title, presenter, description), a WYSIWYG
 * canvas preview, recording controls, and layer management.
 */
class RecordPage : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Construct the record page.
     * @param parent Optional parent widget.
     */
    explicit RecordPage(QWidget *parent = nullptr);

    /**
     * @brief Access the underlying Recorder instance.
     * @return Pointer to the Recorder used for capture.
     */
    Recorder *recorder() { return m_recorder; }

protected:
    /** @brief Refresh layer list when page becomes visible. */
    void showEvent(QShowEvent *event) override;

signals:
    /** @brief Emitted when a recording session begins. */
    void recordingStarted();
    /** @brief Emitted when a recording session ends. */
    void recordingStopped();

public slots:
    /** @brief Handle the Start button click, initiating the countdown. */
    void onStartClicked();
    /** @brief Handle the Stop button click, ending the active recording. */
    void onStopClicked();
    /** @brief Handle the Pause button click, toggling pause state. */
    void onPauseClicked();
    /** @brief Decrement the pre-recording countdown timer. */
    void onCountdownTick();
    /** @brief Slot called when the Recorder signals it has started capturing. */
    void onRecorderStarted();
    /** @brief Slot called when the Recorder signals it has finished. */
    void onRecorderStopped();
    /**
     * @brief Slot called when the Recorder encounters an error.
     * @param error Human-readable error message.
     */
    void onRecorderError(const QString &error);

private:
    /** @brief Build the complete UI layout for this page. */
    void setupUI();
    /**
     * @brief Add a screen/monitor option to the canvas.
     * @param mon Monitor information describing the display.
     */
    void addScreen(const MonitorInfo &mon);

    // -- Form widgets --
    /** @brief Input field for the recording title. */
    QLineEdit *m_titleInput;
    /** @brief Input field for the episode/recording number. */
    QLineEdit *m_numberInput;
    /** @brief Input field for the presenter name. */
    QLineEdit *m_presenterInput;
    /** @brief Multi-line input for the recording description. */
    QTextEdit *m_descInput;
    /** @brief Checkbox to enable/disable audio capture. */
    QCheckBox *m_audioCheck;

    // -- Canvas --
    /** @brief WYSIWYG canvas preview of the recording layout. */
    Canvas *m_canvas;
    /** @brief Button group for selecting the canvas orientation mode. */
    QButtonGroup *m_modeGroup;

    // -- Controls --
    /** @brief Button to start recording. */
    QPushButton *m_startBtn;
    /** @brief Button to pause/resume recording. */
    QPushButton *m_pauseBtn;
    /** @brief Button to stop recording. */
    QPushButton *m_stopBtn;
    /** @brief Label showing the current recording status. */
    QLabel *m_statusLabel;
    /** @brief Label showing elapsed recording time. */
    QLabel *m_elapsedLabel;

    // -- State --
    /** @brief Timer driving the pre-recording countdown. */
    QTimer *m_countdownTimer;
    /** @brief Timer updating the elapsed time display. */
    QTimer *m_elapsedTimer;
    /** @brief Current countdown value in seconds. */
    int m_countdownVal = 0;
    /** @brief Whether a recording is currently in progress. */
    bool m_isRecording = false;
    /** @brief Guard to prevent saving during restore. */
    bool m_restoring = false;

    // -- Layer list --
    /** @brief List widget showing canvas layer stack. */
    QListWidget *m_layerList;
    /** @brief Rebuild the layer list from current canvas items. */
    void refreshLayerList();
    /** @brief Persist current canvas item layout to settings. */
    void saveCanvasState();
    /** @brief Restore canvas item layout from persisted settings. */
    void restoreCanvasState();
    /** @brief Populate the preset combobox from config. */
    void refreshPresetList();
    /** @brief Capture current canvas state into a CanvasState struct. */
    CanvasState captureCurrentState();
    /** @brief Apply a CanvasState to the canvas (clear + reimport). */
    void applyState(const CanvasState &state);

    /** @brief Preset selector combobox. */
    QComboBox *m_presetCombo;

    // -- Data --
    /** @brief Recorder instance managing the capture pipeline. */
    Recorder *m_recorder;
    /** @brief Available display monitors. */
    QList<MonitorInfo> m_monitors;
    /** @brief Available webcam devices. */
    QList<WebcamInfo> m_webcams;
};
