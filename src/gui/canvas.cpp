#include "gui/canvas.h"
#include "config/config.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QDir>
#include <QFileInfo>
#include <QThreadPool>
#include <QThreadPool>
#include <cmath>
#ifndef Q_OS_WIN
#include <signal.h>
#endif

Canvas::Canvas(QWidget *parent) : QWidget(parent) {
    qDebug() << "Canvas::Canvas starting";
    setMinimumSize(400, 225);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet("background: #11111b; border-radius: 8px;");

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(2000);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        // Load pending screenshot
        m_mutex.lock();
        QString path = m_pendingScreenPath;
        m_pendingScreenPath.clear();
        m_mutex.unlock();

        if (!path.isEmpty()) {
            QPixmap pix(path);
            QFile::remove(path);
            if (!pix.isNull()) m_screenPixmap = pix;
        }

        // Check webcam frames — use index-based loop (safe for QList modification)
        for (int i = 0; i < m_items.size(); i++) {
            if (m_items[i].type == 1 && m_items[i].webcamNewFrame) {
                m_mutex.lock();
                if (m_items[i].webcamBuf.size() >= WC_FRAME_SIZE) {
                    QImage img((const uchar*)m_items[i].webcamBuf.constData(), WC_W, WC_H, WC_W*3, QImage::Format_RGB888);
                    m_items[i].webcamPixmap = QPixmap::fromImage(img);
                }
                m_items[i].webcamNewFrame = false;
                m_mutex.unlock();
            }
        }

        update();

        // Capture screen in background
        QThreadPool::globalInstance()->start([this]() { captureScreen(); });
    });
    m_refreshTimer->start();
    m_lastFrameRect = frameRect();
}

Canvas::~Canvas() {
    for (int i = 0; i < m_items.size(); i++) {
        stopWebcamCapture(i);
        if (m_items[i].movie) { m_items[i].movie->stop(); delete m_items[i].movie; }
    }
}

void Canvas::setMonitor(const MonitorInfo &mon) {
    m_monitor = mon;
    m_monitorName = mon.name;

    bool hasScreen = false;
    for (const auto &item : m_items) { if (item.type == 0) { hasScreen = true; break; } }
    if (!hasScreen) {
        QString desc = mon.description.isEmpty() ? mon.name : mon.description;
        CanvasItem item;
        item.type = 0; item.label = "Screen: " + desc;
        item.x = m_cw/2; item.y = m_ch/2; item.w = m_cw; item.h = m_ch;
        m_items.prepend(item);
        emit itemsChanged();
    }

    QThreadPool::globalInstance()->start([this]() { captureScreen(); });
    update();
}

void Canvas::setTitle(const QString &text) {
    m_title = text;
    for (auto &item : m_items) {
        if (item.type == 3) { item.label = text; update(); return; }
    }
    if (!text.isEmpty()) {
        CanvasItem item;
        item.type = 3; item.label = text;
        item.x = m_cw/2; item.y = m_ch - 25; item.w = 200; item.h = 20;
        m_items.append(item);
        emit itemsChanged();
        update();
    }
}

void Canvas::setMode(int mode) {
    QRect oldFrame = frameRect();
    m_mode = mode;
    QRect newFrame = frameRect();

    // Rescale items to new frame
    if (oldFrame.isValid() && newFrame.isValid() &&
        oldFrame.width() > 0 && oldFrame.height() > 0) {
        for (auto &item : m_items) {
            if (item.type == 0) continue;
            double relX = double(item.x - oldFrame.x()) / oldFrame.width();
            double relY = double(item.y - oldFrame.y()) / oldFrame.height();
            double relW = double(item.w) / oldFrame.width();
            double relH = double(item.h) / oldFrame.height();
            item.x = newFrame.x() + int(relX * newFrame.width());
            item.y = newFrame.y() + int(relY * newFrame.height());
            item.w = std::max(10, int(relW * newFrame.width()));
            if (item.type == 1) {
                item.h = (item.shape == 0) ? item.w : item.w * 3 / 4;
            } else {
                item.h = std::max(10, int(relH * newFrame.height()));
            }
        }
    }
    m_lastFrameRect = newFrame;
    update();
}

