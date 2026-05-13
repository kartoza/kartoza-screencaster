/**
 * @file youtube.h
 * @brief YouTube Data API v3 integration: OAuth2, upload, channel info.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpServer>

/**
 * @brief YouTube metadata stored alongside each recording.
 */
struct YouTubeMetadata {
    QString videoId;
    QString videoUrl;
    QString privacy;     // public, unlisted, private
    QString uploadedAt;
    QString channelName;
    QString channelId;
    QString playlistId;

    bool isUploaded() const { return !videoId.isEmpty(); }
    QJsonObject toJson() const;
    static YouTubeMetadata fromJson(const QJsonObject &obj);
};

/**
 * @brief Options for a YouTube video upload.
 */
struct UploadOptions {
    QString videoPath;
    QString title;
    QString description;
    QStringList tags;
    QString categoryId = "28"; // Science & Technology
    QString privacy = "unlisted";
    QString playlistId;
};

/**
 * @brief Playlist summary from YouTube API.
 */
struct PlaylistInfo {
    QString id;
    QString title;
};

/**
 * @class YouTube
 * @brief Manages OAuth2 authentication and YouTube Data API operations.
 *
 * Single-account. Tokens stored in ~/.config/kartoza-screencaster/youtube_token.json.
 * All network operations are asynchronous via signals.
 */
class YouTube : public QObject {
    Q_OBJECT
public:
    explicit YouTube(QObject *parent = nullptr);

    /** @brief Whether we have a saved token (may be expired). */
    bool hasToken() const;

    /** @brief Whether credentials (client ID + secret) are configured. */
    bool hasCredentials() const;

    /** @brief Start OAuth2 flow: opens browser, waits for callback. */
    void authenticate();

    /** @brief Revoke and delete the saved token. */
    void logout();

    /** @brief Fetch the authenticated user's channel name. */
    void fetchChannelInfo();

    /** @brief Fetch the user's playlists. */
    void fetchPlaylists();

    /** @brief Upload a video with the given options. */
    void uploadVideo(const UploadOptions &opts);

    /** @brief Add a video to a YouTube playlist. */
    void addToPlaylist(const QString &videoId, const QString &playlistId);

    // --- Token file helpers (public for testing) ---

    /** @brief Returns the path to the token file. */
    static QString tokenFilePath();

    /** @brief Save a token JSON to disk. */
    static bool saveToken(const QJsonObject &token);

    /** @brief Load the token JSON from disk. Returns empty object if missing. */
    static QJsonObject loadToken();

    /** @brief Delete the token file from disk. */
    static bool deleteToken();

signals:
    /** @brief OAuth flow completed successfully. */
    void authenticated();
    /** @brief OAuth flow or token refresh failed. */
    void authError(const QString &error);
    /** @brief Logged out (token deleted). */
    void loggedOut();

    /** @brief Channel info fetched. */
    void channelInfoReady(const QString &channelName, const QString &channelId);
    /** @brief Channel info fetch failed. */
    void channelInfoError(const QString &error);

    /** @brief Playlists fetched. */
    void playlistsReady(const QList<PlaylistInfo> &playlists);

    /** @brief Upload progress update (0-100). */
    void uploadProgress(int percent);
    /** @brief Upload completed. */
    void uploadFinished(const QString &videoId, const QString &videoUrl);
    /** @brief Upload failed. */
    void uploadError(const QString &error);

private:
    /** @brief Exchange authorization code for tokens. */
    void exchangeCodeForToken(const QString &code, const QString &redirectUri,
                               const QString &codeVerifier);
    /** @brief Refresh the access token using the refresh token. */
    void refreshAccessToken(std::function<void(bool)> callback);
    /** @brief Make an authenticated GET request. */
    void authenticatedGet(const QUrl &url,
                          std::function<void(QJsonObject)> onSuccess,
                          std::function<void(QString)> onError);
    /** @brief Generate PKCE code verifier and challenge. */
    static QPair<QString, QString> generatePKCE();

    QNetworkAccessManager *m_nam;
    QTcpServer *m_callbackServer = nullptr;
};
