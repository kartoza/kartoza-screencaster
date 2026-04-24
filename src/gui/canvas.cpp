#include "gui/canvas.h"
#include "config/config.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QDir>
#include <QTemporaryDir>
#include <QtConcurrent>
#include <QThreadPool>
#include <cmath>

Canvas::Canvas(QWidget *parent) : QWidget(parent) {
    setMinimumSize(400, 225);
    setMouseTracking(true);
    setStyleSheet("background: #11111b; border-radius: 8px;");

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(100);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        // Load pending screenshot (created by background thread)
        if (!m_pendingScreenPath.isEmpty()) {
            QPixmap pix(m_pendingScreenPath);
            QFile::remove(m_pendingScreenPath);
            m_pendingScreenPath.clear();
            if (!pix.isNull()) {
                m_screenPixmap = pix;
            }
        }
        update();
    });
    m_refreshTimer->start();

    // Capture screen every 2s in background
    auto *captureTimer = new QTimer(this);
    connect(captureTimer, &QTimer::timeout, this, [this]() {
        QtConcurrent::run(QThreadPool::globalInstance(), [this]() { captureScreen(); });
    });
    captureTimer->start(2000);
}

void Canvas::setMonitor(const MonitorInfo &mon) {
    m_monitor = mon;
    m_monitorName = mon.name;

    // Add screen item if not already present
    bool hasScreen = false;
    for (const auto &item : m_items) {
        if (item.type == 0) { hasScreen = true; break; }
    }
    if (!hasScreen) {
        QString desc = mon.description.isEmpty() ? mon.name : mon.description;
        m_items.prepend({0, "Screen: " + desc, m_cw/2, m_ch/2, m_cw, m_ch, 0, {}, {}, {}, true});
    }

    QtConcurrent::run(QThreadPool::globalInstance(), [this]() { captureScreen(); });
    update();
}

void Canvas::setTitle(const QString &text) {
    m_title = text;
    // Add or update title item
    for (auto &item : m_items) {
        if (item.type == 3) { item.label = text; update(); return; }
    }
    if (!text.isEmpty()) {
        m_items.append({3, text, m_cw/2, m_ch - 25, 200, 20, 0, {}, {}, {}, true});
        update();
    }
}

void Canvas::setMode(int mode) {
    m_mode = mode;
    update();
}

void Canvas::addWebcam(const QString &device, const QString &name, int shape) {
    int count = 0;
    for (const auto &item : m_items) if (item.type == 1) count++;
    int r = 30;
    int w = r * 2, h = r * 2;
    if (shape == 2) { w = r * 3; h = r * 2; }
    int x = m_cw - r - 15 - count * (r * 2 + 10);
    int y = m_ch - r - 15;
    m_items.append({1, name, x, y, w, h, shape, {}, {}, device, true});
    update();
}

void Canvas::addLogo(const QString &filePath) {
    QPixmap pix(filePath);
    if (pix.isNull()) return;

    int logoCount = 0;
    for (const auto &item : m_items) if (item.type == 2) logoCount++;

    int w = m_cw / 6;
    if (w < 40) w = 40;
    int h = w * pix.height() / pix.width();
    if (h < 15) h = 15;
    int x = w/2 + 10 + logoCount * (w + 10);
    int y = h/2 + 10;
    if (x + w/2 > m_cw) { x = w/2 + 10; y = h + 20 + h/2; }

    QFileInfo fi(filePath);
    m_items.append({2, fi.fileName(), x, y, w, h, 0, pix, filePath, {}, true});
    update();
}

void Canvas::captureScreen() {
    if (m_monitorName.isEmpty()) return;
    QString path = QDir::tempPath() + "/kartoza-canvas-" + m_monitorName + ".png";
    QProcess grim;
    grim.start("grim", {"-o", m_monitorName, "-t", "png", "-l", "0", path});
    if (grim.waitForFinished(5000) && grim.exitCode() == 0) {
        m_pendingScreenPath = path; // main thread timer picks it up
    }
}

void Canvas::resizeEvent(QResizeEvent *event) {
    m_cw = event->size().width();
    m_ch = event->size().height();
    QWidget::resizeEvent(event);
}

void Canvas::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 17, 27));

    drawScreen(painter);

    // Draw items (skip screen — handled by drawScreen)
    for (int i = 0; i < m_items.size(); i++) {
        const auto &item = m_items[i];
        if (!item.visible || item.type == 0) continue;

        bool isDragging = (i == m_dragging);

        if (item.type == 2 && !item.pixmap.isNull()) {
            // Logo
            painter.drawPixmap(QRect(item.x - item.w/2, item.y - item.h/2, item.w, item.h), item.pixmap);
            if (isDragging) {
                painter.setPen(QColor(137, 180, 250));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(item.x - item.w/2, item.y - item.h/2, item.w, item.h);
            }
        } else if (item.type == 1) {
            // Webcam
            QPen pen(isDragging ? QColor(137, 180, 250) : QColor(205, 214, 244));
            painter.setPen(pen);
            QBrush brush(isDragging ? QColor(137, 180, 250) : QColor(166, 227, 161));
            painter.setBrush(brush);
            int r = item.w / 2;
            if (item.shape == 0) painter.drawEllipse(item.x-r, item.y-r, item.w, item.h);
            else painter.drawRect(item.x-item.w/2, item.y-item.h/2, item.w, item.h);

            painter.setPen(QColor(30, 30, 46));
            QString name = item.label.left(6);
            painter.drawText(QPoint(item.x - r/2, item.y + 4), name);
        } else if (item.type == 3) {
            // Title
            painter.save();
            int fontSize = std::max(6, item.h * 2 / 3);
            painter.setFont(QFont("Sans", fontSize));
            painter.setPen(QColor(Config::instance().titleColor));
            painter.drawText(QPoint(item.x - item.w/2, item.y + item.h/4), item.label);
            if (isDragging) {
                painter.setPen(QColor(137, 180, 250));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(item.x-item.w/2-2, item.y-item.h/2-2, item.w+4, item.h+4);
            }
            painter.restore();
        }
    }

    // Mode label
    painter.setPen(QColor(108, 112, 134));
    QStringList modeNames = {"Landscape 16:9", "Vertical 9:16", "9:16 (Left Split)", "9:16 (Right Split)"};
    painter.drawText(QPoint(5, m_ch - 5), modeNames.value(m_mode, ""));
}