void Canvas::addWebcam(const QString &device, const QString &name, int shape) {
    int count = 0;
    for (const auto &item : m_items) if (item.type == 1) count++;
    int r = 30;

    CanvasItem item;
    item.type = 1; item.label = name; item.device = device; item.shape = shape;
    QRect fr = frameRect();
    int sz = fr.width() / 5; // 1/5 of frame width
    item.w = sz;
    item.h = (shape == 0) ? sz : sz * 3 / 4; // round=square, rect/square=4:3
    // Position in bottom-right of frame
    item.x = fr.right() - sz/2 - 10 - count*(sz+10);
    item.y = fr.bottom() - item.h/2 - 10;
    item.webcamBuf.resize(WC_FRAME_SIZE);

    m_items.append(item);
    int idx = m_items.size() - 1;
    startWebcamCapture(idx);
    emit itemsChanged();
    update();
}

void Canvas::addLogo(const QString &filePath) {
    QPixmap pix(filePath);
    if (pix.isNull()) return;

    int logoCount = 0;
    for (const auto &item : m_items) if (item.type == 2) logoCount++;

    int w = std::max(40, m_cw / 6);
    int h = std::max(15, w * pix.height() / pix.width());
    int x = w/2 + 10 + logoCount*(w+10);
    int y = h/2 + 10;
    if (x + w/2 > m_cw) { x = w/2 + 10; y = h + 20 + h/2; }

    CanvasItem item;
    item.type = 2; item.label = QFileInfo(filePath).fileName();
    item.x = x; item.y = y; item.w = w; item.h = h;
    item.pixmap = pix; item.filePath = filePath;

    // Detect animated GIF
    if (filePath.toLower().endsWith(".gif")) {
        auto *movie = new QMovie(filePath, QByteArray(), this);
        if (movie->frameCount() > 1) {
            item.isGif = true;
            item.movie = movie;
            QString logoFile = filePath; // capture for lambda
            connect(movie, &QMovie::frameChanged, this, [this, logoFile](int) {
                // Find item by filePath (index may have changed)
                for (auto &it : m_items) {
                    if (it.filePath == logoFile && it.movie) {
                        it.pixmap = it.movie->currentPixmap();
                        break;
                    }
                }
            });
            movie->start();
        } else {
            delete movie;
        }
    }

    m_items.append(item);
    emit itemsChanged();
    update();
}

void Canvas::removeItem(int index) {
    if (index < 0 || index >= m_items.size()) return;
    stopWebcamCapture(index);
    if (m_items[index].movie) {
        m_items[index].movie->disconnect();
        m_items[index].movie->stop();
        delete m_items[index].movie;
        m_items[index].movie = nullptr;
    }
    m_items.removeAt(index);
    if (m_selected >= m_items.size()) m_selected = -1;
    emit itemsChanged();
    update();
}

void Canvas::clearAll() {
    for (int i = 0; i < m_items.size(); i++) {
        stopWebcamCapture(i);
        if (m_items[i].movie) {
            m_items[i].movie->disconnect(); // prevent use-after-free from queued signals
            m_items[i].movie->stop();
            delete m_items[i].movie;
            m_items[i].movie = nullptr;
        }
    }
    m_items.clear();
    m_selected = -1;
    emit itemsChanged();
    update();
}

void Canvas::stopAllWebcamPreviews() {
    for (int i = 0; i < m_items.size(); i++) {
        if (m_items[i].type == 1) stopWebcamCapture(i);
    }
}

void Canvas::startAllWebcamPreviews() {
    for (int i = 0; i < m_items.size(); i++) {
        if (m_items[i].type == 1 && !m_items[i].webcamProc) startWebcamCapture(i);
    }
}

void Canvas::suspendPreviews() {
    stopAllWebcamPreviews();
    if (m_refreshTimer) m_refreshTimer->stop();
}

void Canvas::resumePreviews() {
    startAllWebcamPreviews();
    if (m_refreshTimer) m_refreshTimer->start();
}

void Canvas::setSelectedItem(int index) {
    m_selected = index;
    update();
}

void Canvas::swapItems(int i, int j) {
    if (i < 0 || j < 0 || i >= m_items.size() || j >= m_items.size()) return;
    m_items.swapItemsAt(i, j);
    update();
}

bool Canvas::hasWebcams() const {
    for (const auto &item : m_items) if (item.type == 1 && item.visible) return true;
    return false;
}

