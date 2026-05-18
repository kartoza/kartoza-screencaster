/**
 * @file assetgallery.h
 * @brief Horizontal thumbnail gallery for dragging assets onto the canvas.
 */

#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QFileSystemWatcher>

/**
 * @class AssetGallery
 * @brief A horizontal thumbnail strip showing images from the assets directory.
 *
 * Displays small previews of all image files found in the configured assets
 * directory. Users can drag thumbnails onto the canvas to add them as logo
 * overlay items.
 */
class AssetGallery : public QWidget {
    Q_OBJECT

public:
    explicit AssetGallery(QWidget *parent = nullptr);

    /** @brief Refresh thumbnails from the current assets directory. */
    void refresh();

signals:
    /** @brief Emitted when a user drags an asset onto the canvas area. */
    void assetDropped(const QString &filePath);

private:
    QScrollArea *m_scrollArea;
    QWidget *m_container;
    QHBoxLayout *m_layout;
    QFileSystemWatcher *m_watcher;
    QString m_currentDir;

    static constexpr int THUMB_SIZE = 48;
};
