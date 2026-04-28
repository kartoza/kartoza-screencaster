#pragma once

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoSink>
#include <QVideoFrame>

struct RecordingEntry {
    QString folder;
    QString dirName;
    QString title;
    QString status;
    QString date;
    qint64 duration = 0; // nanoseconds
    qint64 totalSize = 0;
    QString mergedFile;
    QString verticalFile;
    QString screenFile;
    QString audioFile;
    QString webcamFile;
};

class HistoryPage : public QWidget {
    Q_OBJECT

public:
    explicit HistoryPage(QWidget *parent = nullptr);
    void refresh();

signals:
    void reprocessRequested(const QString &folder);

private slots:
    void onRecordingSelected(int row);
    void onPlayClicked();
    void onStopPlayback();
    void onDeleteClicked();
    void onSearchChanged(const QString &text);

private:
    void setupUI();
    void loadRecordings();
    QString findBestVideo(const RecordingEntry &rec);

    QListWidget *m_list;
    QLineEdit *m_searchInput;

    // Player
    QLabel *m_videoLabel;
    QVideoSink *m_videoSink;
    QMediaPlayer *m_player;
    QAudioOutput *m_audioOutput;
    QPushButton *m_playBtn;
    QPushButton *m_stopBtn;
    QSlider *m_seekSlider;
    QLabel *m_timeLabel;
    bool m_playing = false;

    // Details
    QLabel *m_titleLabel;
    QLabel *m_statusLabel;
    QLabel *m_durationLabel;
    QLabel *m_filesLabel;
    QLabel *m_sizeLabel;

    // Actions
    QPushButton *m_deleteBtn;

    QList<RecordingEntry> m_recordings;
};