QString Canvas::firstWebcamDevice() const {
    for (const auto &item : m_items) {
        if (item.type == 1 && item.visible && !item.device.isEmpty())
            return item.device;
    }
    return {};
}

QStringList Canvas::logoFilePaths() const {
    QStringList paths;
    for (const auto &item : m_items) {
        if (item.type == 2 && item.visible && !item.filePath.isEmpty())
            paths.append(item.filePath);
    }
    return paths;
}

QString Canvas::itemLabel(int index) const {
    if (index < 0 || index >= m_items.size()) return {};
    return m_items[index].label;
}

QList<Canvas::ItemExport> Canvas::exportItems() const {
    QList<ItemExport> result;
    for (const auto &item : m_items) {
        ItemExport e;
        e.type = item.type; e.label = item.label;
        e.x = item.x; e.y = item.y; e.w = item.w; e.h = item.h;
        e.shape = item.shape; e.filePath = item.filePath; e.device = item.device;
        e.gifLoop = item.gifLoop; e.gifLoopMax = item.gifLoopMax;
        result.append(e);
    }
    return result;
}

void Canvas::importItem(const ItemExport &e) {
    switch (e.type) {
    case 0: { // screen
        CanvasItem item;
        item.type = 0; item.label = e.label;
        item.x = e.x; item.y = e.y; item.w = e.w; item.h = e.h;
        m_items.append(item);
        break;
    }
    case 1: { // webcam
        addWebcam(e.device, e.label, e.shape);
        if (!m_items.isEmpty()) {
            int idx = m_items.size() - 1;
            m_items[idx].x = e.x; m_items[idx].y = e.y;
            m_items[idx].w = e.w; m_items[idx].h = e.h;
        }
        break;
    }
    case 2: { // logo
        if (!e.filePath.isEmpty() && QFile::exists(e.filePath)) {
            addLogo(e.filePath);
            auto &last = m_items.last();
            last.x = e.x; last.y = e.y; last.w = e.w; last.h = e.h;
            last.gifLoop = e.gifLoop; last.gifLoopMax = e.gifLoopMax;
        }
        break;
    }
    case 3: { // title
        m_title = e.label;
        CanvasItem item;
        item.type = 3; item.label = e.label;
        item.x = e.x; item.y = e.y; item.w = e.w; item.h = e.h;
        m_items.append(item);
        break;
    }
    }
}

QString Canvas::modeString() const {
    switch (m_mode) {
    case 0: return "landscape";
    case 1: return "vertical";
    case 2: return "left_split";
    case 3: return "right_split";
    }
    return "landscape";
}

// === Webcam live capture ===

void Canvas::startWebcamCapture(int itemIdx) {
    if (itemIdx < 0 || itemIdx >= m_items.size()) return;
    if (m_items[itemIdx].webcamProc) return;

    QString device = m_items[itemIdx].device;
    m_items[itemIdx].webcamProc = new QProcess(this);

    QStringList args = {"-f", "v4l2", "-framerate", QString::number(WC_FPS),
                        "-i", "/dev/" + device,
                        "-vf", QString("scale=%1:%2").arg(WC_W).arg(WC_H),
                        "-f", "rawvideo", "-pix_fmt", "rgb24", "-an", "pipe:1"};

    auto *accum = new QByteArray();
    QProcess *proc = m_items[itemIdx].webcamProc;

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, device, accum]() {
        // Find item by device name (safe even if items reordered)
        int idx = -1;
        for (int i = 0; i < m_items.size(); i++) {
            if (m_items[i].device == device && m_items[i].type == 1) { idx = i; break; }
        }
        if (idx < 0 || !m_items[idx].webcamProc) return;

        accum->append(m_items[idx].webcamProc->readAllStandardOutput());
        while (accum->size() >= WC_FRAME_SIZE) {
            m_mutex.lock();
            if (m_items[idx].webcamBuf.size() >= WC_FRAME_SIZE) {
                memcpy(m_items[idx].webcamBuf.data(), accum->constData(), WC_FRAME_SIZE);
                m_items[idx].webcamNewFrame = true;
            }
            m_mutex.unlock();
            accum->remove(0, WC_FRAME_SIZE);
        }
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [accum]() { delete accum; });

    proc->start("ffmpeg", args);
}

