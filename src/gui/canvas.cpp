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
        // Load pending screenshot for the primary screen — unless it is paused,
        // in which case the last frame stays frozen (per-item pause).
        int primary = screenItemIndex();
        bool primaryPaused = primary >= 0 && m_items[primary].paused;
        m_mutex.lock();
        QString path = m_pendingScreenPath;
        m_pendingScreenPath.clear();
        QHash<QString, QString> extraScreens = m_pendingScreens;
        m_pendingScreens.clear();
        m_mutex.unlock();

        bool changed = false;
        if (!path.isEmpty()) {
            QPixmap pix(path);
            QFile::remove(path);
            if (!pix.isNull() && !primaryPaused) { m_screenPixmap = pix; changed = true; }
        }

        // Load newest captures for any additional monitors into their own items.
        for (auto it = extraScreens.constBegin(); it != extraScreens.constEnd(); ++it) {
            QPixmap pix(it.value());
            QFile::remove(it.value());
            if (pix.isNull()) continue;
            for (int i = 0; i < m_items.size(); i++) {
                if (m_items[i].type == 0 && i != primary &&
                    m_items[i].monitorName == it.key() && !m_items[i].paused) {
                    m_items[i].screenPixmap = pix;
                    changed = true;
                }
            }
        }

        // Check webcam frames — use index-based loop (safe for QList modification).
        // A paused webcam keeps its last frame (drops incoming frames).
        for (int i = 0; i < m_items.size(); i++) {
            if (m_items[i].type == 1 && m_items[i].webcamNewFrame) {
                m_mutex.lock();
                if (!m_items[i].paused && m_items[i].webcamBuf.size() >= WC_FRAME_SIZE) {
                    QImage img((const uchar*)m_items[i].webcamBuf.constData(), WC_W, WC_H, WC_W*3, QImage::Format_RGB888);
                    m_items[i].webcamPixmap = QPixmap::fromImage(img);
                    changed = true;
                }
                m_items[i].webcamNewFrame = false;
                m_mutex.unlock();
            }
        }

        // Only repaint when new content actually arrived — avoids a repaint every
        // 2 s on an idle canvas.
        if (changed) update();

        // Capture every (non-paused) monitor in the background. The target list is
        // computed here on the UI thread so the pool task never touches m_items.
        auto targets = screensToCapture();
        if (!targets.isEmpty())
            m_capturePool.start([this, targets]() { captureScreens(targets); });
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
    // Drain in-flight screen-capture tasks first: they run on a background thread
    // and touch this Canvas's members (mutex, pending-capture maps). If one is
    // still running when the members below are destroyed it is a use-after-free
    // (an intermittent SIGSEGV on the capture thread under test). The refresh
    // timer stops firing while we are in the destructor, so no new tasks queue.
    m_capturePool.waitForDone();
    for (int i = 0; i < m_items.size(); i++) {
        stopWebcamCapture(i);
        if (m_items[i].movie) {
            // Disconnect before delete: a queued frameChanged into a half-destroyed
            // Canvas would be a use-after-free (mirrors removeItem/clearAll).
            m_items[i].movie->disconnect();
            m_items[i].movie->stop();
            delete m_items[i].movie;
        }
    }
}

