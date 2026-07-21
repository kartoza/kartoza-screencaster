#include "gui/assetgallery.h"
#include "config/config.h"
#include <QDir>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QFileInfo>
#include <QPainter>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QApplication>

static bool isAudioExt(const QString &path) {
    QStringList exts = {"wav", "mp3", "ogg", "flac", "aac", "m4a", "opus"};
    return exts.contains(QFileInfo(path).suffix().toLower());
}

// Thumbnail label that initiates drag or plays audio on click
class ThumbLabel : public QLabel {
public:
    ThumbLabel(const QString &filePath, QWidget *parent = nullptr)
        : QLabel(parent), m_filePath(filePath), m_isAudio(isAudioExt(filePath)) {
        setCursor(Qt::OpenHandCursor);
        setToolTip(QFileInfo(filePath).fileName());
    }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            m_pressPos = event->pos();
            m_dragging = false;
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (!(event->buttons() & Qt::LeftButton)) return;
        if (m_dragging) return;

        if ((event->pos() - m_pressPos).manhattanLength() > QApplication::startDragDistance()) {
            m_dragging = true;
            auto *drag = new QDrag(this);
            auto *mime = new QMimeData;
            mime->setUrls({QUrl::fromLocalFile(m_filePath)});
            mime->setText(m_filePath);
            drag->setMimeData(mime);
            drag->setPixmap(pixmap().scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            drag->exec(Qt::CopyAction);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && !m_dragging && m_isAudio) {
            playOnce();
        }
        m_dragging = false;
    }

private:
    void playOnce() {
        auto *player = new QMediaPlayer(this);
        auto *output = new QAudioOutput(this);
        player->setAudioOutput(output);
        output->setVolume(1.0);
        player->setSource(QUrl::fromLocalFile(m_filePath));
        connect(player, &QMediaPlayer::playbackStateChanged, player, [player, output](QMediaPlayer::PlaybackState state) {
            if (state == QMediaPlayer::StoppedState) {
                player->deleteLater();
                output->deleteLater();
            }
        });
        player->play();
    }

    QString m_filePath;
    bool m_isAudio;
    QPoint m_pressPos;
    bool m_dragging = false;
};

AssetGallery::AssetGallery(QWidget *parent) : QWidget(parent) {
    auto *outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 4, 0, 0);
    outerLayout->setSpacing(0);

    m_scrollArea = new QScrollArea;
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFixedHeight(THUMB_SIZE + 16);
    m_scrollArea->setStyleSheet(
        "QScrollArea { background: #1a1a2e; border: 1px solid #3d3d56; border-radius: 4px; }"
        "QScrollBar:horizontal { height: 6px; background: #1a1a2e; }"
        "QScrollBar::handle:horizontal { background: #569FC6; border-radius: 3px; min-width: 20px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }");

    m_container = new QWidget;
    m_layout = new QHBoxLayout(m_container);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(6);
    m_layout->addStretch();

    m_scrollArea->setWidget(m_container);
    outerLayout->addWidget(m_scrollArea);

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &AssetGallery::refresh);

    refresh();
}

void AssetGallery::refresh() {
    // Clear existing thumbnails
    while (m_layout->count() > 1) { // keep the stretch
        auto *item = m_layout->takeAt(0);
        if (item->widget()) { delete item->widget(); }
        delete item;
    }

    QString dir = Config::instance().logoDirectory;
    if (dir.isEmpty() || !QDir(dir).exists()) {
        // Show placeholder
        auto *placeholder = new QLabel("Set assets directory in Settings");
        placeholder->setStyleSheet("QLabel { color: #8A8B8B; font-size: 11px; padding: 8px; }");
        m_layout->insertWidget(0, placeholder);
        return;
    }

    // Update watcher
    if (!m_currentDir.isEmpty() && m_currentDir != dir) {
        m_watcher->removePath(m_currentDir);
    }
    if (m_currentDir != dir) {
        m_watcher->addPath(dir);
        m_currentDir = dir;
    }

    QDir assetDir(dir);
    // SVG excluded: FFmpeg has no SVG decoder, so an SVG overlay would break the
    // render. Raster/GIF formats only.
    QStringList imageFilters = {"*.png", "*.jpg", "*.jpeg", "*.gif", "*.webp", "*.bmp"};
    QStringList audioFilters = {"*.wav", "*.mp3", "*.ogg", "*.flac", "*.aac", "*.m4a", "*.opus"};
    QStringList allFilters = imageFilters + audioFilters;
    auto files = assetDir.entryInfoList(allFilters, QDir::Files, QDir::Name);

    QStringList audioExts = {"wav", "mp3", "ogg", "flac", "aac", "m4a", "opus"};

    for (const auto &fi : files) {
        bool isAudio = audioExts.contains(fi.suffix().toLower());
        auto *label = new ThumbLabel(fi.absoluteFilePath(), m_container);
        label->setFixedSize(THUMB_SIZE + 4, THUMB_SIZE + 4);
        label->setAlignment(Qt::AlignCenter);

        if (isAudio) {
            // Draw a speaker icon for audio files
            QPixmap audioPix(THUMB_SIZE, THUMB_SIZE);
            audioPix.fill(Qt::transparent);
            QPainter p(&audioPix);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(86, 159, 198));
            // Speaker body
            p.drawRect(14, 18, 6, 12);
            // Cone
            QPolygon cone;
            cone << QPoint(20, 16) << QPoint(28, 10) << QPoint(28, 38) << QPoint(20, 32);
            p.drawPolygon(cone);
            // Waves
            p.setPen(QPen(QColor(86, 159, 198), 2));
            p.setBrush(Qt::NoBrush);
            p.drawArc(30, 16, 6, 16, -60*16, 120*16);
            p.end();
            label->setPixmap(audioPix);
        } else {
            QPixmap pix(fi.absoluteFilePath());
            if (pix.isNull()) { delete label; continue; }
            QPixmap thumb = pix.scaled(THUMB_SIZE, THUMB_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            label->setPixmap(thumb);
        }

        label->setStyleSheet(
            "QLabel { background: #2d2d44; border: 1px solid #3d3d56; border-radius: 3px; padding: 1px; }"
            "QLabel:hover { border-color: #DF9E2F; }");
        m_layout->insertWidget(m_layout->count() - 1, label); // before stretch
    }
}
