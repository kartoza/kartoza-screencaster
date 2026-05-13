#include "youtube/youtube.h"
#include "config/config.h"
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

// Google OAuth2 endpoints
static const QString AUTH_URL = "https://accounts.google.com/o/oauth2/v2/auth";
static const QString TOKEN_URL = "https://oauth2.googleapis.com/token";
static const QString REVOKE_URL = "https://oauth2.googleapis.com/revoke";
static const QString YT_API_BASE = "https://www.googleapis.com/youtube/v3";
static const QString YT_UPLOAD_URL = "https://www.googleapis.com/upload/youtube/v3/videos";

// Scopes
static const QString SCOPES = "https://www.googleapis.com/auth/youtube.upload "
                               "https://www.googleapis.com/auth/youtube";

// --- YouTubeMetadata ---

QJsonObject YouTubeMetadata::toJson() const {
    QJsonObject obj;
    obj["video_id"] = videoId;
    obj["video_url"] = videoUrl;
    obj["privacy"] = privacy;
    obj["uploaded_at"] = uploadedAt;
    obj["channel_name"] = channelName;
    obj["channel_id"] = channelId;
    obj["playlist_id"] = playlistId;
    return obj;
}

YouTubeMetadata YouTubeMetadata::fromJson(const QJsonObject &obj) {
    YouTubeMetadata m;
    m.videoId = obj["video_id"].toString();
    m.videoUrl = obj["video_url"].toString();
    m.privacy = obj["privacy"].toString();
    m.uploadedAt = obj["uploaded_at"].toString();
    m.channelName = obj["channel_name"].toString();
    m.channelId = obj["channel_id"].toString();
    m.playlistId = obj["playlist_id"].toString();
    return m;
}

// --- YouTube ---

YouTube::YouTube(QObject *parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
}

QString YouTube::tokenFilePath() {
    QString dir = QDir::homePath() + "/.config/kartoza-screencaster";
    QDir().mkpath(dir);
    return dir + "/youtube_token.json";
}

bool YouTube::saveToken(const QJsonObject &token) {
    QFile file(tokenFilePath());
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(token).toJson());
    file.close();
    return true;
}

QJsonObject YouTube::loadToken() {
    QFile file(tokenFilePath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    return obj;
}

bool YouTube::deleteToken() {
    return QFile::remove(tokenFilePath());
}

bool YouTube::hasToken() const {
    return QFile::exists(tokenFilePath());
}

bool YouTube::hasCredentials() const {
    auto &cfg = Config::instance();
    return !cfg.youtubeClientId.isEmpty() && !cfg.youtubeClientSecret.isEmpty();
}

QPair<QString, QString> YouTube::generatePKCE() {
    // Generate 32 random bytes for verifier
    QByteArray raw(32, 0);
    QRandomGenerator::global()->fillRange(reinterpret_cast<quint32*>(raw.data()),
                                           raw.size() / sizeof(quint32));
    QString verifier = raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    // SHA256 of verifier = challenge
    QByteArray hash = QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256);
    QString challenge = hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    return {verifier, challenge};
}

