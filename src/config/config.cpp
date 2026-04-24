#include "config/config.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

Config &Config::instance() {
    static Config cfg;
    return cfg;
}

QString Config::configPath() const {
    QString dir = QDir::homePath() + "/.config/kartoza-screencaster";
    QDir().mkpath(dir);
    return dir + "/config.json";
}

void Config::load() {
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonObject root = doc.object();

    outputDir = root["output_dir"].toString();
    defaultPresenter = root["default_presenter"].toString();
    logoDirectory = root["logo_directory"].toString();
    bgColor = root["bg_color"].toString("white");
    normalizeAudio = root["audio_processing"].toObject()["normalize_enabled"].toBool(true);

    auto logos = root["last_used_logos"].toObject();
    titleColor = logos["title_color"].toString("#62A4C7");

    auto yt = root["youtube"].toObject();
    youtubeClientId = yt["client_id"].toString();
    youtubeClientSecret = yt["client_secret"].toString();

    // Canvas state
    auto cs = root["canvas_state"].toObject();
    if (!cs.isEmpty()) {
        canvasState.mode = cs["mode"].toString("landscape");
        canvasState.titleColor = cs["title_color"].toString();
        canvasState.audioEnabled = cs["audio_enabled"].toBool(true);
        canvasState.presenter = cs["presenter"].toString();

        canvasState.items.clear();
        for (const auto &val : cs["items"].toArray()) {
            auto item = val.toObject();
            CanvasItemState s;
            s.type = item["type"].toString();
            s.label = item["label"].toString();
            s.x = item["x"].toInt();
            s.y = item["y"].toInt();
            s.w = item["w"].toInt();
            s.h = item["h"].toInt();
            s.device = item["device"].toString();
            s.filePath = item["file_path"].toString();
            s.shape = item["shape"].toInt();
            s.gifLoop = item["gif_loop"].toInt(2);
            s.gifLoopMax = item["gif_loop_max"].toInt(3);
            canvasState.items.append(s);
        }
    }
}

void Config::save() {
    QJsonObject root;
    root["output_dir"] = outputDir;
    root["default_presenter"] = defaultPresenter;
    root["logo_directory"] = logoDirectory;
    root["bg_color"] = bgColor;

    QJsonObject audio;
    audio["normalize_enabled"] = normalizeAudio;
    root["audio_processing"] = audio;

    QJsonObject logos;
    logos["title_color"] = titleColor;
    root["last_used_logos"] = logos;

    QJsonObject yt;
    yt["client_id"] = youtubeClientId;
    yt["client_secret"] = youtubeClientSecret;
    root["youtube"] = yt;

    // Canvas state
    QJsonObject cs;
    cs["mode"] = canvasState.mode;
    cs["title_color"] = canvasState.titleColor;
    cs["audio_enabled"] = canvasState.audioEnabled;
    cs["presenter"] = canvasState.presenter;

    QJsonArray items;
    for (const auto &s : canvasState.items) {
        QJsonObject item;
        item["type"] = s.type;
        item["label"] = s.label;
        item["x"] = s.x;
        item["y"] = s.y;
        item["w"] = s.w;
        item["h"] = s.h;
        item["device"] = s.device;
        item["file_path"] = s.filePath;
        item["shape"] = s.shape;
        item["gif_loop"] = s.gifLoop;
        item["gif_loop_max"] = s.gifLoopMax;
        items.append(item);
    }
    cs["items"] = items;
    root["canvas_state"] = cs;

    QFile file(configPath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}
