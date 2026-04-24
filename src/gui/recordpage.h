#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QRadioButton>
#include <QButtonGroup>
#include <QElapsedTimer>
#include "gui/canvas.h"
#include "recorder/recorder.h"
#include "monitor/monitor.h"
#include "webcam/webcam.h"

class RecordPage : public QWidget {
    Q_OBJECT

public:
    explicit RecordPage(QWidget *parent = nullptr);
    Recorder *recorder() { return m_recorder; }

signals:
    void recordingStarted();
    void recordingStopped();

public slots:
    void onStartClicked();
    void onStopClicked();
    void onPauseClicked();
    void onCountdownTick();
    void onRecorderStarted();
    void onRecorderStopped();
    void onRecorderError(const QString &error);

private:
    void setupUI();
    void addScreen(const MonitorInfo &mon);

    // Form
    QLineEdit *m_titleInput;
    QLineEdit *m_numberInput;
    QLineEdit *m_presenterInput;
    QTextEdit *m_descInput;
    QCheckBox *m_audioCheck;

    // Canvas
    Canvas *m_canvas;
    QButtonGroup *m_modeGroup;

    // Controls
    QPushButton *m_startBtn;
    QPushButton *m_pauseBtn;
    QPushButton *m_stopBtn;
    QLabel *m_statusLabel;
    QLabel *m_elapsedLabel;

    // State
    QTimer *m_countdownTimer;
    QTimer *m_elapsedTimer;
    int m_countdownVal = 0;
    QElapsedTimer m_elapsed;
    bool m_isRecording = false;

    // Data
    Recorder *m_recorder;
    QList<MonitorInfo> m_monitors;
    QList<WebcamInfo> m_webcams;
};
