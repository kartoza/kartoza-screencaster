#include "config/config.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QRegularExpression>

Config &Config::instance() {
  static Config cfg;
  return cfg;
}

QString Config::configPath() const {
  QString dir = QDir::homePath() + "/.config/kartoza-screencaster";
  QDir().mkpath(dir);
  return dir + "/config.json";
}

// --- CanvasState JSON helpers ---

static CanvasState canvasStateFromJson(const QJsonObject &cs) {
  CanvasState state;
  state.mode = cs["mode"].toString("landscape");
  state.titleColor = cs["title_color"].toString();
  state.audioEnabled = cs["audio_enabled"].toBool(true);
  state.presenter = cs["presenter"].toString();

  for (const auto &val : cs["items"].toArray()) {
    auto obj = val.toObject();
    CanvasItemState s;
    s.type = obj["type"].toString();
    s.label = obj["label"].toString();
    s.x = obj["x"].toInt();
    s.y = obj["y"].toInt();
    s.w = obj["w"].toInt();
    s.h = obj["h"].toInt();
    s.device = obj["device"].toString();
    s.filePath = obj["file_path"].toString();
    s.shape = obj["shape"].toInt();
    s.gifLoop = obj["gif_loop"].toInt(2);
    s.gifLoopMax = obj["gif_loop_max"].toInt(3);
    state.items.append(s);
  }
  return state;
}

static QJsonObject canvasStateToJson(const CanvasState &state) {
  QJsonObject cs;
  cs["mode"] = state.mode;
  cs["title_color"] = state.titleColor;
  cs["audio_enabled"] = state.audioEnabled;
  cs["presenter"] = state.presenter;

  QJsonArray items;
  for (const auto &s : state.items) {
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
  return cs;
}

// --- Load / Save ---

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
    canvasState = canvasStateFromJson(cs);
  }

  // Active preset name
  activePreset = root["active_preset"].toString();

  // Presets
  presets.clear();
  auto presetsObj = root["presets"].toObject();
  for (auto it = presetsObj.begin(); it != presetsObj.end(); ++it) {
    presets[it.key()] = canvasStateFromJson(it.value().toObject());
  }
}

int Config::nextRecordingNumber() const {
  QString dir = outputDir;
  if (dir.isEmpty()) dir = QDir::homePath() + "/Videos/Screencasts";
  QDir d(dir);
  if (!d.exists()) return 1;

  int highest = 0;
  QRegularExpression re("^(\\d{3})-");
  for (const auto &entry : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    auto match = re.match(entry);
    if (match.hasMatch()) {
      int num = match.captured(1).toInt();
      if (num > highest) highest = num;
    }
  }
  return highest + 1;
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
  root["canvas_state"] = canvasStateToJson(canvasState);

  // Active preset name
  root["active_preset"] = activePreset;

  // Presets
  QJsonObject presetsObj;
  for (auto it = presets.begin(); it != presets.end(); ++it) {
    presetsObj[it.key()] = canvasStateToJson(it.value());
  }
  root["presets"] = presetsObj;

  QFile file(configPath());
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(root).toJson());
    file.close();
  }
}