void Canvas::drawScreen(QPainter &painter) {
    bool hasScreen = !m_screenPixmap.isNull();

    if (m_mode == 0) {
        // Landscape: full canvas
        if (hasScreen)
            painter.drawPixmap(rect(), m_screenPixmap);
        else {
            painter.fillRect(rect(), QColor(30, 30, 46));
            painter.setPen(QColor(108, 112, 134));
            painter.drawText(QPoint(m_cw/2-20, m_ch/2), "Screen");
        }
    } else {
        // Vertical modes: centered 9:16 frame
        int fh = m_ch - 20;
        int fw = fh * 9 / 16;
        if (fw > m_cw - 20) { fw = m_cw - 20; fh = fw * 16 / 9; }
        int fx = (m_cw - fw) / 2;
        int fy = (m_ch - fh) / 2;

        // Dim outside frame
        painter.setOpacity(0.6);
        painter.fillRect(QRect(0, 0, fx, m_ch), Qt::black);
        painter.fillRect(QRect(fx+fw, 0, m_cw-fx-fw, m_ch), Qt::black);
        painter.fillRect(QRect(fx, 0, fw, fy), Qt::black);
        painter.fillRect(QRect(fx, fy+fh, fw, m_ch-fy-fh), Qt::black);
        painter.setOpacity(1.0);

        // Screen in top of frame
        int screenH = (m_mode >= 2) ? fw * 9 / 8 : fw * 9 / 16; // split: 8:9, normal: 16:9
        if (screenH > fh) screenH = fh;

        if (hasScreen) {
            QRect target(fx, fy, fw, screenH);
            if (m_mode == 2) { // left split: source = left half
                QRect src(0, 0, m_screenPixmap.width()/2, m_screenPixmap.height());
                painter.drawPixmap(target, m_screenPixmap, src);
            } else if (m_mode == 3) { // right split: source = right half
                int pw = m_screenPixmap.width();
                QRect src(pw/2, 0, pw/2, m_screenPixmap.height());
                painter.drawPixmap(target, m_screenPixmap, src);
            } else {
                painter.drawPixmap(target, m_screenPixmap);
            }
        }

        // White space below screen
        if (screenH < fh)
            painter.fillRect(QRect(fx, fy+screenH, fw, fh-screenH), Qt::white);

        // Split guide line
        if (m_mode >= 2 && hasScreen) {
            painter.setPen(QColor(249, 226, 175)); // yellow
            // The split is already applied in the source crop, just show the frame
        }

        // Frame border
        painter.setPen(QColor(137, 180, 250));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(fx, fy, fw, fh);
    }

    // Canvas border
    painter.setPen(QColor(69, 71, 90));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(0, 0, m_cw-1, m_ch-1);
}

void Canvas::mousePressEvent(QMouseEvent *event) {
    int mx = event->pos().x(), my = event->pos().y();
    for (int i = m_items.size()-1; i >= 0; i--) {
        const auto &item = m_items[i];
        if (!item.visible || item.type == 0) continue;
        int dx = mx - item.x, dy = my - item.y;
        bool hit = false;
        if (item.type == 1 && item.shape == 0) // circle
            hit = dx*dx + dy*dy <= (item.w/2)*(item.w/2);
        else
            hit = abs(dx) <= item.w/2 && abs(dy) <= item.h/2;
        if (hit) {
            m_dragging = i;
            m_dragOffX = dx;
            m_dragOffY = dy;
            break;
        }
    }
}

void Canvas::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging < 0) return;
    auto &item = m_items[m_dragging];
    item.x = event->pos().x() - m_dragOffX;
    item.y = event->pos().y() - m_dragOffY;
    // Clamp
    item.x = std::max(item.w/2, std::min(m_cw - item.w/2, item.x));
    item.y = std::max(item.h/2, std::min(m_ch - item.h/2, item.y));
    update();
}

void Canvas::mouseReleaseEvent(QMouseEvent *) {
    m_dragging = -1;
}

void Canvas::wheelEvent(QWheelEvent *event) {
    int mx = event->position().toPoint().x();
    int my = event->position().toPoint().y();
    for (int i = m_items.size()-1; i >= 0; i--) {
        auto &item = m_items[i];
        if (!item.visible || item.type == 0) continue;
        int dx = mx - item.x, dy = my - item.y;
        bool hit = abs(dx) <= item.w/2 && abs(dy) <= item.h/2;
        if (hit) {
            int delta = event->angleDelta().y() > 0 ? 5 : -5;
            item.w = std::max(20, item.w + delta);
            item.h = std::max(15, item.h + delta);
            if (item.type == 1 && item.shape == 2) item.h = item.w * 2 / 3; // rect aspect
            update();
            break;
        }
    }
}
