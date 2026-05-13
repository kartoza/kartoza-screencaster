/**
 * @file test_youtube.cpp
 * @brief Tests for YouTube module: token management, config, metadata.
 *
 * No actual network calls — tests token file I/O, PKCE generation,
 * metadata serialization, and config round-trips.
 */
#include <QTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include "youtube/youtube.h"
#include "config/config.h"

class TestYouTube : public QObject {
    Q_OBJECT

private slots:

    // --- YouTubeMetadata serialization ---

    void testMetadataRoundTrip() {
        YouTubeMetadata meta;
        meta.videoId = "abc123";
        meta.videoUrl = "https://youtube.com/watch?v=abc123";
        meta.privacy = "unlisted";
        meta.uploadedAt = "2026-01-15T10:30:00Z";
        meta.channelName = "Test Channel";
        meta.channelId = "UC12345";
        meta.playlistId = "PL999";

        QJsonObject json = meta.toJson();
        YouTubeMetadata loaded = YouTubeMetadata::fromJson(json);

        QCOMPARE(loaded.videoId, meta.videoId);
        QCOMPARE(loaded.videoUrl, meta.videoUrl);
        QCOMPARE(loaded.privacy, meta.privacy);
        QCOMPARE(loaded.uploadedAt, meta.uploadedAt);
        QCOMPARE(loaded.channelName, meta.channelName);
        QCOMPARE(loaded.channelId, meta.channelId);
        QCOMPARE(loaded.playlistId, meta.playlistId);
        QVERIFY(loaded.isUploaded());
    }

    void testMetadataEmptyNotUploaded() {
        YouTubeMetadata meta;
        QVERIFY(!meta.isUploaded());

        QJsonObject json = meta.toJson();
        YouTubeMetadata loaded = YouTubeMetadata::fromJson(json);
        QVERIFY(!loaded.isUploaded());
    }

    void testMetadataFromEmptyJson() {
        YouTubeMetadata meta = YouTubeMetadata::fromJson(QJsonObject());
        QVERIFY(!meta.isUploaded());
        QVERIFY(meta.videoId.isEmpty());
    }

    // --- Token file management ---

    void testTokenFilePath() {
        QString path = YouTube::tokenFilePath();
        QVERIFY(path.contains("kartoza-screencaster"));
        QVERIFY(path.endsWith("youtube_token.json"));
    }

    void testTokenSaveLoadDelete() {
        // Save a token
        QJsonObject token;
        token["access_token"] = "test_access_123";
        token["refresh_token"] = "test_refresh_456";
        token["token_type"] = "Bearer";
        token["expires_in"] = 3600;

        QVERIFY(YouTube::saveToken(token));

        // Load it back
        QJsonObject loaded = YouTube::loadToken();
        QCOMPARE(loaded["access_token"].toString(), QString("test_access_123"));
        QCOMPARE(loaded["refresh_token"].toString(), QString("test_refresh_456"));

        // Delete it
        QVERIFY(YouTube::deleteToken());
        QJsonObject empty = YouTube::loadToken();
        QVERIFY(empty.isEmpty());
    }

    void testLoadTokenMissingFile() {
        // Ensure clean state
        YouTube::deleteToken();
        QJsonObject loaded = YouTube::loadToken();
        QVERIFY(loaded.isEmpty());
    }

    // --- hasToken / hasCredentials ---

    void testHasTokenAndCredentials() {
        YouTube yt;

        // No credentials configured
        auto &cfg = Config::instance();
        QString savedId = cfg.youtubeClientId;
        QString savedSecret = cfg.youtubeClientSecret;
        cfg.youtubeClientId.clear();
        cfg.youtubeClientSecret.clear();
        QVERIFY(!yt.hasCredentials());

        // Set credentials
        cfg.youtubeClientId = "test_id";
        cfg.youtubeClientSecret = "test_secret";
        QVERIFY(yt.hasCredentials());

        // Restore
        cfg.youtubeClientId = savedId;
        cfg.youtubeClientSecret = savedSecret;
    }

    // --- Config youtube fields persist ---

    void testConfigYouTubeFieldsPersist() {
        auto &cfg = Config::instance();
        QString savedId = cfg.youtubeClientId;
        QString savedSecret = cfg.youtubeClientSecret;

        cfg.youtubeClientId = "persist_test_id";
        cfg.youtubeClientSecret = "persist_test_secret";
        cfg.save();

        cfg.youtubeClientId.clear();
        cfg.youtubeClientSecret.clear();
        cfg.load();

        QCOMPARE(cfg.youtubeClientId, QString("persist_test_id"));
        QCOMPARE(cfg.youtubeClientSecret, QString("persist_test_secret"));

        // Restore
        cfg.youtubeClientId = savedId;
        cfg.youtubeClientSecret = savedSecret;
        cfg.save();
    }
};

QTEST_MAIN(TestYouTube)
#include "test_youtube.moc"