void YouTube::authenticate() {
    if (!hasCredentials()) {
        emit authError("YouTube credentials not configured. Set Client ID and Secret in Settings.");
        return;
    }

    auto &cfg = Config::instance();

    // Start local TCP server to receive OAuth callback
    if (m_callbackServer) {
        m_callbackServer->close();
        m_callbackServer->deleteLater();
    }
    m_callbackServer = new QTcpServer(this);
    if (!m_callbackServer->listen(QHostAddress::LocalHost, 0)) {
        emit authError("Failed to start local callback server");
        return;
    }
    quint16 port = m_callbackServer->serverPort();
    QString redirectUri = QString("http://127.0.0.1:%1/callback").arg(port);

    auto [verifier, challenge] = generatePKCE();

    // Build state parameter for CSRF protection
    QByteArray stateRaw(16, 0);
    QRandomGenerator::global()->fillRange(reinterpret_cast<quint32*>(stateRaw.data()),
                                           stateRaw.size() / sizeof(quint32));
    QString state = stateRaw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    // Build authorization URL
    QUrl authUrl(AUTH_URL);
    QUrlQuery q;
    q.addQueryItem("client_id", cfg.youtubeClientId);
    q.addQueryItem("redirect_uri", redirectUri);
    q.addQueryItem("response_type", "code");
    q.addQueryItem("scope", SCOPES);
    q.addQueryItem("state", state);
    q.addQueryItem("code_challenge", challenge);
    q.addQueryItem("code_challenge_method", "S256");
    q.addQueryItem("access_type", "offline");
    q.addQueryItem("prompt", "consent");
    authUrl.setQuery(q);

    // Open browser
    QDesktopServices::openUrl(authUrl);

    // Wait for callback
    connect(m_callbackServer, &QTcpServer::newConnection, this,
            [this, state, verifier, redirectUri]() {
        auto *socket = m_callbackServer->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket, state, verifier, redirectUri]() {
            QByteArray data = socket->readAll();

            // Parse the GET request for code and state
            QString line = QString::fromUtf8(data).split("\r\n").first();
            // GET /callback?code=xxx&state=yyy HTTP/1.1
            QUrl requestUrl("http://localhost" + line.split(" ").value(1));
            QUrlQuery params(requestUrl);

            QString receivedState = params.queryItemValue("state");
            QString code = params.queryItemValue("code");
            QString error = params.queryItemValue("error");

            // Send response to browser
            QString html;
            if (!error.isEmpty()) {
                html = "<html><body><h2>Authentication Failed</h2><p>" + error +
                       "</p><p>You can close this window.</p></body></html>";
            } else {
                html = "<html><body><h2>Authentication Successful</h2>"
                       "<p>You can close this window and return to Kartoza Screencaster.</p></body></html>";
            }
            QString response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n" + html;
            socket->write(response.toUtf8());
            socket->flush();
            socket->disconnectFromHost();

            // Close server
            m_callbackServer->close();

            if (!error.isEmpty()) {
                emit authError("OAuth error: " + error);
                return;
            }
            if (receivedState != state) {
                emit authError("OAuth state mismatch (CSRF protection)");
                return;
            }
            if (code.isEmpty()) {
                emit authError("No authorization code received");
                return;
            }

            exchangeCodeForToken(code, redirectUri, verifier);
        });
    });
}

void YouTube::exchangeCodeForToken(const QString &code, const QString &redirectUri,
                                    const QString &codeVerifier) {
    auto &cfg = Config::instance();

    QUrlQuery body;
    body.addQueryItem("code", code);
    body.addQueryItem("client_id", cfg.youtubeClientId);
    body.addQueryItem("client_secret", cfg.youtubeClientSecret);
    body.addQueryItem("redirect_uri", redirectUri);
    body.addQueryItem("grant_type", "authorization_code");
    body.addQueryItem("code_verifier", codeVerifier);

    QNetworkRequest req{QUrl(TOKEN_URL)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    auto *reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit authError("Token exchange failed: " + reply->errorString());
            return;
        }
        QJsonObject token = QJsonDocument::fromJson(reply->readAll()).object();
        if (!token.contains("access_token")) {
            emit authError("Token response missing access_token");
            return;
        }
        saveToken(token);
        emit authenticated();
    });
}

void YouTube::refreshAccessToken(std::function<void(bool)> callback) {
    QJsonObject token = loadToken();
    QString refreshToken = token["refresh_token"].toString();
    if (refreshToken.isEmpty()) {
        callback(false);
        return;
    }

    auto &cfg = Config::instance();
    QUrlQuery body;
    body.addQueryItem("client_id", cfg.youtubeClientId);
    body.addQueryItem("client_secret", cfg.youtubeClientSecret);
    body.addQueryItem("refresh_token", refreshToken);
    body.addQueryItem("grant_type", "refresh_token");

    QNetworkRequest req{QUrl(TOKEN_URL)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    auto *reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback, refreshToken]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            callback(false);
            return;
        }
        QJsonObject newToken = QJsonDocument::fromJson(reply->readAll()).object();
        if (!newToken.contains("access_token")) {
            callback(false);
            return;
        }
        // Preserve refresh_token if not returned in response
        if (!newToken.contains("refresh_token")) {
            newToken["refresh_token"] = refreshToken;
        }
        saveToken(newToken);
        callback(true);
    });
}

