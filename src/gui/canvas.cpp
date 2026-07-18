#include "gui/canvas.h"
#include "config/config.h"
#include "monitor/monitor.h"
#include "platform/platform.h"
#ifdef HAS_DBUS
#include "portal/portal.h"
#endif
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDir>
#include <QFileInfo>
#include <QScreen>
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
    setAcceptDrops(true);
    setStyleSheet("background: #0f0f20; border-radius: 8px;");

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

#ifdef HAS_DBUS
    // GNOME/KDE preview path — Portal::requestScreenshot() returns
    // immediately and emits one of these signals when the portal
    // eventually responds (which may be many seconds after the first
    // call while the user clicks through the permission dialog). We
    // route the result onto the same m_pendingScreenPath the timer
    // already drains, so the rendering side is identical to the
    // wlroots/X11/macOS paths.
    connect(&Portal::instance(), &Portal::screenshotReady, this,
            [this](const QString &shot) {
                QPixmap pix(shot);
                QFile::remove(shot);
                if (pix.isNull() || m_monitorName.isEmpty()) return;
                QPixmap scaled = pix.scaled(960, 540, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
                QString path = QDir::tempPath() + "/kartoza-canvas-"
                               + m_monitorName + ".png";
                scaled.save(path, "PNG");
                m_mutex.lock();
                m_pendingScreenPath = path;
                m_mutex.unlock();
            });
    connect(&Portal::instance(), &Portal::screenshotFailed, this,
            [](const QString &reason) {
                // qWarning so the message lands in journalctl --user by
                // default — preview failures from .desktop launches are
                // otherwise invisible (no stdout, qDebug filtered out).
                qWarning() << "Portal screenshot failed:" << reason;
            });
#endif
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
    for (auto &item : m_items) {
        if (item.type == 0) {
            hasScreen = true;
            QString desc = mon.description.isEmpty() ? mon.name : mon.description;
            item.label = "Screen: " + desc;
            break;
        }
    }
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
            if (item.type == 0) {
                // Reset screen to centered when mode changes
                item.x = m_cw/2;
                item.y = m_ch/2;
                item.w = m_cw;
                item.h = m_ch;
                continue;
            }
            double relX = double(item.x - oldFrame.x()) / oldFrame.width();
            double relY = double(item.y - oldFrame.y()) / oldFrame.height();
            double relW = double(item.w) / oldFrame.width();
            double relH = double(item.h) / oldFrame.height();
            item.x = newFrame.x() + int(relX * newFrame.width());
            item.y = newFrame.y() + int(relY * newFrame.height());
            item.w = std::max(10, int(relW * newFrame.width()));
            if (item.type == 1) {
                item.h = (item.shape == 0) ? item.w : item.w * 3 / 4;
            } else if (item.type == 2 && !item.pixmap.isNull() && item.pixmap.width() > 0) {
                item.h = std::max(10, item.w * item.pixmap.height() / item.pixmap.width());
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
    // Only open the capture device when the window is actually on screen.
    // Why: addWebcam runs during state restoration at startup — before the
    // MainWindow has been shown — and the app launches into the tray. Starting
    // ffmpeg here would hold the webcam open while the user thinks the app is
    // idle. MainWindow::showEvent → RecordPage::resumePreviews →
    // Canvas::resumePreviews will start any pending captures once visible.
    if (isVisible()) startWebcamCapture(idx);
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

void Canvas::addSound(const QString &filePath, bool isEnd) {
    int soundType = isEnd ? 5 : 4;
    QString label = QFileInfo(filePath).baseName() + (isEnd ? " (End)" : " (Start)");
    // Replace existing sound of this type
    for (int i = 0; i < m_items.size(); i++) {
        if (m_items[i].type == soundType) {
            m_items[i].filePath = filePath;
            m_items[i].label = label;
            emit itemsChanged();
            update();
            return;
        }
    }
    CanvasItem item;
    item.type = soundType;
    item.label = label;
    item.filePath = filePath;
    item.x = 0; item.y = 0; item.w = 0; item.h = 0;
    item.visible = true;
    m_items.append(item);
    emit itemsChanged();
    update();
}

void Canvas::updateScreenItem(const ItemExport &e) {
    for (auto &item : m_items) {
        if (item.type == 0) {
            item.x = e.x; item.y = e.y;
            item.w = e.w; item.h = e.h;
            item.cropTop = e.cropTop; item.cropBottom = e.cropBottom;
            item.cropLeft = e.cropLeft; item.cropRight = e.cropRight;
            update();
            return;
        }
    }
}

void Canvas::removeItem(int index) {
    if (index < 0 || index >= m_items.size()) return;
    bool wasScreen = (m_items[index].type == 0);
    stopWebcamCapture(index);
    if (m_items[index].movie) {
        m_items[index].movie->disconnect();
        m_items[index].movie->stop();
        delete m_items[index].movie;
        m_items[index].movie = nullptr;
    }
    m_items.removeAt(index);
    if (wasScreen) {
        m_screenPixmap = QPixmap(); // Clear cached screenshot
        m_monitorName.clear();
    }
    if (m_selected >= m_items.size()) m_selected = -1;
    emit itemsChanged();
    update();
}

void Canvas::setItemGifLoop(int index, int gifLoop, int gifLoopMax) {
    if (index < 0 || index >= m_items.size()) return;
    m_items[index].gifLoop = gifLoop;
    m_items[index].gifLoopMax = gifLoopMax;
    emit itemsChanged();
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
    m_suspended = true;
    stopAllWebcamPreviews();
    updateTimerState();
}

void Canvas::resumePreviews() {
    m_suspended = false;
    startAllWebcamPreviews();
    updateTimerState();
}

void Canvas::updateTimerState() {
    if (!m_refreshTimer) return;
    // The slurp/grim capture only runs while the preview is neither suspended
    // by the app (window hidden, tab inactive, recording) nor manually paused.
    bool shouldRun = !m_suspended && !m_userPaused;
    if (shouldRun && !m_refreshTimer->isActive()) m_refreshTimer->start();
    else if (!shouldRun && m_refreshTimer->isActive()) m_refreshTimer->stop();
}

void Canvas::togglePreviewPause() {
    m_userPaused = !m_userPaused;
    updateTimerState();
    emit previewPausedChanged(m_userPaused);
    update();
}

bool Canvas::hasScreenItem() const {
    for (const auto &item : m_items)
        if (item.type == 0) return true;
    return false;
}

QRect Canvas::previewToggleRect() const {
    if (!hasScreenItem()) return {};
    const int r = 26;
    QPoint c = frameRect().center();
    return {c.x() - r, c.y() - r, 2 * r, 2 * r};
}

void Canvas::drawPreviewToggle(QPainter &painter) {
    QRect btn = previewToggleRect();
    if (btn.isEmpty()) return;
    // While running, the toggle only appears on hover to keep the preview clean;
    // while paused it stays visible so the frozen state is always explained.
    if (!m_userPaused && !m_hovered) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Circular button backdrop — more prominent while paused so the frozen
    // preview reads as intentional rather than broken.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(15, 15, 32, m_userPaused ? 200 : 120));
    painter.drawEllipse(btn);
    painter.setPen(QPen(QColor(86, 159, 198), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(btn);

    QPoint c = btn.center();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(232, 232, 236));
    if (m_userPaused) {
        // Paused → show a play triangle (click to resume).
        QPolygon tri;
        tri << QPoint(c.x() - 7, c.y() - 11)
            << QPoint(c.x() - 7, c.y() + 11)
            << QPoint(c.x() + 12, c.y());
        painter.drawPolygon(tri);
    } else {
        // Running → show pause bars (click to pause).
        painter.drawRect(c.x() - 9, c.y() - 11, 6, 22);
        painter.drawRect(c.x() + 3, c.y() - 11, 6, 22);
    }
    painter.restore();
}

void Canvas::setSelectedItem(int index) {
    m_selected = index;
    emit selectionChanged(index);
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
        e.cropTop = item.cropTop; e.cropBottom = item.cropBottom;
        e.cropLeft = item.cropLeft; e.cropRight = item.cropRight;
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
        item.cropTop = e.cropTop; item.cropBottom = e.cropBottom;
        item.cropLeft = e.cropLeft; item.cropRight = e.cropRight;
        m_items.append(item);
        break;
    }
    case 1: { // webcam
        addWebcam(e.device, e.label, e.shape);
        if (!m_items.isEmpty()) {
            int idx = m_items.size() - 1;
            m_items[idx].x = e.x; m_items[idx].y = e.y;
            m_items[idx].w = e.w; m_items[idx].h = e.h;
            m_items[idx].gifLoop = e.gifLoop;
            m_items[idx].gifLoopMax = e.gifLoopMax;
            m_items[idx].cropTop = e.cropTop; m_items[idx].cropBottom = e.cropBottom;
            m_items[idx].cropLeft = e.cropLeft; m_items[idx].cropRight = e.cropRight;
        }
        break;
    }
    case 2: { // logo
        if (!e.filePath.isEmpty() && QFile::exists(e.filePath)) {
            addLogo(e.filePath);
            auto &last = m_items.last();
            last.x = e.x; last.y = e.y; last.w = e.w; last.h = e.h;
            last.gifLoop = e.gifLoop; last.gifLoopMax = e.gifLoopMax;
            last.cropTop = e.cropTop; last.cropBottom = e.cropBottom;
            last.cropLeft = e.cropLeft; last.cropRight = e.cropRight;
        }
        break;
    }
    case 3: { // title
        m_title = e.label;
        CanvasItem item;
        item.type = 3; item.label = e.label;
        item.x = e.x; item.y = e.y; item.w = e.w; item.h = e.h;
        item.cropTop = e.cropTop; item.cropBottom = e.cropBottom;
        item.cropLeft = e.cropLeft; item.cropRight = e.cropRight;
        m_items.append(item);
        break;
    }
    case 4: // start sound
    case 5: { // end sound
        if (!e.filePath.isEmpty() && QFile::exists(e.filePath)) {
            addSound(e.filePath, e.type == 5);
        }
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

#if defined(Q_OS_LINUX)
    if (Platform::isWayland()) {
        if (Platform::supportsWlrCapture()) {
            // wlroots compositors (Hyprland, Sway, COSMIC, …) — use grim.
            QProcess proc;
            proc.start("grim", {"-o", m_monitorName, "-t", "png", "-l", "0", path});
            if (proc.waitForFinished(5000) && proc.exitCode() == 0) {
                m_mutex.lock();
                m_pendingScreenPath = path;
                m_mutex.unlock();
            }
            return;
        }

#ifdef HAS_DBUS
        // GNOME (Mutter) / KDE (KWin) — no wlr-screencopy. Fire an
        // async screenshot request; the Portal singleton emits
        // screenshotReady() when the portal responds (potentially
        // after a long user-interaction delay on first run). The
        // signal is wired in the Canvas constructor and writes the
        // result into m_pendingScreenPath. We must hop to the main
        // thread to touch the D-Bus session bus.
        QMetaObject::invokeMethod(&Portal::instance(),
                                  &Portal::requestScreenshot,
                                  Qt::QueuedConnection);
        return;
#else
        // No D-Bus in this build — preview is unavailable on GNOME/KDE.
        return;
#endif
    }
#endif

    // X11 / macOS / Windows: use Qt's screen grab (works without external tools)
    // QScreen::grabWindow must be called on the main thread, so we use invokeMethod
    QMetaObject::invokeMethod(this, [this, path]() {
        QScreen *targetScreen = nullptr;
        for (auto *screen : QApplication::screens()) {
            if (screen->name() == m_monitorName) {
                targetScreen = screen;
                break;
            }
        }
        if (!targetScreen) targetScreen = QApplication::primaryScreen();
        if (!targetScreen) return;

        QPixmap pix = targetScreen->grabWindow(0);
        if (!pix.isNull()) {
            // Scale down for preview performance
            QPixmap scaled = pix.scaled(960, 540, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            scaled.save(path, "PNG");
            m_mutex.lock();
            m_pendingScreenPath = path;
            m_mutex.unlock();
        }
    }, Qt::QueuedConnection);
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

void Canvas::enterEvent(QEnterEvent *event) {
    QWidget::enterEvent(event);
    m_hovered = true;
    if (hasScreenItem() && !m_userPaused) update();
}

void Canvas::leaveEvent(QEvent *event) {
    QWidget::leaveEvent(event);
    m_hovered = false;
    if (hasScreenItem() && !m_userPaused) update();
}

void Canvas::resizeEvent(QResizeEvent *event) {
    int widgetW = event->size().width();
    int widgetH = event->size().height();

    // Maintain 16:9 aspect ratio — fit largest 16:9 rect inside widget
    int targetW = widgetW;
    int targetH = targetW * 9 / 16;
    if (targetH > widgetH) {
        targetH = widgetH;
        targetW = targetH * 16 / 9;
    }

    m_offsetX = (widgetW - targetW) / 2;
    m_offsetY = (widgetH - targetH) / 2;
    m_cw = targetW;
    m_ch = targetH;

    // Rescale all items from old frame to new frame
    QRect newFrame = frameRect();
    if (m_lastFrameRect.isValid() && newFrame.isValid() &&
        m_lastFrameRect.width() > 0 && m_lastFrameRect.height() > 0) {
        for (auto &item : m_items) {
            if (item.type == 0) {
                // Rescale screen item position proportionally
                double relX = double(item.x) / m_lastFrameRect.width();
                double relY = double(item.y) / m_lastFrameRect.height();
                item.x = int(relX * newFrame.width());
                item.y = int(relY * newFrame.height());
                item.w = newFrame.width();
                item.h = newFrame.height();
                continue;
            }

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
            } else if (item.type == 2 && !item.pixmap.isNull() && item.pixmap.width() > 0) {
                item.h = std::max(10, item.w * item.pixmap.height() / item.pixmap.width());
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

int Canvas::hitCropHandle(const CanvasItem &item, int mx, int my) const {
    // Returns: 0=none, 1=top, 2=bottom, 3=left, 4=right
    int left = item.x - item.w/2;
    int top = item.y - item.h/2;
    int right = left + item.w;
    int bottom = top + item.h;
    int hs = CROP_HANDLE_SIZE;

    // Top handle: centered horizontally at item top edge
    if (std::abs(my - (top + item.cropTop)) <= hs &&
        std::abs(mx - item.x) <= hs*2)
        return 1;
    // Bottom handle
    if (std::abs(my - (bottom - item.cropBottom)) <= hs &&
        std::abs(mx - item.x) <= hs*2)
        return 2;
    // Left handle
    if (std::abs(mx - (left + item.cropLeft)) <= hs &&
        std::abs(my - item.y) <= hs*2)
        return 3;
    // Right handle
    if (std::abs(mx - (right - item.cropRight)) <= hs &&
        std::abs(my - item.y) <= hs*2)
        return 4;

    return 0;
}

void Canvas::mousePressEvent(QMouseEvent *event) {
    int mx = event->pos().x() - m_offsetX, my = event->pos().y() - m_offsetY;
    m_cropHandle = 0;
    m_cropItem = -1;
    int hit = -1;

    // Centre pause/continue toggle takes priority over item selection/drag.
    if (event->button() == Qt::LeftButton) {
        QRect toggle = previewToggleRect();
        if (!toggle.isEmpty() && toggle.contains(mx, my)) {
            togglePreviewPause();
            return;
        }
    }

    // Check crop handles on selected item first
    if (m_selected >= 0 && m_selected < m_items.size() && m_items[m_selected].visible) {
        int handle = hitCropHandle(m_items[m_selected], mx, my);
        if (handle > 0) {
            m_cropHandle = handle;
            m_cropItem = m_selected;
            hit = m_selected;
            setFocus();
            update();
            m_selected = hit;
            emit selectionChanged(hit);
            return;
        }
    }

    // First pass: check non-screen items (drawn on top)
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
    // Check sound items (click on their icon area)
    if (hit < 0) {
        for (int i = 0; i < m_items.size(); i++) {
            if ((m_items[i].type == 4 || m_items[i].type == 5) && m_items[i].visible) {
                bool isEnd = (m_items[i].type == 5);
                int iconX = isEnd ? m_cw - 20 : 5;
                int iconY = m_ch - 18;
                if (mx >= iconX && mx <= iconX + 18 && my >= iconY && my <= iconY + 14) {
                    hit = i;
                    break;
                }
            }
        }
    }
    // Last pass: check screen item
    if (hit < 0) {
        for (int i = 0; i < m_items.size(); i++) {
            if (m_items[i].type == 0 && m_items[i].visible) {
                if (hitTest(m_items[i], mx, my)) {
                    hit = i;
                    m_dragging = i;
                    m_dragOffX = mx - m_items[i].x;
                    m_dragOffY = my - m_items[i].y;
                }
                break;
            }
        }
    }
    m_selected = hit;
    emit selectionChanged(hit);
    setFocus();
    update();
}

void Canvas::mouseMoveEvent(QMouseEvent *event) {
    int mx = event->pos().x() - m_offsetX;
    int my = event->pos().y() - m_offsetY;
    bool shiftHeld = event->modifiers() & Qt::ShiftModifier;

    // Handle crop handle dragging
    if (m_cropHandle > 0 && m_cropItem >= 0 && m_cropItem < m_items.size()) {
        auto &item = m_items[m_cropItem];
        int left = item.x - item.w/2;
        int top = item.y - item.h/2;
        int right = left + item.w;
        int bottom = top + item.h;
        int minVisible = 20; // minimum visible area

        switch (m_cropHandle) {
        case 1: // top
            item.cropTop = std::clamp(my - top, 0, item.h - item.cropBottom - minVisible);
            break;
        case 2: // bottom
            item.cropBottom = std::clamp(bottom - my, 0, item.h - item.cropTop - minVisible);
            break;
        case 3: // left
            item.cropLeft = std::clamp(mx - left, 0, item.w - item.cropRight - minVisible);
            break;
        case 4: // right
            item.cropRight = std::clamp(right - mx, 0, item.w - item.cropLeft - minVisible);
            break;
        }
        update();
        return;
    }

    if (m_dragging < 0) {
        // Update cursor for crop handle hover on selected item
        if (m_selected >= 0 && m_selected < m_items.size()) {
            int handle = hitCropHandle(m_items[m_selected], mx, my);
            if (handle == 1 || handle == 2)
                setCursor(Qt::SizeVerCursor);
            else if (handle == 3 || handle == 4)
                setCursor(Qt::SizeHorCursor);
            else
                setCursor(Qt::ArrowCursor);
        }
        return;
    }

    auto &item = m_items[m_dragging];
    int nx = mx - m_dragOffX;
    int ny = my - m_dragOffY;

    if (item.type == 0) {
        item.x = std::clamp(nx, 0, m_cw);
        item.y = std::clamp(ny, 0, m_ch);
    } else {
        item.x = std::clamp(nx, item.w/2, m_cw - item.w/2);
        item.y = std::clamp(ny, item.h/2, m_ch - item.h/2);

        // Edge snap (unless Shift is held)
        if (!shiftHeld) {
            QRect fr = frameRect();
            int snap = 12;
            // Use x+width instead of QRect::right() to avoid off-by-one
            int frRight = fr.x() + fr.width();
            int frBottom = fr.y() + fr.height();
            int frCenterX = fr.x() + fr.width()/2;
            int frCenterY = fr.y() + fr.height()/2;

            // Item edges
            int itemLeft = item.x - item.w/2;
            int itemRight = item.x + item.w/2;
            int itemTop = item.y - item.h/2;
            int itemBottom = item.y + item.h/2;

            // Snap to frame left edge
            if (std::abs(itemLeft - fr.x()) < snap) item.x = fr.x() + item.w/2;
            // Snap to frame right edge
            else if (std::abs(itemRight - frRight) < snap) item.x = frRight - item.w/2;
            // Snap to frame horizontal center
            else if (std::abs(item.x - frCenterX) < snap) item.x = frCenterX;

            // Snap to frame top edge
            if (std::abs(itemTop - fr.y()) < snap) item.y = fr.y() + item.h/2;
            // Snap to frame bottom edge
            else if (std::abs(itemBottom - frBottom) < snap) item.y = frBottom - item.h/2;
            // Snap to frame vertical center
            else if (std::abs(item.y - frCenterY) < snap) item.y = frCenterY;
        }
    }

    update();
}

void Canvas::mouseReleaseEvent(QMouseEvent *) {
    if (m_cropHandle > 0) {
        m_cropHandle = 0;
        m_cropItem = -1;
        setCursor(Qt::ArrowCursor);
        emit itemsChanged();
    }
    if (m_dragging >= 0) {
        m_dragging = -1;
        emit itemsChanged();
    }
}

void Canvas::wheelEvent(QWheelEvent *event) {
    int mx = event->position().toPoint().x() - m_offsetX;
    int my = event->position().toPoint().y() - m_offsetY;

    // Check non-screen items first
    for (int i = m_items.size()-1; i >= 0; i--) {
        auto &item = m_items[i];
        if (!item.visible || item.type == 0) continue;
        if (hitTest(item, mx, my)) {
            int delta = event->angleDelta().y() > 0 ? 5 : -5;
            item.w = std::max(20, item.w + delta);
            if (item.type == 1) {
                if (item.shape == 0) item.h = item.w;
                else item.h = item.w * 3 / 4;
            } else if (item.type == 2 && !item.pixmap.isNull() && item.pixmap.width() > 0) {
                item.h = std::max(10, item.w * item.pixmap.height() / item.pixmap.width());
            } else {
                item.h = std::max(15, item.h + delta);
            }
            emit itemsChanged();
            update();
            return;
        }
    }

    // If no overlay item hit, allow scroll-wheel on the screen item (scale/zoom)
    if (m_selected >= 0 && m_selected < m_items.size() && m_items[m_selected].type == 0) {
        auto &item = m_items[m_selected];
        int delta = event->angleDelta().y() > 0 ? 10 : -10;
        int newW = std::max(m_cw / 2, item.w + delta);
        int newH = newW * 9 / 16;
        item.w = newW;
        item.h = newH;
        emit itemsChanged();
        update();
    }
}

void Canvas::keyPressEvent(QKeyEvent *event) {
    if (m_selected >= 0 && m_selected < m_items.size()) {
        auto &item = m_items[m_selected];
        switch (event->key()) {
        case Qt::Key_Left: item.x--; update(); emit itemsChanged(); return;
        case Qt::Key_Right: item.x++; update(); emit itemsChanged(); return;
        case Qt::Key_Up: item.y--; update(); emit itemsChanged(); return;
        case Qt::Key_Down: item.y++; update(); emit itemsChanged(); return;
        case Qt::Key_Delete:
            if (item.type != 0) { // Don't allow deleting the screen item
                removeItem(m_selected);
                m_selected = -1;
                emit selectionChanged(-1);
            }
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

// === Drag and Drop ===

static bool isAudioFile(const QString &path) {
    QStringList exts = {"wav", "mp3", "ogg", "flac", "aac", "m4a", "opus"};
    return exts.contains(QFileInfo(path).suffix().toLower());
}

static bool isImageFile(const QString &path) {
    QStringList exts = {"png", "jpg", "jpeg", "svg", "gif", "webp", "bmp"};
    return exts.contains(QFileInfo(path).suffix().toLower());
}

static QString dragFilePath(const QMimeData *mime) {
    if (mime->hasUrls() && !mime->urls().isEmpty())
        return mime->urls().first().toLocalFile();
    if (mime->hasText())
        return mime->text();
    return {};
}

QRect Canvas::soundDropTargetRect(bool isEnd) const {
    int tw = 70, th = 30;
    int y = m_ch - th - 4;
    int x = isEnd ? m_cw - tw - 4 : 4;
    return QRect(x, y, tw, th);
}

void Canvas::dragEnterEvent(QDragEnterEvent *event) {
    QString path = dragFilePath(event->mimeData());
    if (path.isEmpty()) return;

    if (isImageFile(path) || isAudioFile(path)) {
        if (isAudioFile(path)) {
            m_showSoundDropTargets = true;
            m_soundDropHover = 0;
            update();
        }
        event->acceptProposedAction();
    }
}

void Canvas::dragMoveEvent(QDragMoveEvent *event) {
    if (!m_showSoundDropTargets) {
        event->acceptProposedAction();
        return;
    }

    int mx = event->position().toPoint().x() - m_offsetX;
    int my = event->position().toPoint().y() - m_offsetY;

    int oldHover = m_soundDropHover;
    m_soundDropHover = 0;
    if (soundDropTargetRect(false).contains(mx, my))
        m_soundDropHover = 1; // start
    else if (soundDropTargetRect(true).contains(mx, my))
        m_soundDropHover = 2; // end

    if (m_soundDropHover != oldHover) update();
    event->acceptProposedAction();
}

void Canvas::dragLeaveEvent(QDragLeaveEvent *) {
    if (m_showSoundDropTargets) {
        m_showSoundDropTargets = false;
        m_soundDropHover = 0;
        update();
    }
}

void Canvas::dropEvent(QDropEvent *event) {
    QString filePath = dragFilePath(event->mimeData());
    if (filePath.isEmpty()) return;

    if (isImageFile(filePath)) {
        addLogo(filePath);
        event->acceptProposedAction();
        return;
    }

    if (isAudioFile(filePath) && m_showSoundDropTargets) {
        int mx = event->position().toPoint().x() - m_offsetX;
        int my = event->position().toPoint().y() - m_offsetY;

        bool accepted = false;
        if (soundDropTargetRect(false).contains(mx, my)) {
            addSound(filePath, false);
            accepted = true;
        } else if (soundDropTargetRect(true).contains(mx, my)) {
            addSound(filePath, true);
            accepted = true;
        }

        m_showSoundDropTargets = false;
        m_soundDropHover = 0;
        update();

        if (accepted) event->acceptProposedAction();
        return;
    }

    m_showSoundDropTargets = false;
    m_soundDropHover = 0;
    update();
}

// === Painting ===

void Canvas::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    // Fill entire widget with background (letterbox/pillarbox margins)
    painter.fillRect(rect(), QColor(15, 15, 32));
    // Translate to the 16:9 canvas area
    painter.translate(m_offsetX, m_offsetY);
    drawScreen(painter);

    // Draw items (skip screen)
    for (int i = 0; i < m_items.size(); i++) {
        const auto &item = m_items[i];
        if (!item.visible || item.type == 0) continue;
        bool isDragging = (i == m_dragging);
        bool isSelected = (i == m_selected);

        // Compute visible rect after cropping
        int visLeft = item.x - item.w/2 + item.cropLeft;
        int visTop = item.y - item.h/2 + item.cropTop;
        int visW = item.w - item.cropLeft - item.cropRight;
        int visH = item.h - item.cropTop - item.cropBottom;
        QRect visRect(visLeft, visTop, visW, visH);

        if (item.type == 2) { // Logo
            if (!item.pixmap.isNull()) {
                // Source rect within the pixmap (proportional crop)
                int pw = item.pixmap.width(), ph = item.pixmap.height();
                int srcL = item.cropLeft * pw / item.w;
                int srcT = item.cropTop * ph / item.h;
                int srcW = visW * pw / item.w;
                int srcH = visH * ph / item.h;
                painter.drawPixmap(visRect, item.pixmap, QRect(srcL, srcT, srcW, srcH));
            }
            if (isDragging || isSelected) {
                painter.setPen(isSelected ? QColor(232, 184, 74) : QColor(86, 159, 198));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(visRect);
            }
        } else if (item.type == 1) { // Webcam
            QPen pen(isDragging ? QColor(86, 159, 198) : QColor(232, 232, 236));
            painter.setPen(pen);

            bool hasFrame = !item.webcamPixmap.isNull();

            if (item.shape == 0) { // round
                // For round webcam, crop reduces the visible circle
                int r = std::min(visW, visH) / 2;
                int cx = visLeft + visW/2, cy = visTop + visH/2;
                if (hasFrame) {
                    painter.save();
                    QPainterPath clipPath;
                    clipPath.addEllipse(cx-r, cy-r, r*2, r*2);
                    painter.setClipPath(clipPath);
                    painter.drawPixmap(QRect(cx-r, cy-r, r*2, r*2), item.webcamPixmap);
                    painter.restore();
                } else {
                    painter.setBrush(QColor(6, 150, 154));
                }
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(cx-r, cy-r, r*2, r*2);
            } else { // square or rect
                if (hasFrame) {
                    // Source crop within webcam frame
                    int pw = item.webcamPixmap.width(), ph = item.webcamPixmap.height();
                    int srcL = item.cropLeft * pw / item.w;
                    int srcT = item.cropTop * ph / item.h;
                    int srcW = visW * pw / item.w;
                    int srcH = visH * ph / item.h;
                    painter.drawPixmap(visRect, item.webcamPixmap, QRect(srcL, srcT, srcW, srcH));
                } else {
                    painter.setBrush(QColor(6, 150, 154));
                    painter.drawRect(visRect);
                }
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(visRect);
            }

            // Label
            painter.setPen(QColor(232, 232, 236));
            painter.drawText(QPoint(visLeft, visTop + visH + 12), item.label.left(10));

            if (isSelected) {
                painter.setPen(QColor(232, 184, 74));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(visRect.adjusted(-3, -3, 3, 3));
            }
        } else if (item.type == 3) { // Title
            painter.save();
            int fontSize = std::max(6, visH * 2 / 3);
            painter.setFont(QFont("Sans", fontSize));
            painter.setPen(isDragging ? QColor(86, 159, 198) : QColor(m_titleColor));
            painter.setClipRect(visRect);
            painter.drawText(QPoint(item.x - item.w/2, item.y + item.h/4), item.label);
            painter.restore();
            if (isDragging || isSelected) {
                painter.setPen(isSelected ? QColor(232, 184, 74) : QColor(86, 159, 198));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(visRect.adjusted(-2, -2, 2, 2));
            }
        }

        // Draw crop handles on the selected item
        if (isSelected) {
            drawCropHandles(painter, item);
        }
    }

    // Sound effect indicators (bottom-left = start, bottom-right = end)
    for (int i = 0; i < m_items.size(); i++) {
        const auto &item = m_items[i];
        if (item.type != 4 && item.type != 5) continue;
        if (!item.visible) continue;

        bool isEnd = (item.type == 5);
        bool isSelected = (i == m_selected);
        int iconX = isEnd ? m_cw - 20 : 5;
        int iconY = m_ch - 18;

        // Speaker icon (simple drawn shape)
        painter.setPen(Qt::NoPen);
        painter.setBrush(isSelected ? QColor(223, 158, 47) : QColor(86, 159, 198));

        // Speaker body
        painter.drawRect(iconX, iconY + 3, 5, 8);
        // Speaker cone
        QPolygon cone;
        cone << QPoint(iconX + 5, iconY + 2) << QPoint(iconX + 10, iconY)
             << QPoint(iconX + 10, iconY + 14) << QPoint(iconX + 5, iconY + 12);
        painter.drawPolygon(cone);

        // Sound waves
        painter.setPen(QPen(isSelected ? QColor(223, 158, 47) : QColor(86, 159, 198), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(iconX + 11, iconY + 2, 4, 10, -60*16, 120*16);

        // Label
        painter.setPen(isSelected ? QColor(223, 158, 47) : QColor(138, 139, 139));
        QFont smallFont("Sans", 7);
        painter.setFont(smallFont);
        QString label = QFileInfo(item.filePath).baseName().left(12);
        int textX = isEnd ? iconX - 4 - painter.fontMetrics().horizontalAdvance(label) : iconX + 18;
        painter.drawText(QPoint(textX, iconY + 10), label);
        painter.setFont(QFont()); // reset
    }

    // Sound drop targets (shown only during audio file drag)
    if (m_showSoundDropTargets) {
        for (int side = 0; side < 2; side++) {
            bool isEnd = (side == 1);
            QRect r = soundDropTargetRect(isEnd);
            bool hovered = (isEnd ? m_soundDropHover == 2 : m_soundDropHover == 1);

            // Background
            painter.setPen(QPen(hovered ? QColor(223, 158, 47) : QColor(86, 159, 198), 2, Qt::DashLine));
            painter.setBrush(QColor(hovered ? 223 : 86, hovered ? 158 : 159, hovered ? 47 : 198, 40));
            painter.drawRoundedRect(r, 4, 4);

            // Speaker icon
            int ix = r.x() + 6, iy = r.y() + 8;
            painter.setPen(Qt::NoPen);
            painter.setBrush(hovered ? QColor(223, 158, 47) : QColor(86, 159, 198));
            painter.drawRect(ix, iy + 2, 4, 6);
            QPolygon cone;
            cone << QPoint(ix + 4, iy + 1) << QPoint(ix + 8, iy - 1)
                 << QPoint(ix + 8, iy + 11) << QPoint(ix + 4, iy + 9);
            painter.drawPolygon(cone);

            // Label
            painter.setPen(hovered ? QColor(223, 158, 47) : QColor(200, 200, 200));
            QFont f("Sans", 8);
            painter.setFont(f);
            QString label = isEnd ? "End" : "Start";
            painter.drawText(QPoint(ix + 14, iy + 8), label);
            painter.setFont(QFont());
        }
    }

    // Mode label
    painter.setPen(QColor(138, 139, 139));
    QStringList names = {"Landscape 16:9", "Vertical 9:16", "9:16 (Left Split)", "9:16 (Right Split)"};
    painter.drawText(QPoint(5, m_ch - 5), names.value(m_mode, ""));

    // Pause/continue toggle over the screen preview (topmost).
    drawPreviewToggle(painter);
}

void Canvas::drawScreen(QPainter &painter) {
    // Check if a screen item actually exists
    bool hasScreenItem = false;
    int screenOffX = 0, screenOffY = 0;
    int screenW = m_cw, screenH = m_ch;
    int sCropT = 0, sCropB = 0, sCropL = 0, sCropR = 0;
    for (const auto &item : m_items) {
        if (item.type == 0) {
            hasScreenItem = true;
            screenOffX = item.x - m_cw/2;
            screenOffY = item.y - m_ch/2;
            screenW = item.w;
            screenH = item.h;
            sCropT = item.cropTop; sCropB = item.cropBottom;
            sCropL = item.cropLeft; sCropR = item.cropRight;
            break;
        }
    }

    // If no screen item in the layer list, just draw empty canvas
    if (!hasScreenItem) {
        painter.fillRect(QRect(0, 0, m_cw, m_ch), QColor(15, 15, 32));
        painter.setPen(QColor(61, 61, 86));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(0, 0, m_cw-1, m_ch-1);
        return;
    }

    bool hasScreen = !m_screenPixmap.isNull();

    QRect canvasRect(0, 0, m_cw, m_ch);
    if (m_mode == 0) {
        int sx = screenOffX + (m_cw - screenW)/2;
        int sy = screenOffY + (m_ch - screenH)/2;
        QRect screenRect(sx + sCropL, sy + sCropT,
                         screenW - sCropL - sCropR, screenH - sCropT - sCropB);
        if (hasScreen) {
            // Crop the source pixmap proportionally
            int pw = m_screenPixmap.width(), ph = m_screenPixmap.height();
            int srcL = sCropL * pw / screenW;
            int srcT = sCropT * ph / screenH;
            int srcW = (screenW - sCropL - sCropR) * pw / screenW;
            int srcH = (screenH - sCropT - sCropB) * ph / screenH;
            painter.drawPixmap(screenRect, m_screenPixmap, QRect(srcL, srcT, srcW, srcH));
        } else {
            painter.fillRect(screenRect, QColor(26, 26, 46));
            painter.setPen(QColor(138, 139, 139));
            painter.drawText(screenRect.center() - QPoint(20, 0), "Screen");
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
            QRect target(fx + screenOffX, fy + screenOffY, fw, screenH);
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
            painter.fillRect(QRect(fx + screenOffX, fy + screenOffY + screenH, fw, fh-screenH), Qt::white);

        painter.setPen(QColor(86, 159, 198));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(fx, fy, fw, fh);

        QStringList modeNames = {"Landscape 16:9", "Vertical 9:16", "9:16 (Left Split)", "9:16 (Right Split)"};
        painter.drawText(QPoint(fx+5, fy+fh+14), modeNames.value(m_mode, ""));
    }

    // Draw selection highlight and crop handles on screen item if selected
    int screenIdx = -1;
    for (int i = 0; i < m_items.size(); i++) {
        if (m_items[i].type == 0) { screenIdx = i; break; }
    }
    if (screenIdx >= 0 && (screenIdx == m_selected || screenIdx == m_dragging)) {
        painter.setPen(QPen(QColor(232, 184, 74), 2, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(0, 0, m_cw-1, m_ch-1);
        if (screenIdx == m_selected)
            drawCropHandles(painter, m_items[screenIdx]);
    }

    painter.setPen(QColor(61, 61, 86));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(0, 0, m_cw-1, m_ch-1);
}

void Canvas::drawCropHandles(QPainter &painter, const CanvasItem &item) {
    int left = item.x - item.w/2;
    int top = item.y - item.h/2;
    int right = left + item.w;
    int bottom = top + item.h;
    int hs = CROP_HANDLE_SIZE;

    QColor handleColor(223, 158, 47); // Kartoza highlight1 #DF9E2F
    QColor cropAreaColor(223, 158, 47, 40);
    painter.setBrush(handleColor);
    painter.setPen(Qt::NoPen);

    // Draw dimmed crop regions
    if (item.cropTop > 0) {
        painter.fillRect(QRect(left, top, item.w, item.cropTop), cropAreaColor);
    }
    if (item.cropBottom > 0) {
        painter.fillRect(QRect(left, bottom - item.cropBottom, item.w, item.cropBottom), cropAreaColor);
    }
    if (item.cropLeft > 0) {
        painter.fillRect(QRect(left, top + item.cropTop, item.cropLeft, item.h - item.cropTop - item.cropBottom), cropAreaColor);
    }
    if (item.cropRight > 0) {
        painter.fillRect(QRect(right - item.cropRight, top + item.cropTop, item.cropRight, item.h - item.cropTop - item.cropBottom), cropAreaColor);
    }

    // Draw crop edge lines
    painter.setPen(QPen(handleColor, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    if (item.cropTop > 0)
        painter.drawLine(left, top + item.cropTop, right, top + item.cropTop);
    if (item.cropBottom > 0)
        painter.drawLine(left, bottom - item.cropBottom, right, bottom - item.cropBottom);
    if (item.cropLeft > 0)
        painter.drawLine(left + item.cropLeft, top, left + item.cropLeft, bottom);
    if (item.cropRight > 0)
        painter.drawLine(right - item.cropRight, top, right - item.cropRight, bottom);

    // Draw handle rectangles
    painter.setPen(Qt::NoPen);
    painter.setBrush(handleColor);
    // Top handle
    painter.drawRect(item.x - hs, top + item.cropTop - hs/2, hs*2, hs);
    // Bottom handle
    painter.drawRect(item.x - hs, bottom - item.cropBottom - hs/2, hs*2, hs);
    // Left handle
    painter.drawRect(left + item.cropLeft - hs/2, item.y - hs, hs, hs*2);
    // Right handle
    painter.drawRect(right - item.cropRight - hs/2, item.y - hs, hs, hs*2);
}