void Canvas::setMonitor(const MonitorInfo &mon) {
    QString desc = mon.description.isEmpty() ? mon.name : mon.description;

    // Already on the canvas? Just refresh its label and re-capture.
    for (auto &item : m_items) {
        if (item.type == 0 && item.monitorName == mon.name) {
            item.label = "Screen: " + desc;
            auto again = screensToCapture();
            if (!again.isEmpty())
                m_capturePool.start([this, again]() { captureScreens(again); });
            update();
            return;
        }
    }

    int existingScreens = 0;
    for (const auto &item : m_items) if (item.type == 0) existingScreens++;

    CanvasItem item;
    item.type = 0; item.label = "Screen: " + desc;
    item.monitorName = mon.name; item.monitor = mon;
    if (existingScreens == 0) {
        // Primary screen: fills the canvas and drives the vertical/split modes.
        // Mirror it into the globals so the tested single-screen capture path is
        // byte-for-byte unchanged.
        item.x = m_cw/2; item.y = m_ch/2; item.w = m_cw; item.h = m_ch;
        m_monitor = mon;
        m_mutex.lock();
        m_monitorName = mon.name;
        m_mutex.unlock();
        m_items.prepend(item);
    } else {
        // Additional monitor: added as a movable/resizable inset the user can
        // arrange (e.g. side-by-side). Captured into its own per-item pixmap and
        // composited on top of the primary screen.
        QRect fr = frameRect();
        int w = std::max(80, fr.width() / 2);
        int h = w * 9 / 16;
        item.w = w; item.h = h;
        item.x = std::clamp(fr.x() + fr.width() - w/2 - 10 - (existingScreens - 1) * 20, w/2, m_cw - w/2);
        item.y = std::clamp(fr.y() + fr.height() - h/2 - 10, h/2, m_ch - h/2);
        m_items.append(item);
    }
    emit itemsChanged();

    auto targets = screensToCapture();
    if (!targets.isEmpty())
        m_capturePool.start([this, targets]() { captureScreens(targets); });
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

int Canvas::addTextBox(const QString &text) {
    CanvasItem item;
    item.type = 3;
    item.label = text.isEmpty() ? "Text" : text;
    // Place near the centre of the frame; sized so the container-derived font is
    // comfortably readable. Users can then resize/drag/crop as with any item.
    QRect fr = frameRect();
    item.w = std::max(120, fr.width() / 3);
    item.h = std::max(24, fr.height() / 8);
    item.x = fr.center().x();
    item.y = fr.center().y();
    item.fontFamily = "Sans";
    item.fontWeight = 400;
    item.textColor = m_titleColor;
    m_items.append(item);
    int idx = m_items.size() - 1;
    m_selected = idx;
    emit itemsChanged();
    emit selectionChanged(idx);
    update();
    return idx;
}

void Canvas::setItemText(int index, const QString &text) {
    if (index < 0 || index >= m_items.size() || m_items[index].type != 3) return;
    m_items[index].label = text;
    emit itemsChanged();
    update();
}

void Canvas::setItemFont(int index, const QString &family) {
    if (index < 0 || index >= m_items.size() || m_items[index].type != 3) return;
    m_items[index].fontFamily = family;
    emit itemsChanged();
    update();
}

void Canvas::setItemFontWeight(int index, int weight) {
    if (index < 0 || index >= m_items.size() || m_items[index].type != 3) return;
    m_items[index].fontWeight = weight;
    emit itemsChanged();
    update();
}

void Canvas::setItemTextColor(int index, const QString &color) {
    if (index < 0 || index >= m_items.size() || m_items[index].type != 3) return;
    m_items[index].textColor = color;
    emit itemsChanged();
    update();
}

int Canvas::screenItemIndex() const {
    for (int i = 0; i < m_items.size(); i++)
        if (m_items[i].type == 0) return i;
    return -1;
}

void Canvas::rescaleItemsToFrame(const QRect &oldFrame, const QRect &newFrame) {
    if (!oldFrame.isValid() || !newFrame.isValid() ||
        oldFrame.width() <= 0 || oldFrame.height() <= 0)
        return;
    for (auto &item : m_items) {
        if (item.type == 0) continue; // screen handled by the caller
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

void Canvas::setMode(int mode) {
    QRect oldFrame = frameRect();
    m_mode = mode;
    QRect newFrame = frameRect();

    if (oldFrame.isValid() && newFrame.isValid() &&
        oldFrame.width() > 0 && oldFrame.height() > 0) {
        // Screen item recentres to the full canvas when the mode changes.
        for (auto &item : m_items) {
            if (item.type == 0) {
                item.x = m_cw/2; item.y = m_ch/2;
                item.w = m_cw; item.h = m_ch;
            }
        }
        rescaleItemsToFrame(oldFrame, newFrame);
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
        // If another screen remains, promote the first one to primary; otherwise
        // clear the primary-screen globals.
        int p = screenItemIndex();
        if (p >= 0) {
            m_screenPixmap = m_items[p].screenPixmap;
            m_monitor = m_items[p].monitor;
            m_mutex.lock();
            m_monitorName = m_items[p].monitorName;
            m_mutex.unlock();
        } else {
            m_screenPixmap = QPixmap(); // Clear cached screenshot
            m_mutex.lock();
            m_monitorName.clear();
            m_mutex.unlock();
        }
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
    // The timer only stops when the whole preview is suspended by the app (window
    // hidden, tab inactive, recording). Per-item pause is handled inside the tick
    // (paused items simply drop their new frames), so one paused element never
    // freezes the others.
    bool shouldRun = !m_suspended;
    if (shouldRun && !m_refreshTimer->isActive()) m_refreshTimer->start();
    else if (!shouldRun && m_refreshTimer->isActive()) m_refreshTimer->stop();
}

void Canvas::toggleItemPause(int index) {
    if (!isLiveItem(index)) return;
    m_items[index].paused = !m_items[index].paused;
    update();
}

bool Canvas::hasScreenItem() const {
    return screenItemIndex() >= 0;
}

bool Canvas::isLiveItem(int index) const {
    if (index < 0 || index >= m_items.size() || !m_items[index].visible) return false;
    return m_items[index].type == 0 || m_items[index].type == 1;
}

int Canvas::liveItemAt(int mx, int my) const {
    // Topmost first: overlays (incl. additional screens/webcams) sit above the
    // primary background screen, which is checked last.
    int primary = screenItemIndex();
    for (int i = m_items.size() - 1; i >= 0; i--) {
        if (i == primary || !isLiveItem(i)) continue;
        if (hitTest(m_items[i], mx, my)) return i;
    }
    if (primary >= 0 && isLiveItem(primary) &&
        frameRect().contains(mx, my)) return primary;
    return -1;
}

QRect Canvas::itemToggleRect(int index) const {
    if (!isLiveItem(index)) return {};
    const int r = 22;
    // Centre the toggle on the item's visible area. The primary screen (which
    // fills the frame) centres on the frame so the button never drifts off-canvas.
    QPoint c;
    if (index == screenItemIndex()) c = frameRect().center();
    else c = QPoint(m_items[index].x, m_items[index].y);
    return {c.x() - r, c.y() - r, 2 * r, 2 * r};
}

void Canvas::drawItemToggle(QPainter &painter, int index) {
    QRect btn = itemToggleRect(index);
    if (btn.isEmpty()) return;
    bool paused = m_items[index].paused;
    // While running, the toggle only appears when hovering that element, keeping
    // the preview clean; while paused it stays visible so the frozen state reads
    // as intentional rather than broken.
    if (!paused && m_hoverItem != index) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(15, 15, 32, paused ? 200 : 120));
    painter.drawEllipse(btn);
    painter.setPen(QPen(QColor(86, 159, 198), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(btn);

    QPoint c = btn.center();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(232, 232, 236));
    if (paused) {
        // Paused → show a play triangle (click to resume this element).
        QPolygon tri;
        tri << QPoint(c.x() - 6, c.y() - 9)
            << QPoint(c.x() - 6, c.y() + 9)
            << QPoint(c.x() + 10, c.y());
        painter.drawPolygon(tri);
    } else {
        // Running → show pause bars (click to pause this element).
        painter.drawRect(c.x() - 8, c.y() - 9, 5, 18);
        painter.drawRect(c.x() + 3, c.y() - 9, 5, 18);
    }
    painter.restore();
}

QStringList Canvas::selectedMonitors() const {
    QStringList names;
    for (const auto &item : m_items)
        if (item.type == 0 && !item.monitorName.isEmpty())
            names << item.monitorName;
    return names;
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

Canvas::ItemExport Canvas::itemExport(int index) const {
    if (index < 0 || index >= m_items.size()) return {};
    const auto &item = m_items[index];
    ItemExport e;
    e.type = item.type; e.label = item.label;
    e.x = item.x; e.y = item.y; e.w = item.w; e.h = item.h;
    e.shape = item.shape; e.filePath = item.filePath; e.device = item.device;
    e.monitorName = item.monitorName;
    e.gifLoop = item.gifLoop; e.gifLoopMax = item.gifLoopMax;
    e.cropTop = item.cropTop; e.cropBottom = item.cropBottom;
    e.cropLeft = item.cropLeft; e.cropRight = item.cropRight;
    e.fontFamily = item.fontFamily; e.fontWeight = item.fontWeight;
    e.textColor = item.textColor;
    return e;
}

QList<Canvas::ItemExport> Canvas::exportItems() const {
    QList<ItemExport> result;
    result.reserve(m_items.size());
    for (int i = 0; i < m_items.size(); i++)
        result.append(itemExport(i));
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
    case 3: { // text box
        m_title = e.label;
        CanvasItem item;
        item.type = 3; item.label = e.label;
        item.x = e.x; item.y = e.y; item.w = e.w; item.h = e.h;
        item.cropTop = e.cropTop; item.cropBottom = e.cropBottom;
        item.cropLeft = e.cropLeft; item.cropRight = e.cropRight;
        if (!e.fontFamily.isEmpty()) item.fontFamily = e.fontFamily;
        item.fontWeight = e.fontWeight;
        item.textColor = e.textColor;
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

    QProcess *proc = m_items[itemIdx].webcamProc;

    // Read from the emitting process directly and accumulate into the item's own
    // buffer — no heap allocation and no manual cross-lambda lifetime to manage.
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, device, proc]() {
        // Find item by device name (safe even if items reordered)
        int idx = -1;
        for (int i = 0; i < m_items.size(); i++) {
            if (m_items[i].device == device && m_items[i].type == 1) { idx = i; break; }
        }
        if (idx < 0 || m_items[idx].webcamProc != proc) return; // stale/restarted

        QByteArray &accum = m_items[idx].webcamAccum;
        accum.append(proc->readAllStandardOutput());
        while (accum.size() >= WC_FRAME_SIZE) {
            m_mutex.lock();
            if (m_items[idx].webcamBuf.size() >= WC_FRAME_SIZE) {
                memcpy(m_items[idx].webcamBuf.data(), accum.constData(), WC_FRAME_SIZE);
                m_items[idx].webcamNewFrame = true;
            }
            m_mutex.unlock();
            accum.remove(0, WC_FRAME_SIZE);
        }
    });

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
    item.webcamAccum.clear(); // next capture starts frame-aligned
}

// === Screen capture ===

QVector<Canvas::ScreenGrab> Canvas::screensToCapture() const {
    // Runs on the UI thread (m_items owner). One entry per non-paused screen
    // layer; the first screen is the primary (mirrored into the globals).
    QVector<ScreenGrab> targets;
    int primary = screenItemIndex();
    for (int i = 0; i < m_items.size(); i++) {
        if (m_items[i].type != 0 || m_items[i].paused) continue;
        QString name = m_items[i].monitorName;
        if (i == primary && name.isEmpty()) name = m_monitorName;
        if (name.isEmpty()) continue;
        targets.append({name, i == primary});
    }
    return targets;
}

void Canvas::captureScreens(const QVector<ScreenGrab> &targets) {
    // Runs on a thread-pool thread. The target list was snapshotted on the UI
    // thread, so this never touches m_items; results are handed back via the
    // mutex-guarded pending-path members and drained by the refresh timer.
    for (const auto &t : targets) {
        const QString monitorName = t.monitorName;
        const bool primary = t.primary;
        if (monitorName.isEmpty()) continue;
        const QString path = QDir::tempPath() + "/kartoza-canvas-" + monitorName + ".png";

        auto store = [this, primary, monitorName](const QString &p) {
            m_mutex.lock();
            if (primary) m_pendingScreenPath = p;
            else m_pendingScreens.insert(monitorName, p);
            m_mutex.unlock();
        };

#if defined(Q_OS_LINUX)
        if (Platform::isWayland()) {
            if (Platform::supportsWlrCapture()) {
                // wlroots compositors (Hyprland, Sway, COSMIC, …) — grim per output.
                QProcess proc;
                proc.start("grim", {"-o", monitorName, "-t", "png", "-l", "0", path});
                if (proc.waitForFinished(5000) && proc.exitCode() == 0) store(path);
                continue;
            }
#ifdef HAS_DBUS
            // GNOME (Mutter) / KDE (KWin) — no wlr-screencopy. Fire an async portal
            // screenshot; the Portal singleton's screenshotReady (wired in the
            // constructor) writes m_pendingScreenPath. The portal picks a single
            // source, so only the primary screen previews on these compositors.
            if (primary) {
                QMetaObject::invokeMethod(&Portal::instance(),
                                          &Portal::requestScreenshot,
                                          Qt::QueuedConnection);
            }
            continue;
#else
            continue;
#endif
        }
#endif

        // X11 / macOS / Windows: Qt's screen grab. QScreen::grabWindow must run on
        // the main thread, so hop there.
        QMetaObject::invokeMethod(this, [path, monitorName, store]() {
            QScreen *targetScreen = nullptr;
            for (auto *screen : QApplication::screens()) {
                if (screen->name() == monitorName) { targetScreen = screen; break; }
            }
            if (!targetScreen) targetScreen = QApplication::primaryScreen();
            if (!targetScreen) return;

            QPixmap pix = targetScreen->grabWindow(0);
            if (!pix.isNull()) {
                QPixmap scaled = pix.scaled(960, 540, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                scaled.save(path, "PNG");
                store(path);
            }
        }, Qt::QueuedConnection);
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

void Canvas::enterEvent(QEnterEvent *event) {
    QWidget::enterEvent(event);
    m_hovered = true;
    if (hasScreenItem()) update();
}

void Canvas::leaveEvent(QEvent *event) {
    QWidget::leaveEvent(event);
    m_hovered = false;
    if (m_hoverItem != -1) { m_hoverItem = -1; update(); }
    else if (hasScreenItem()) update();
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
        // Screen item rescales its position proportionally and fills the frame.
        for (auto &item : m_items) {
            if (item.type == 0) {
                double relX = double(item.x) / m_lastFrameRect.width();
                double relY = double(item.y) / m_lastFrameRect.height();
                item.x = int(relX * newFrame.width());
                item.y = int(relY * newFrame.height());
                item.w = newFrame.width();
                item.h = newFrame.height();
            }
        }
        rescaleItemsToFrame(m_lastFrameRect, newFrame);
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
    // Returns: 0=none, 1=top, 2=bottom, 3=left, 4=right, 5=TL, 6=TR, 7=BL, 8=BR.
    int left = item.x - item.w/2;
    int top = item.y - item.h/2;
    int right = left + item.w;
    int bottom = top + item.h;
    int hs = CROP_HANDLE_SIZE;
    int cl = left + item.cropLeft, cr = right - item.cropRight;
    int ct = top + item.cropTop, cb = bottom - item.cropBottom;

    // Corners first (crop two edges at once — Alt+"scale" crops from the corner).
    if (std::abs(mx - cl) <= hs && std::abs(my - ct) <= hs) return 5; // TL
    if (std::abs(mx - cr) <= hs && std::abs(my - ct) <= hs) return 6; // TR
    if (std::abs(mx - cl) <= hs && std::abs(my - cb) <= hs) return 7; // BL
    if (std::abs(mx - cr) <= hs && std::abs(my - cb) <= hs) return 8; // BR
    // Top handle: centered horizontally at item top edge
    if (std::abs(my - ct) <= hs && std::abs(mx - item.x) <= hs*2) return 1;
    // Bottom handle
    if (std::abs(my - cb) <= hs && std::abs(mx - item.x) <= hs*2) return 2;
    // Left handle
    if (std::abs(mx - cl) <= hs && std::abs(my - item.y) <= hs*2) return 3;
    // Right handle
    if (std::abs(mx - cr) <= hs && std::abs(my - item.y) <= hs*2) return 4;

    return 0;
}

int Canvas::hitResizeHandle(const CanvasItem &item, int mx, int my) const {
    // Returns: 0=none, 1=TL, 2=TR, 3=BL, 4=BR, 5=top, 6=bottom, 7=left, 8=right
    int left = item.x - item.w/2;
    int top = item.y - item.h/2;
    int right = left + item.w;
    int bottom = top + item.h;
    int hs = RESIZE_HANDLE_SIZE;

    // Corners take priority (aspect-locked resize).
    if (std::abs(mx - left) <= hs && std::abs(my - top) <= hs) return 1;
    if (std::abs(mx - right) <= hs && std::abs(my - top) <= hs) return 2;
    if (std::abs(mx - left) <= hs && std::abs(my - bottom) <= hs) return 3;
    if (std::abs(mx - right) <= hs && std::abs(my - bottom) <= hs) return 4;
    // Edge midpoints (single-axis / free resize).
    if (std::abs(my - top) <= hs && std::abs(mx - item.x) <= hs) return 5;
    if (std::abs(my - bottom) <= hs && std::abs(mx - item.x) <= hs) return 6;
    if (std::abs(mx - left) <= hs && std::abs(my - item.y) <= hs) return 7;
    if (std::abs(mx - right) <= hs && std::abs(my - item.y) <= hs) return 8;
    return 0;
}

void Canvas::mousePressEvent(QMouseEvent *event) {
    int mx = event->pos().x() - m_offsetX, my = event->pos().y() - m_offsetY;
    m_cropHandle = 0;
    m_cropItem = -1;
    m_resizeHandle = 0;
    m_resizeItem = -1;
    int hit = -1;
    bool altHeld = event->modifiers() & Qt::AltModifier;
    m_altActive = altHeld;

    // The per-element pause/continue toggle. Only the *visible* toggle (shown on
    // hover, or while the element is paused) is clickable, so a normal press on
    // the item body still selects/drags it. A press on the visible toggle is
    // deferred: a click (release in place) toggles pause, a drag moves the item.
    m_pendingToggle = -1;
    if (event->button() == Qt::LeftButton) {
        int cand = (m_hoverItem >= 0) ? m_hoverItem : liveItemAt(mx, my);
        if (cand >= 0 && (m_items[cand].paused || m_hoverItem == cand)) {
            QRect toggle = itemToggleRect(cand);
            if (!toggle.isEmpty() && toggle.contains(mx, my)) {
                m_pendingToggle = cand;
                m_pressPos = QPoint(mx, my);
                m_selected = cand;
                m_dragging = cand;
                m_dragOffX = mx - m_items[cand].x;
                m_dragOffY = my - m_items[cand].y;
                emit selectionChanged(cand);
                setFocus();
                update();
                return;
            }
        }
    }

    // Check handles on the selected item first. Default is aspect-locked resize
    // (corner handles); holding Alt switches to crop (edge handles).
    if (m_selected >= 0 && m_selected < m_items.size() && m_items[m_selected].visible) {
        if (altHeld) {
            int handle = hitCropHandle(m_items[m_selected], mx, my);
            if (handle > 0) {
                m_cropHandle = handle;
                m_cropItem = m_selected;
                setFocus();
                update();
                emit selectionChanged(m_selected);
                return;
            }
        } else {
            int rhandle = hitResizeHandle(m_items[m_selected], mx, my);
            if (rhandle > 0) {
                auto &it = m_items[m_selected];
                m_resizeHandle = rhandle;
                m_resizeItem = m_selected;
                m_resizeStartW = it.w;
                m_resizeStartH = it.h;
                // Anchor is the point opposite the grabbed handle; it stays fixed
                // while the grabbed handle tracks the cursor (no size cap).
                int L = it.x - it.w/2, T = it.y - it.h/2;
                int R = L + it.w, B = T + it.h;
                switch (rhandle) {
                case 1: m_resizeAnchorX = R;    m_resizeAnchorY = B;    break; // TL
                case 2: m_resizeAnchorX = L;    m_resizeAnchorY = B;    break; // TR
                case 3: m_resizeAnchorX = R;    m_resizeAnchorY = T;    break; // BL
                case 4: m_resizeAnchorX = L;    m_resizeAnchorY = T;    break; // BR
                case 5: m_resizeAnchorX = it.x; m_resizeAnchorY = B;    break; // top
                case 6: m_resizeAnchorX = it.x; m_resizeAnchorY = T;    break; // bottom
                case 7: m_resizeAnchorX = R;    m_resizeAnchorY = it.y; break; // left
                case 8: m_resizeAnchorX = L;    m_resizeAnchorY = it.y; break; // right
                }
                setFocus();
                update();
                emit selectionChanged(m_selected);
                return;
            }
        }
    }

    // First pass: check non-screen items (drawn on top)
    for (int i = m_items.size()-1; i >= 0; i--) {
        if (!m_items[i].visible || m_items[i].type == 0) continue;
        if (hitTest(m_items[i], mx, my)) {
            hit = i;
            if (altHeld && m_items[i].type == 1) {
                // Alt+drag a webcam body pans its crop window across the source,
                // so you can aim the (cropped) bubble at the part you want to show.
                m_cropPanItem = i;
                m_panStartCropLeft = m_items[i].cropLeft;
                m_panStartCropTop = m_items[i].cropTop;
                m_panStartMX = mx;
                m_panStartMY = my;
            } else {
                m_dragging = i;
                m_dragOffX = mx - m_items[i].x;
                m_dragOffY = my - m_items[i].y;
            }
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
    bool altHeld = event->modifiers() & Qt::AltModifier;

    // A press that landed on a pause toggle stays a "click" (which toggles on
    // release) until the cursor moves past a small threshold, at which point it
    // becomes an ordinary drag of the item.
    if (m_pendingToggle >= 0) {
        if ((QPoint(mx, my) - m_pressPos).manhattanLength() <= 4) return;
        m_pendingToggle = -1; // promoted to a drag; fall through to normal handling
    }

    // Handle resize (corner) handle dragging — aspect-locked scale about centre,
    // giving the same result as the scroll wheel.
    if (m_resizeHandle > 0 && m_resizeItem >= 0 && m_resizeItem < m_items.size()) {
        auto &item = m_items[m_resizeItem];
        bool isText = (item.type == 3);
        bool corner = (m_resizeHandle >= 1 && m_resizeHandle <= 4);
        bool horizEdge = (m_resizeHandle == 7 || m_resizeHandle == 8); // left/right
        bool vertEdge = (m_resizeHandle == 5 || m_resizeHandle == 6);  // top/bottom
        // Raw box dimensions from the fixed anchor to the cursor (no cap).
        int rawW = std::abs(mx - m_resizeAnchorX);
        int rawH = std::abs(my - m_resizeAnchorY);
        double aspect = m_resizeStartH > 0 ? double(m_resizeStartW) / m_resizeStartH : 1.0;

        if (isText) {
            // Text resizes to any size. Corners keep the box aspect (Shift = free);
            // edges stretch a single axis so text can be any width/height.
            if (corner) {
                if (shiftHeld) {
                    item.w = std::max(20, rawW);
                    item.h = std::max(10, rawH);
                } else {
                    int w = std::max(rawW, int(std::lround(rawH * aspect)));
                    item.w = std::max(20, w);
                    item.h = std::max(10, int(std::lround(item.w / aspect)));
                }
            } else if (horizEdge) {
                item.w = std::max(20, rawW);
            } else { // vertEdge
                item.h = std::max(10, rawH);
            }
        } else {
            // Other items stay aspect-locked; the grabbed handle's axis drives it.
            int newW;
            if (vertEdge) newW = int(std::lround(rawH * aspect));
            else if (horizEdge) newW = rawW;
            else newW = std::max(rawW, int(std::lround(rawH * aspect)));
            setItemWidthKeepingAspect(item, newW);
        }

        // Keep the anchor point fixed: the box extends from it toward the cursor.
        int signX = (mx >= m_resizeAnchorX) ? 1 : -1;
        int signY = (my >= m_resizeAnchorY) ? 1 : -1;
        if (horizEdge) {
            item.x = m_resizeAnchorX + signX * item.w / 2;
            item.y = m_resizeAnchorY;
        } else if (vertEdge) {
            item.y = m_resizeAnchorY + signY * item.h / 2;
            item.x = m_resizeAnchorX;
        } else {
            item.x = m_resizeAnchorX + signX * item.w / 2;
            item.y = m_resizeAnchorY + signY * item.h / 2;
        }
        update();
        return;
    }

    // Handle crop-window panning (Alt+drag a webcam body): slide the cropped
    // window across the source so the shown portion tracks toward the cursor,
    // keeping the window size fixed. Only pans on an axis that has crop room.
    if (m_cropPanItem >= 0 && m_cropPanItem < m_items.size()) {
        auto &item = m_items[m_cropPanItem];
        int totalH = item.cropLeft + item.cropRight;
        int totalV = item.cropTop + item.cropBottom;
        int newLeft = std::clamp(m_panStartCropLeft + (mx - m_panStartMX), 0, totalH);
        int newTop  = std::clamp(m_panStartCropTop  + (my - m_panStartMY), 0, totalV);
        item.cropLeft = newLeft;   item.cropRight = totalH - newLeft;
        item.cropTop = newTop;     item.cropBottom = totalV - newTop;
        update();
        return;
    }

    // Handle crop handle dragging
    if (m_cropHandle > 0 && m_cropItem >= 0 && m_cropItem < m_items.size()) {
        auto &item = m_items[m_cropItem];
        int left = item.x - item.w/2;
        int top = item.y - item.h/2;
        int right = left + item.w;
        int bottom = top + item.h;
        int minVisible = 20; // minimum visible area

        auto cropTop    = [&]{ item.cropTop    = std::clamp(my - top,    0, item.h - item.cropBottom - minVisible); };
        auto cropBottom = [&]{ item.cropBottom = std::clamp(bottom - my, 0, item.h - item.cropTop    - minVisible); };
        auto cropLeft   = [&]{ item.cropLeft   = std::clamp(mx - left,   0, item.w - item.cropRight  - minVisible); };
        auto cropRight  = [&]{ item.cropRight  = std::clamp(right - mx,  0, item.w - item.cropLeft   - minVisible); };
        switch (m_cropHandle) {
        case 1: cropTop();    break;
        case 2: cropBottom(); break;
        case 3: cropLeft();   break;
        case 4: cropRight();  break;
        case 5: cropTop();    cropLeft();  break; // TL corner
        case 6: cropTop();    cropRight(); break; // TR corner
        case 7: cropBottom(); cropLeft();  break; // BL corner
        case 8: cropBottom(); cropRight(); break; // BR corner
        }
        update();
        return;
    }

    if (m_dragging < 0) {
        // Track Alt so the handle colour/mode updates live while hovering.
        if (m_altActive != altHeld) {
            m_altActive = altHeld;
            update();
        }
        // Track which live element the cursor is over so its (and only its)
        // pause/continue toggle is revealed.
        int hover = liveItemAt(mx, my);
        if (hover != m_hoverItem) { m_hoverItem = hover; update(); }
        // Update cursor for handle hover on the selected item.
        if (m_selected >= 0 && m_selected < m_items.size()) {
            if (altHeld) {
                int handle = hitCropHandle(m_items[m_selected], mx, my);
                if (handle == 1 || handle == 2)
                    setCursor(Qt::SizeVerCursor);
                else if (handle == 3 || handle == 4)
                    setCursor(Qt::SizeHorCursor);
                else if (handle == 5 || handle == 8)
                    setCursor(Qt::SizeFDiagCursor);
                else if (handle == 6 || handle == 7)
                    setCursor(Qt::SizeBDiagCursor);
                else if (m_items[m_selected].type == 1 && hitTest(m_items[m_selected], mx, my))
                    setCursor(Qt::SizeAllCursor); // Alt+drag body pans the crop window
                else
                    setCursor(Qt::ArrowCursor);
            } else {
                int rhandle = hitResizeHandle(m_items[m_selected], mx, my);
                if (rhandle == 1 || rhandle == 4)
                    setCursor(Qt::SizeFDiagCursor);
                else if (rhandle == 2 || rhandle == 3)
                    setCursor(Qt::SizeBDiagCursor);
                else if (rhandle == 5 || rhandle == 6)
                    setCursor(Qt::SizeVerCursor);
                else if (rhandle == 7 || rhandle == 8)
                    setCursor(Qt::SizeHorCursor);
                else
                    setCursor(Qt::ArrowCursor);
            }
        }
        return;
    }

    auto &item = m_items[m_dragging];
    int nx = mx - m_dragOffX;
    int ny = my - m_dragOffY;

    // Recording frame (output area) bounds — everything is constrained to it so
    // the entire recording area stays visible and nothing is dragged off it.
    QRect fr = frameRect();
    int frL = fr.x(), frR = fr.x() + fr.width();
    int frT = fr.y(), frB = fr.y() + fr.height();

    if (m_dragging == screenItemIndex()) {
        // Primary background screen: keep it covering the whole recording frame so
        // the recorded area is never left with an uncovered (dark) gap.
        item.x = (item.w >= fr.width())
            ? std::clamp(nx, frR - item.w/2, frL + item.w/2)
            : std::clamp(nx, frL + item.w/2, frR - item.w/2);
        item.y = (item.h >= fr.height())
            ? std::clamp(ny, frB - item.h/2, frT + item.h/2)
            : std::clamp(ny, frT + item.h/2, frB - item.h/2);
        m_snapXActive = false;
        m_snapYActive = false;
    } else {
        // Overlays and additional monitors stay within the recording frame.
        int loX = frL + item.w/2, hiX = frR - item.w/2;
        int loY = frT + item.h/2, hiY = frB - item.h/2;
        if (loX > hiX) loX = hiX = fr.center().x();
        if (loY > hiY) loY = hiY = fr.center().y();
        item.x = std::clamp(nx, loX, hiX);
        item.y = std::clamp(ny, loY, hiY);

        // Snap to frame, other objects and half-dimension guides (unless Shift).
        if (!shiftHeld) {
            applySnapping(item);
        } else {
            m_snapXActive = false;
            m_snapYActive = false;
        }
    }

    update();
}

void Canvas::mouseReleaseEvent(QMouseEvent *) {
    // A pause toggle that was pressed and released without dragging is a click:
    // toggle that element's pause (the item did not actually move).
    if (m_pendingToggle >= 0) {
        int t = m_pendingToggle;
        m_pendingToggle = -1;
        m_dragging = -1;
        toggleItemPause(t);
        return;
    }
    if (m_resizeHandle > 0) {
        m_resizeHandle = 0;
        m_resizeItem = -1;
        setCursor(Qt::ArrowCursor);
        emit itemsChanged();
    }
    if (m_cropHandle > 0) {
        m_cropHandle = 0;
        m_cropItem = -1;
        setCursor(Qt::ArrowCursor);
        emit itemsChanged();
    }
    if (m_cropPanItem >= 0) {
        m_cropPanItem = -1;
        setCursor(Qt::ArrowCursor);
        emit itemsChanged();
    }
    if (m_dragging >= 0) {
        m_dragging = -1;
        m_snapXActive = false;
        m_snapYActive = false;
        emit itemsChanged();
        update();
    }
}

void Canvas::setItemWidthKeepingAspect(CanvasItem &item, int newW, int textHeight) {
    if (item.type == 0) { // screen — locked to 16:9
        item.w = std::max(m_cw / 2, newW);
        item.h = item.w * 9 / 16;
    } else if (item.type == 1) { // webcam — round is square, otherwise 4:3
        item.w = std::max(20, newW);
        item.h = (item.shape == 0) ? item.w : item.w * 3 / 4;
    } else if (item.type == 2 && !item.pixmap.isNull() && item.pixmap.width() > 0) {
        item.w = std::max(20, newW);
        item.h = std::max(10, item.w * item.pixmap.height() / item.pixmap.width());
    } else if (item.type == 3) { // text — height drives the container-derived font size
        item.w = std::max(20, newW);
        if (textHeight >= 0) item.h = std::max(10, textHeight);
    } else {
        item.w = std::max(20, newW);
        item.h = std::max(15, item.h);
    }
}

void Canvas::applySnapping(CanvasItem &item) {
    const int snap = 12;
    QRect fr = frameRect();
    int frR = fr.x() + fr.width();
    int frB = fr.y() + fr.height();

    // Candidate vertical (x) and horizontal (y) snap lines: frame edges, frame
    // centre (the half-width / half-height guides), and every other object's
    // edges and centre.
    QList<int> xLines = { fr.x(), fr.x() + fr.width()/2, frR };
    QList<int> yLines = { fr.y(), fr.y() + fr.height()/2, frB };
    for (int i = 0; i < m_items.size(); i++) {
        if (i == m_dragging) continue;
        const auto &o = m_items[i];
        if (!o.visible || o.type == 4 || o.type == 5) continue; // sounds have no geometry
        xLines << (o.x - o.w/2) << o.x << (o.x + o.w/2);
        yLines << (o.y - o.h/2) << o.y << (o.y + o.h/2);
    }

    // Dragged-item anchors: left/centre/right and top/centre/bottom.
    int itemL = item.x - item.w/2, itemR = item.x + item.w/2;
    int itemT = item.y - item.h/2, itemB = item.y + item.h/2;

    int bestDX = snap + 1, shiftX = 0, guideX = 0;
    for (int line : xLines)
        for (int a : {itemL, item.x, itemR}) {
            int d = std::abs(a - line);
            if (d < bestDX) { bestDX = d; shiftX = line - a; guideX = line; }
        }
    m_snapXActive = (bestDX <= snap);
    if (m_snapXActive) { item.x += shiftX; m_snapXLine = guideX; }

    int bestDY = snap + 1, shiftY = 0, guideY = 0;
    for (int line : yLines)
        for (int a : {itemT, item.y, itemB}) {
            int d = std::abs(a - line);
            if (d < bestDY) { bestDY = d; shiftY = line - a; guideY = line; }
        }
    m_snapYActive = (bestDY <= snap);
    if (m_snapYActive) { item.y += shiftY; m_snapYLine = guideY; }
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
            int textH = (item.type == 3) ? item.h + delta : -1;
            setItemWidthKeepingAspect(item, item.w + delta, textH);
            emit itemsChanged();
            update();
            return;
        }
    }

    // If no overlay item hit, allow scroll-wheel on the screen item (scale/zoom)
    if (m_selected >= 0 && m_selected < m_items.size() && m_items[m_selected].type == 0) {
        auto &item = m_items[m_selected];
        int delta = event->angleDelta().y() > 0 ? 10 : -10;
        setItemWidthKeepingAspect(item, item.w + delta);
        emit itemsChanged();
        update();
    }
}

void Canvas::keyPressEvent(QKeyEvent *event) {
    // Alt switches the selected item's handles from resize to crop; reflect it
    // immediately so the handle colour changes even without mouse movement.
    if (event->key() == Qt::Key_Alt && !m_altActive) {
        m_altActive = true;
        update();
    }
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

void Canvas::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Alt && m_altActive) {
        m_altActive = false;
        update();
    }
    QWidget::keyReleaseEvent(event);
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

    // Draw items. The primary screen is the background (drawn by drawScreen);
    // every other item — including *additional* monitors — is composited on top.
    int primaryIdx = screenItemIndex();
    for (int i = 0; i < m_items.size(); i++) {
        const auto &item = m_items[i];
        if (!item.visible) continue;
        if (item.type == 0 && i == primaryIdx) continue; // primary handled by drawScreen
        bool isDragging = (i == m_dragging);
        bool isSelected = (i == m_selected);

        // Compute visible rect after cropping
        int visLeft = item.x - item.w/2 + item.cropLeft;
        int visTop = item.y - item.h/2 + item.cropTop;
        int visW = item.w - item.cropLeft - item.cropRight;
        int visH = item.h - item.cropTop - item.cropBottom;
        QRect visRect(visLeft, visTop, visW, visH);

        if (item.type == 0) { // Additional monitor (composited inset)
            if (!item.screenPixmap.isNull()) {
                int pw = item.screenPixmap.width(), ph = item.screenPixmap.height();
                int srcL = item.cropLeft * pw / item.w;
                int srcT = item.cropTop * ph / item.h;
                int srcW = visW * pw / item.w;
                int srcH = visH * ph / item.h;
                painter.drawPixmap(visRect, item.screenPixmap, QRect(srcL, srcT, srcW, srcH));
            } else {
                painter.fillRect(visRect, QColor(26, 26, 46));
                painter.setPen(QColor(138, 139, 139));
                painter.drawText(visRect, Qt::AlignCenter, item.label.left(16));
            }
            painter.setPen(isSelected ? QColor(232, 184, 74) : QColor(86, 159, 198));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(visRect);
        } else if (item.type == 2) { // Logo
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
                // For round webcam, crop selects the shown portion of the source:
                // draw the centre square of the cropped region into the circle
                // (matches the round recording path), so Alt-crop visibly crops
                // down and the panned crop window aims at the chosen part.
                int r = std::min(visW, visH) / 2;
                int cx = visLeft + visW/2, cy = visTop + visH/2;
                if (hasFrame) {
                    int pw = item.webcamPixmap.width(), ph = item.webcamPixmap.height();
                    int srcL = item.cropLeft * pw / item.w;
                    int srcT = item.cropTop * ph / item.h;
                    int srcW = visW * pw / item.w;
                    int srcH = visH * ph / item.h;
                    int srcSq = std::max(1, std::min(srcW, srcH));
                    int srcX = srcL + (srcW - srcSq) / 2;
                    int srcY = srcT + (srcH - srcSq) / 2;
                    painter.save();
                    QPainterPath clipPath;
                    clipPath.addEllipse(cx-r, cy-r, r*2, r*2);
                    painter.setClipPath(clipPath);
                    painter.drawPixmap(QRect(cx-r, cy-r, r*2, r*2), item.webcamPixmap,
                                       QRect(srcX, srcY, srcSq, srcSq));
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
        } else if (item.type == 3) { // Text box
            painter.save();
            int fontSize = std::max(6, visH * 2 / 3); // size derives from the container
            QFont f(item.fontFamily.isEmpty() ? QStringLiteral("Sans") : item.fontFamily, fontSize);
            f.setWeight(static_cast<QFont::Weight>(item.fontWeight));
            painter.setFont(f);
            QString col = item.textColor.isEmpty() ? m_titleColor : item.textColor;
            painter.setPen(isDragging ? QColor(86, 159, 198) : QColor(col));
            painter.setClipRect(visRect);
            painter.drawText(QPoint(item.x - item.w/2, item.y + item.h/4), item.label);
            painter.restore();
            if (isDragging || isSelected) {
                painter.setPen(isSelected ? QColor(232, 184, 74) : QColor(86, 159, 198));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(visRect.adjusted(-2, -2, 2, 2));
            }
        }

        // Draw handles on the selected item — crop (Alt) or resize (default).
        if (isSelected) {
            if (cropModeActive()) drawCropHandles(painter, item);
            else drawResizeHandles(painter, item);
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

    // Snap guide lines while dragging (alignment feedback).
    if (m_dragging >= 0) {
        painter.setPen(QPen(QColor(86, 159, 198, 200), 1, Qt::DashLine));
        if (m_snapXActive) painter.drawLine(m_snapXLine, 0, m_snapXLine, m_ch);
        if (m_snapYActive) painter.drawLine(0, m_snapYLine, m_cw, m_snapYLine);
    }

    // Per-element pause/continue toggles (topmost). Each live element shows its
    // own centred button — revealed on hover, or kept visible while that element
    // is paused so the frozen state always reads as intentional.
    for (int i = 0; i < m_items.size(); i++) {
        if (isLiveItem(i)) drawItemToggle(painter, i);
    }
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
    int screenIdx = screenItemIndex();
    if (screenIdx >= 0 && (screenIdx == m_selected || screenIdx == m_dragging)) {
        painter.setPen(QPen(QColor(232, 184, 74), 2, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(0, 0, m_cw-1, m_ch-1);
        if (screenIdx == m_selected) {
            if (cropModeActive()) drawCropHandles(painter, m_items[screenIdx]);
            else drawResizeHandles(painter, m_items[screenIdx]);
        }
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

    QColor handleColor(86, 159, 198); // Kartoza blue #569FC6 — signals crop mode
    QColor cropAreaColor(86, 159, 198, 40);
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
    int cl = left + item.cropLeft, cr = right - item.cropRight;
    int ct = top + item.cropTop, cb = bottom - item.cropBottom;
    // Edge handles
    painter.drawRect(item.x - hs, ct - hs/2, hs*2, hs);       // top
    painter.drawRect(item.x - hs, cb - hs/2, hs*2, hs);       // bottom
    painter.drawRect(cl - hs/2, item.y - hs, hs, hs*2);       // left
    painter.drawRect(cr - hs/2, item.y - hs, hs, hs*2);       // right
    // Corner handles (crop two edges at once).
    painter.drawRect(cl - hs/2, ct - hs/2, hs, hs);           // TL
    painter.drawRect(cr - hs/2, ct - hs/2, hs, hs);           // TR
    painter.drawRect(cl - hs/2, cb - hs/2, hs, hs);           // BL
    painter.drawRect(cr - hs/2, cb - hs/2, hs, hs);           // BR
}

bool Canvas::cropModeActive() const {
    // During an active drag the mode is locked to the handle being dragged;
    // otherwise it follows whether Alt is currently held.
    if (m_cropHandle > 0) return true;
    if (m_resizeHandle > 0) return false;
    return m_altActive;
}

void Canvas::drawResizeHandles(QPainter &painter, const CanvasItem &item) {
    int left = item.x - item.w/2;
    int top = item.y - item.h/2;
    int right = left + item.w;
    int bottom = top + item.h;
    int hs = RESIZE_HANDLE_SIZE;

    QColor handleColor(223, 158, 47); // Kartoza highlight1 #DF9E2F — resize mode
    painter.setPen(Qt::NoPen);
    painter.setBrush(handleColor);
    // Corner squares (aspect-locked resize).
    painter.drawRect(left - hs/2, top - hs/2, hs, hs);
    painter.drawRect(right - hs/2, top - hs/2, hs, hs);
    painter.drawRect(left - hs/2, bottom - hs/2, hs, hs);
    painter.drawRect(right - hs/2, bottom - hs/2, hs, hs);
    // Edge midpoint squares (single-axis / free resize).
    painter.drawRect(item.x - hs/2, top - hs/2, hs, hs);
    painter.drawRect(item.x - hs/2, bottom - hs/2, hs, hs);
    painter.drawRect(left - hs/2, item.y - hs/2, hs, hs);
    painter.drawRect(right - hs/2, item.y - hs/2, hs, hs);
}