void YouTube::authenticatedGet(const QUrl &url,
                                std::function<void(QJsonObject)> onSuccess,
                                std::function<void(QString)> onError) {
    QJsonObject token = loadToken();
    QString accessToken = token["access_token"].toString();
    if (accessToken.isEmpty()) {
        onError("No access token available. Please authenticate first.");
        return;
    }

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());

    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, onSuccess, onError]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::AuthenticationRequiredError ||
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401) {
            // Try refresh
            refreshAccessToken([this, url, onSuccess, onError](bool ok) {
                if (!ok) {
                    onError("Token expired and refresh failed. Please re-authenticate.");
                    return;
                }
                // Retry with new token
                QJsonObject t = loadToken();
                QNetworkRequest retryReq(url);
                retryReq.setRawHeader("Authorization", ("Bearer " + t["access_token"].toString()).toUtf8());
                auto *r2 = m_nam->get(retryReq);
                connect(r2, &QNetworkReply::finished, this, [r2, onSuccess, onError]() {
                    r2->deleteLater();
                    if (r2->error() != QNetworkReply::NoError) {
                        onError("API request failed: " + r2->errorString());
                        return;
                    }
                    onSuccess(QJsonDocument::fromJson(r2->readAll()).object());
                });
            });
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            onError("API request failed: " + reply->errorString());
            return;
        }
        onSuccess(QJsonDocument::fromJson(reply->readAll()).object());
    });
}

void YouTube::logout() {
    QJsonObject token = loadToken();
    QString accessToken = token["access_token"].toString();

    if (!accessToken.isEmpty()) {
        // Revoke token (best effort)
        QUrl url(REVOKE_URL);
        QUrlQuery q;
        q.addQueryItem("token", accessToken);
        url.setQuery(q);
        auto *reply = m_nam->post(QNetworkRequest(url), QByteArray());
        connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
    }

    deleteToken();
    emit loggedOut();
}

void YouTube::fetchChannelInfo() {
    QUrl url(YT_API_BASE + "/channels");
    QUrlQuery q;
    q.addQueryItem("part", "snippet");
    q.addQueryItem("mine", "true");
    url.setQuery(q);

    authenticatedGet(url,
        [this](const QJsonObject &data) {
            auto items = data["items"].toArray();
            if (items.isEmpty()) {
                emit channelInfoError("No channel found for this account");
                return;
            }
            auto snippet = items[0].toObject()["snippet"].toObject();
            QString name = snippet["title"].toString();
            QString id = items[0].toObject()["id"].toString();
            emit channelInfoReady(name, id);
        },
        [this](const QString &err) {
            emit channelInfoError(err);
        });
}

void YouTube::fetchPlaylists() {
    QUrl url(YT_API_BASE + "/playlists");
    QUrlQuery q;
    q.addQueryItem("part", "snippet");
    q.addQueryItem("mine", "true");
    q.addQueryItem("maxResults", "50");
    url.setQuery(q);

    authenticatedGet(url,
        [this](const QJsonObject &data) {
            QList<PlaylistInfo> playlists;
            for (const auto &item : data["items"].toArray()) {
                auto obj = item.toObject();
                PlaylistInfo pi;
                pi.id = obj["id"].toString();
                pi.title = obj["snippet"].toObject()["title"].toString();
                playlists.append(pi);
            }
            emit playlistsReady(playlists);
        },
        [this](const QString &) {
            emit playlistsReady({}); // empty list on error
        });
}

