#pragma once

#include <QWidget>
#include <QPixmap>
#include <QTimer>
#include <QProcess>
#include <QMovie>
#include <QMutex>
#include "monitor/monitor.h"

class Canvas : public QWidget {
    Q_OBJECT

public:
    explicit Canvas(QWidget *parent = nullptr);
    ~Canvas();

    // Content management
    void setMonitor(const MonitorInfo &mon);
    void setTitle(const QString &text);
    void setTitleColor(const QString &color) { m_titleColor = color; update(); }
    void setMode(int mode);
    void addWebcam(const QString &device, const QString &name, int shape);
    void addLogo(const QString &filePath);
    void removeItem(int index);
    void clearAll();

    // Queries
    QString selectedMonitor() const { return m_monitorName; }
    bool hasWebcams() const;
    bool isVertical() const { return m_mode >= 1; }
    int itemCount() const { return m_items.size(); }
    QString itemLabel(int index) const;
    int selectedItem() const { return m_selected; }
    void setSelectedItem(int index);

    // Z-order
    void swapItems(int i, int j);

    // State export (for persistence)
    struct ItemExport {
        int type; QString label; int x, y, w, h; int shape;
        QString filePath; QString device;
        int gifLoop = 2; int gifLoopMax = 3;
    };
    QList<ItemExport> exportItems() const;
    void importItem(const ItemExport &e);
    QString modeString() const;

signals:
    void selectionChanged(int index);
    void itemsChanged(); // for layer list refresh

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
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
        // GIF
        QMovie *movie = nullptr;
        bool isGif = false;
        int gifLoop = 2; // 0=disabled, 1=once, 2=continuous
        int gifLoopMax = 3;
        // Webcam live
        QProcess *webcamProc = nullptr;
        QByteArray webcamBuf;
        QPixmap webcamPixmap;
        bool webcamNewFrame = false;
    };

    void captureScreen();
    void drawScreen(QPainter &painter);
    bool hitTest(const CanvasItem &item, int mx, int my) const;
    void startWebcamCapture(int itemIdx);
    void stopWebcamCapture(int idx);

    QList<CanvasItem> m_items;
    QPixmap m_screenPixmap;
    QString m_monitorName;
    MonitorInfo m_monitor;
    QString m_title;
    QString m_titleColor = "#62A4C7";

    int m_mode = 0;
    int m_cw = 560, m_ch = 315;
    int m_dragging = -1;
    int m_selected = -1;
    int m_dragOffX = 0, m_dragOffY = 0;

    QTimer *m_refreshTimer;
    QString m_pendingScreenPath;
    QMutex m_mutex;

    static constexpr int WC_W = 160, WC_H = 120, WC_FPS = 10;
    static constexpr int WC_FRAME_SIZE = WC_W * WC_H * 3;
};
