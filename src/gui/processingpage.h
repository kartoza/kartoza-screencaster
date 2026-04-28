#pragma once
#include <QWidget>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include "recorder/recorder.h"

class ProcessingPage : public QWidget {
    Q_OBJECT
public:
    explicit ProcessingPage(QWidget *parent = nullptr);
    void startMonitoring(Recorder *recorder);

signals:
    void processingDone(bool success);
    void backToHistory();

private:
    QList<QProgressBar*> m_bars;
    QList<QLabel*> m_statusLabels;
    QList<QLabel*> m_errorLabels;
    QLabel *m_elapsedLabel;
    QLabel *m_summaryLabel;
    QPushButton *m_backBtn;
    QPushButton *m_openFolderBtn;
    QTimer *m_elapsedTimer;
    QElapsedTimer m_elapsed;
    Recorder *m_recorder = nullptr;
};
