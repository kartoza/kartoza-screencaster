#pragma once
#include <QWidget>
#include <QProgressBar>
#include <QLabel>
#include <QTimer>
#include "recorder/recorder.h"

class ProcessingPage : public QWidget {
    Q_OBJECT
public:
    explicit ProcessingPage(QWidget *parent = nullptr);
    void startMonitoring(Recorder *recorder);

signals:
    void processingDone(bool success);

private:
    QList<QProgressBar*> m_bars;
    QList<QLabel*> m_statusLabels;
    QLabel *m_elapsedLabel;
    QTimer *m_elapsedTimer;
    QElapsedTimer m_elapsed;
};