void Canvas::stopWebcamCapture(int idx) {
    if (idx < 0 || idx >= m_items.size()) return;
    auto &item = m_items[idx];
    if (!item.webcamProc) return;
    qint64 pid = item.webcamProc->processId();
#ifndef Q_OS_WIN
    if (pid > 0) ::kill(pid, SIGINT);
#endif
    item.webcamProc->waitForFinished(2000);
    item.webcamProc->deleteLater();
    item.webcamProc = nullptr;
}

// === Screen capture ===

void Canvas::captureScreen() {
    if (m_monitorName.isEmpty()) return;
    QString path = QDir::tempPath() + "/kartoza-canvas-" + m_monitorName + ".png";

    QProcess proc;
#if defined(Q_OS_LINUX)
    // Try grim first (Wayland), fall back to scrot/import (X11)
    QString wayland = qEnvironmentVariable("WAYLAND_DISPLAY");
    if (!wayland.isEmpty()) {
        proc.start("grim", {"-o", m_monitorName, "-t", "png", "-l", "0", path});
    } else {
        // X11: use ffmpeg to grab a single frame
        QString display = qEnvironmentVariable("DISPLAY", ":0");
        proc.start("ffmpeg", {"-y", "-f", "x11grab", "-video_size", "1920x1080",
                              "-i", display, "-frames:v", "1", path});
    }
#elif defined(Q_OS_MACOS)
    proc.start("screencapture", {"-x", path});
#elif defined(Q_OS_WIN)
    // On Windows use ffmpeg gdigrab single frame
    proc.start("ffmpeg", {"-y", "-f", "gdigrab", "-i", "desktop",
                          "-frames:v", "1", path});
#endif

    if (proc.waitForFinished(5000) && proc.exitCode() == 0) {
        m_mutex.lock();
        m_pendingScreenPath = path;
        m_mutex.unlock();
    }
}

QRect Canvas::frameRect() const {
    if (m_mode == 0) {
        return QRect(0, 0, m_cw, m_ch);
    }
    int fh = m_ch - 20, fw = fh * 9 / 16;
    if (fw > m_cw - 20) { fw = m_cw - 20; fh = fw * 16 / 9; }
    int fx = (m_cw - fw) / 2, fy = (m_ch - fh) / 2;
    return QRect(fx, fy, fw, fh);
}

void Canvas::resizeEvent(QResizeEvent *event) {
    m_cw = event->size().width();
    m_ch = event->size().height();

    // Rescale all items from old frame to new frame
    QRect newFrame = frameRect();
    if (m_lastFrameRect.isValid() && newFrame.isValid() &&
        m_lastFrameRect.width() > 0 && m_lastFrameRect.height() > 0) {
        for (auto &item : m_items) {
            if (item.type == 0) continue; // screen fills the frame, skip

            // Convert from old frame-relative to new frame-relative
            double relX = double(item.x - m_lastFrameRect.x()) / m_lastFrameRect.width();
            double relY = double(item.y - m_lastFrameRect.y()) / m_lastFrameRect.height();
            double relW = double(item.w) / m_lastFrameRect.width();
            double relH = double(item.h) / m_lastFrameRect.height();

            item.x = newFrame.x() + int(relX * newFrame.width());
            item.y = newFrame.y() + int(relY * newFrame.height());
            item.w = std::max(10, int(relW * newFrame.width()));
            if (item.type == 1) {
                item.h = (item.shape == 0) ? item.w : item.w * 3 / 4;
            } else {
                item.h = std::max(10, int(relH * newFrame.height()));
            }
        }
    }
    m_lastFrameRect = newFrame;

    QWidget::resizeEvent(event);
}

// === Hit testing ===

bool Canvas::hitTest(const CanvasItem &item, int mx, int my) const {
    if (item.type == 1 && item.shape == 0) { // circle
        int dx = mx - item.x, dy = my - item.y;
        int r = item.w / 2;
        return dx*dx + dy*dy <= r*r;
    }
    return std::abs(mx - item.x) <= item.w/2 && std::abs(my - item.y) <= item.h/2;
}

// === Input events ===

