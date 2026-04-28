/**
 * @file config.h
 * @brief Application configuration and canvas state persistence.
 */

#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>

/**
 * @brief Describes the state of a single item on the recording canvas.
 *
 * Items can be webcam overlays, GIF animations, images, or text labels.
 */
struct CanvasItemState {
    /** Item type identifier (e.g. "webcam", "gif", "image"). */
    QString type;
    /** Human-readable label shown in the layer list. */
    QString label;
    /** Position and size of the item on the canvas. */
    int x = 0, y = 0, w = 0, h = 0;
    /** Video4Linux device path (relevant for webcam items). */
    QString device;
    /** File path for image or GIF items. */
    QString filePath;
    /** Shape variant index (e.g. 0 = rectangle, 1 = circle). */
    int shape = 0;
    /** GIF loop mode (2 = continuous). */
    int gifLoop = 2;
    /** Maximum number of GIF loop iterations. */
    int gifLoopMax = 3;
};

/**
 * @brief Holds the full state of the recording canvas.
 */
struct CanvasState {
    /** Canvas orientation mode ("landscape" or "portrait"). */
    QString mode = "landscape";
    /** Ordered list of canvas items (back-to-front). */
    QList<CanvasItemState> items;
    /** CSS colour string used for the title text. */
    QString titleColor;
    /** Whether audio recording is enabled. */
    bool audioEnabled = true;
    /** Name of the presenter shown on screen. */
    QString presenter;
};

/**
 * @brief Singleton that manages application-wide settings.
 *
 * Settings are serialised to a JSON file in the user's config directory.
 * Access the single instance via Config::instance().
 */
class Config {
public:
    /** @brief Returns the singleton Config instance. */
    static Config &instance();

    /** @brief Loads settings from the JSON config file on disk. */
    void load();
    /** @brief Saves the current settings to the JSON config file. */
    void save();

    /** Directory where finished recordings are stored. */
    QString outputDir;
    /** Default presenter name pre-filled on new recordings. */
    QString defaultPresenter;
    /** Directory containing logo image files. */
    QString logoDirectory;
    /** CSS colour string for the title overlay (default "#62A4C7"). */
    QString titleColor = "#62A4C7";
    /** CSS colour string for the canvas background (default "white"). */
    QString bgColor = "white";
    /** Whether to normalise audio levels in post-processing. */
    bool normalizeAudio = true;

    /** Current canvas layout state persisted between sessions. */
    CanvasState canvasState;

    /** @brief Returns the next sequential recording number based on existing files. */
    int nextRecordingNumber() const;

    /** YouTube OAuth2 client ID for upload integration. */
    QString youtubeClientId;
    /** YouTube OAuth2 client secret for upload integration. */
    QString youtubeClientSecret;

private:
    Config() = default;
    /** @brief Returns the absolute path to the JSON config file. */
    QString configPath() const;
};
