#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>

struct CanvasItemState {
    QString type;
    QString label;
    int x = 0, y = 0, w = 0, h = 0;
    QString device;
    QString filePath;
    int shape = 0;
    int gifLoop = 2; // continuous
    int gifLoopMax = 3;
};

struct CanvasState {
    QString mode = "landscape";
    QList<CanvasItemState> items;
    QString titleColor;
    bool audioEnabled = true;
    QString presenter;
};

class Config {
public:
    static Config &instance();

    void load();
    void save();

    QString outputDir;
    QString defaultPresenter;
    QString logoDirectory;
    QString titleColor = "#62A4C7";
    QString bgColor = "white";
    bool normalizeAudio = true;

    CanvasState canvasState;

    int nextRecordingNumber() const;

    // YouTube
    QString youtubeClientId;
    QString youtubeClientSecret;

private:
    Config() = default;
    QString configPath() const;
};