void Canvas::mousePressEvent(QMouseEvent *event) {
    int mx = event->pos().x(), my = event->pos().y();
    int hit = -1;
    for (int i = m_items.size()-1; i >= 0; i--) {
        if (!m_items[i].visible || m_items[i].type == 0) continue;
        if (hitTest(m_items[i], mx, my)) {
            hit = i;
            m_dragging = i;
            m_dragOffX = mx - m_items[i].x;
            m_dragOffY = my - m_items[i].y;
            break;
        }
    }
    m_selected = hit;
    emit selectionChanged(hit);
    setFocus();
    update();
}

void Canvas::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging < 0) return;
    auto &item = m_items[m_dragging];
    int nx = event->pos().x() - m_dragOffX;
    int ny = event->pos().y() - m_dragOffY;
    item.x = std::clamp(nx, item.w/2, m_cw - item.w/2);
    item.y = std::clamp(ny, item.h/2, m_ch - item.h/2);

    // Edge snap
    int margin = 8, snap = 15;
    if (item.x - item.w/2 < snap+margin) item.x = item.w/2 + margin;
    else if (m_cw - item.x - item.w/2 < snap+margin) item.x = m_cw - item.w/2 - margin;
    if (item.y - item.h/2 < snap+margin) item.y = item.h/2 + margin;
    else if (m_ch - item.y - item.h/2 < snap+margin) item.y = m_ch - item.h/2 - margin;

    update();
}

void Canvas::mouseReleaseEvent(QMouseEvent *) {
    if (m_dragging >= 0) {
        m_dragging = -1;
        emit itemsChanged();
    }
}

void Canvas::wheelEvent(QWheelEvent *event) {
    int mx = event->position().toPoint().x();
    int my = event->position().toPoint().y();
    for (int i = m_items.size()-1; i >= 0; i--) {
        auto &item = m_items[i];
        if (!item.visible || item.type == 0) continue;
        if (hitTest(item, mx, my)) {
            int delta = event->angleDelta().y() > 0 ? 5 : -5;
            item.w = std::max(20, item.w + delta);
            if (item.type == 1) {
                // Webcam: maintain aspect ratio
                if (item.shape == 0) item.h = item.w; // round: square
                else item.h = item.w * 3 / 4; // 4:3 aspect
            } else {
                item.h = std::max(15, item.h + delta);
            }
            emit itemsChanged();
            update();
            break;
        }
    }
}

void Canvas::keyPressEvent(QKeyEvent *event) {
    if (m_selected >= 0 && m_selected < m_items.size() && m_items[m_selected].type != 0) {
        auto &item = m_items[m_selected];
        switch (event->key()) {
        case Qt::Key_Left: item.x--; update(); return;
        case Qt::Key_Right: item.x++; update(); return;
        case Qt::Key_Up: item.y--; update(); return;
        case Qt::Key_Down: item.y++; update(); return;
        case Qt::Key_Delete:
            removeItem(m_selected);
            m_selected = -1;
            emit selectionChanged(-1);
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

// === Painting ===

void Canvas::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 17, 27));
    drawScreen(painter);

    // Draw items (skip screen)
    for (int i = 0; i < m_items.size(); i++) {
        const auto &item = m_items[i];
        if (!item.visible || item.type == 0) continue;
        bool isDragging = (i == m_dragging);
        bool isSelected = (i == m_selected);

        if (item.type == 2) { // Logo
            if (!item.pixmap.isNull()) {
                painter.drawPixmap(QRect(item.x-item.w/2, item.y-item.h/2, item.w, item.h), item.pixmap);
            }
            if (isDragging || isSelected) {
                painter.setPen(isSelected ? QColor(249, 226, 175) : QColor(137, 180, 250));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(item.x-item.w/2, item.y-item.h/2, item.w, item.h);
            }
        } else if (item.type == 1) { // Webcam
            QPen pen(isDragging ? QColor(137, 180, 250) : QColor(205, 214, 244));
            painter.setPen(pen);

            bool hasFrame = !item.webcamPixmap.isNull();
            int r = item.w / 2;

            if (item.shape == 0) { // round
                if (hasFrame) {
                    painter.save();
                    QPainterPath clipPath;
                    clipPath.addEllipse(item.x-r, item.y-r, item.w, item.h);
                    painter.setClipPath(clipPath);
                    painter.drawPixmap(QRect(item.x-r, item.y-r, item.w, item.h), item.webcamPixmap);
                    painter.restore();
                } else {
                    painter.setBrush(QColor(166, 227, 161));
                }
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(item.x-r, item.y-r, item.w, item.h);
            } else { // square or rect
                QRect rect(item.x-item.w/2, item.y-item.h/2, item.w, item.h);
                if (hasFrame) {
                    painter.drawPixmap(rect, item.webcamPixmap);
                } else {
                    painter.setBrush(QColor(166, 227, 161));
                    painter.drawRect(rect);
                }
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(rect);
            }

            // Label
            painter.setPen(QColor(205, 214, 244));
            painter.drawText(QPoint(item.x - item.w/3, item.y + item.h/2 + 12), item.label.left(10));

            if (isSelected) {
                painter.setPen(QColor(249, 226, 175));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(item.x-item.w/2-3, item.y-item.h/2-3, item.w+6, item.h+6);
            }
        } else if (item.type == 3) { // Title
            painter.save();
            int fontSize = std::max(6, item.h * 2 / 3);
            painter.setFont(QFont("Sans", fontSize));
            painter.setPen(isDragging ? QColor(137, 180, 250) : QColor(m_titleColor));
            painter.drawText(QPoint(item.x - item.w/2, item.y + item.h/4), item.label);
            if (isDragging || isSelected) {
                painter.setPen(isSelected ? QColor(249, 226, 175) : QColor(137, 180, 250));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(item.x-item.w/2-2, item.y-item.h/2-2, item.w+4, item.h+4);
            }
            painter.restore();
        }
    }

    // Mode label
    painter.setPen(QColor(108, 112, 134));
    QStringList names = {"Landscape 16:9", "Vertical 9:16", "9:16 (Left Split)", "9:16 (Right Split)"};
    painter.drawText(QPoint(5, m_ch - 5), names.value(m_mode, ""));
}