void YouTube::uploadVideo(const UploadOptions &opts) {
    if (opts.videoPath.isEmpty() || !QFile::exists(opts.videoPath)) {
        emit uploadError("Video file not found: " + opts.videoPath);
        return;
    }

    // First ensure we have a valid token (refresh if needed)
    QJsonObject token = loadToken();
    if (token.isEmpty()) {
        emit uploadError("Not authenticated. Please connect your YouTube account first.");
        return;
    }

    auto doUpload = [this, opts]() {
        QJsonObject t = loadToken();
        QString accessToken = t["access_token"].toString();

        // Build video metadata
        QJsonObject snippet;
        snippet["title"] = opts.title;
        snippet["description"] = opts.description;
        snippet["categoryId"] = opts.categoryId;
        if (!opts.tags.isEmpty()) {
            QJsonArray tags;
            for (const auto &tag : opts.tags) tags.append(tag);
            snippet["tags"] = tags;
        }

        QJsonObject status;
        status["privacyStatus"] = opts.privacy;

        QJsonObject metadata;
        metadata["snippet"] = snippet;
        metadata["status"] = status;

        // Resumable upload: initiate
        QUrl url(YT_UPLOAD_URL);
        QUrlQuery q;
        q.addQueryItem("uploadType", "resumable");
        q.addQueryItem("part", "snippet,status");
        url.setQuery(q);

        QNetworkRequest req(url);
        req.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QFile *videoFile = new QFile(opts.videoPath);
        qint64 fileSize = videoFile->size();
        req.setRawHeader("X-Upload-Content-Length", QByteArray::number(fileSize));
        req.setRawHeader("X-Upload-Content-Type", "video/*");

        QByteArray metaBody = QJsonDocument(metadata).toJson(QJsonDocument::Compact);

        auto *initReply = m_nam->post(req, metaBody);
        connect(initReply, &QNetworkReply::finished, this,
                [this, initReply, videoFile, fileSize, opts]() {
            initReply->deleteLater();

            if (initReply->error() != QNetworkReply::NoError) {
                delete videoFile;
                QByteArray body = initReply->readAll();
                emit uploadError("Upload init failed: " + initReply->errorString() +
                                 "\n" + QString::fromUtf8(body));
                return;
            }

            // Get resumable upload URI from Location header
            QString uploadUri = QString::fromUtf8(
                initReply->rawHeader("Location"));
            if (uploadUri.isEmpty()) {
                delete videoFile;
                emit uploadError("No upload URI in response");
                return;
            }

            // Upload the file
            if (!videoFile->open(QIODevice::ReadOnly)) {
                delete videoFile;
                emit uploadError("Cannot open video file");
                return;
            }

            QJsonObject t2 = loadToken();
            QNetworkRequest uploadReq{QUrl(uploadUri)};
            uploadReq.setRawHeader("Authorization",
                                    ("Bearer " + t2["access_token"].toString()).toUtf8());
            uploadReq.setHeader(QNetworkRequest::ContentTypeHeader, "video/*");
            uploadReq.setHeader(QNetworkRequest::ContentLengthHeader, fileSize);

            auto *uploadReply = m_nam->put(uploadReq, videoFile);
            videoFile->setParent(uploadReply); // auto-delete with reply

            connect(uploadReply, &QNetworkReply::uploadProgress, this,
                    [this, fileSize](qint64 sent, qint64 total) {
                Q_UNUSED(total);
                if (fileSize > 0) {
                    emit uploadProgress(static_cast<int>(sent * 100 / fileSize));
                }
            });

            connect(uploadReply, &QNetworkReply::finished, this,
                    [this, uploadReply, opts]() {
                uploadReply->deleteLater();

                if (uploadReply->error() != QNetworkReply::NoError) {
                    QByteArray body = uploadReply->readAll();
                    emit uploadError("Upload failed: " + uploadReply->errorString() +
                                     "\n" + QString::fromUtf8(body));
                    return;
                }

                QJsonObject response = QJsonDocument::fromJson(uploadReply->readAll()).object();
                QString videoId = response["id"].toString();
                QString videoUrl = "https://www.youtube.com/watch?v=" + videoId;

                // Add to playlist if specified
                if (!opts.playlistId.isEmpty() && !videoId.isEmpty()) {
                    addToPlaylist(videoId, opts.playlistId);
                }

                emit uploadFinished(videoId, videoUrl);
            });
        });
    };

    // Try refreshing token first, then upload
    refreshAccessToken([this, doUpload](bool ok) {
        if (!ok && !loadToken().contains("access_token")) {
            emit uploadError("Token refresh failed. Please re-authenticate.");
            return;
        }
        doUpload();
    });
}

void YouTube::addToPlaylist(const QString &videoId, const QString &playlistId) {
    QJsonObject t = loadToken();
    QString accessToken = t["access_token"].toString();
    if (accessToken.isEmpty()) return;

    QJsonObject snippet;
    snippet["playlistId"] = playlistId;
    QJsonObject resourceId;
    resourceId["kind"] = "youtube#video";
    resourceId["videoId"] = videoId;
    snippet["resourceId"] = resourceId;

    QJsonObject body;
    body["snippet"] = snippet;

    QUrl url(YT_API_BASE + "/playlistItems");
    QUrlQuery q;
    q.addQueryItem("part", "snippet");
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto *reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply]() {
        reply->deleteLater();
        // Best effort — don't fail the upload if playlist add fails
    });
}
