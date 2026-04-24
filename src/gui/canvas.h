#pragma once

#include <QWidget>
#include <QPixmap>
#include <QTimer>
#include <QProcess>
#include "monitor/monitor.h"

class Canvas : public QWidget {
    Q_OBJECT

public:
    explicit Canvas(QWidget *parent = nullptr);

    void setMonitor(const MonitorInfo &mon);
    void setTitle(const QString &text);
    void setMode(int mode); // 0=landscape, 1=vertical, 2=left split, 3=right split
    void addWebcam(const QString &device, const QString &name, int shape);
    void addLogo(const QString &filePath);
    QString selectedMonitor() const { return m_monitorName; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct CanvasItem {
        int type; // 0=screen, 1=webcam, 2=logo, 3=title
        QString label;
        int x, y, w, h;
        int shape = 0; // webcam: 0=round, 1=square, 2=rect
        QPixmap pixmap;
        QString filePath;
        QString device;
        bool visible = true;
    };

    void captureScreen();
    void drawScreen(QPainter &painter);

    QList<CanvasItem> m_items;
    QPixmap m_screenPixmap;
    QString m_monitorName;
    MonitorInfo m_monitor;
    QString m_title;

    int m_mode = 0; // 0=landscape, 1=vertical, 2=left, 3=right
    int m_cw = 560, m_ch = 315;
    int m_dragging = -1;
    int m_dragOffX = 0, m_dragOffY = 0;

    QTimer *m_refreshTimer;
    QString m_pendingScreenPath;
};