void Canvas::drawScreen(QPainter &painter) {
    bool hasScreen = !m_screenPixmap.isNull();

    if (m_mode == 0) {
        if (hasScreen) painter.drawPixmap(rect(), m_screenPixmap);
        else {
            painter.fillRect(rect(), QColor(30, 30, 46));
            painter.setPen(QColor(108, 112, 134));
            painter.drawText(QPoint(m_cw/2-20, m_ch/2), "Screen");
        }
    } else {
        int fh = m_ch - 20, fw = fh * 9 / 16;
        if (fw > m_cw - 20) { fw = m_cw - 20; fh = fw * 16 / 9; }
        int fx = (m_cw - fw) / 2, fy = (m_ch - fh) / 2;

        // Dim outside frame
        painter.setOpacity(0.6);
        painter.fillRect(QRect(0, 0, fx, m_ch), Qt::black);
        painter.fillRect(QRect(fx+fw, 0, m_cw-fx-fw, m_ch), Qt::black);
        painter.fillRect(QRect(fx, 0, fw, fy), Qt::black);
        painter.fillRect(QRect(fx, fy+fh, fw, m_ch-fy-fh), Qt::black);
        painter.setOpacity(1.0);

        int screenH = (m_mode >= 2) ? std::min(fw * 9 / 8, fh) : std::min(fw * 9 / 16, fh);

        if (hasScreen) {
            QRect target(fx, fy, fw, screenH);
            if (m_mode == 2) { // left split
                QRect src(0, 0, m_screenPixmap.width()/2, m_screenPixmap.height());
                painter.drawPixmap(target, m_screenPixmap, src);
            } else if (m_mode == 3) { // right split
                int pw = m_screenPixmap.width();
                painter.drawPixmap(target, m_screenPixmap, QRect(pw/2, 0, pw/2, m_screenPixmap.height()));
            } else {
                painter.drawPixmap(target, m_screenPixmap);
            }
        }

        if (screenH < fh)
            painter.fillRect(QRect(fx, fy+screenH, fw, fh-screenH), Qt::white);

        painter.setPen(QColor(137, 180, 250));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(fx, fy, fw, fh);

        QStringList modeNames = {"Landscape 16:9", "Vertical 9:16", "9:16 (Left Split)", "9:16 (Right Split)"};
        painter.drawText(QPoint(fx+5, fy+fh+14), modeNames.value(m_mode, ""));
    }

    painter.setPen(QColor(69, 71, 90));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(0, 0, m_cw-1, m_ch-1);
}
