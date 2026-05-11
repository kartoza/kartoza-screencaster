/**
 * @file canvas.h
 * @brief WYSIWYG canvas widget for composing recording layouts.
 */

#pragma once

#include <QWidget>
#include <QPixmap>
#include <QTimer>
#include <QProcess>
#include <QMovie>
#include <QMutex>
#include "monitor/monitor.h"

/**
 * @class Canvas
 * @brief Interactive preview canvas showing the recording composition.
 *
 * Displays a scaled representation of the recording output, with draggable
 * overlay items (webcam feeds, logos, title text) on top of the captured
 * screen content. Supports multiple orientation modes and z-order management.
 */
class Canvas : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Construct the canvas widget.
     * @param parent Optional parent widget.
     */
    explicit Canvas(QWidget *parent = nullptr);
    /** @brief Destroy the canvas and stop all live captures. */
    ~Canvas();

    // -- Content management --

    /**
     * @brief Set the monitor to capture as the background screen.
     * @param mon Monitor information for the target display.
     */
    void setMonitor(const MonitorInfo &mon);
    /**
     * @brief Set the title text overlay.
     * @param text Title string to display on the canvas.
     */
    void setTitle(const QString &text);
    /**
     * @brief Set the title text colour.
     * @param color CSS-style colour string (e.g. "#RRGGBB").
     */
    void setTitleColor(const QString &color) { m_titleColor = color; update(); }
    /**
     * @brief Set the canvas orientation mode.
     * @param mode 0 = landscape, >= 1 = vertical/portrait variants.
     */
    void setMode(int mode);
    /**
     * @brief Add a webcam overlay item.
     * @param device V4L2 device path (e.g. "/dev/video0").
     * @param name Human-readable webcam name.
     * @param shape Webcam frame shape: 0 = round, 1 = square, 2 = rectangle.
     */
    void addWebcam(const QString &device, const QString &name, int shape);
    /**
     * @brief Add a logo/image overlay from a file.
     * @param filePath Path to the image file (PNG, SVG, GIF, etc.).
     */
    void addLogo(const QString &filePath);
    /**
     * @brief Remove an overlay item by index.
     * @param index Zero-based index into the item list.
     */
    void removeItem(int index);
    /** @brief Set GIF loop mode and count for an item. */
    void setItemGifLoop(int index, int gifLoop, int gifLoopMax = 3);
    /** @brief Remove all overlay items and reset the canvas. */
    void clearAll();
    /** @brief Stop all running webcam preview capture processes. */
    void stopAllWebcamPreviews();
    /** @brief Restart webcam preview captures for all webcam items. */
    void startAllWebcamPreviews();
    /** @brief Stop all live previews (webcam + screen capture timer). */
    void suspendPreviews();
    /** @brief Resume all live previews (webcam + screen capture timer). */
    void resumePreviews();

    // -- Queries --

    /** @brief Return the name of the currently selected monitor. */
    QString selectedMonitor() const { return m_monitorName; }
    /** @brief Check whether any webcam overlay items exist. */
    bool hasWebcams() const;
    /** @brief Return the device path of the first webcam item, or empty. */
    QString firstWebcamDevice() const;
    /** @brief Return file paths for all logo overlay items. */
    QStringList logoFilePaths() const;
    /** @brief Whether the canvas is in a vertical/portrait mode. */
    bool isVertical() const { return m_mode >= 1; }
    /** @brief Return the total number of overlay items. */
    int itemCount() const { return m_items.size(); }
    /** @brief Check whether an item at the given index is the screen layer. */
    bool isScreenItem(int index) const { return index >= 0 && index < m_items.size() && m_items[index].type == 0; }
    /** @brief Return the logical canvas width in pixels. */
    int canvasWidth() const { return m_cw; }
    /** @brief Return the logical canvas height in pixels. */
    int canvasHeight() const { return m_ch; }
    /** @brief Return the output frame rect (where items should be placed). */
    QRect frameRect() const;
    /**
     * @brief Return the display label for an item.
     * @param index Zero-based item index.
     */
    QString itemLabel(int index) const;
    /** @brief Return the index of the currently selected item, or -1. */
    int selectedItem() const { return m_selected; }
    /**
     * @brief Set the selected item by index.
     * @param index Zero-based item index, or -1 to deselect.
     */
    void setSelectedItem(int index);

    // -- Z-order --

    /**
     * @brief Swap two items in the z-order stack.
     * @param i First item index.
     * @param j Second item index.
     */
    void swapItems(int i, int j);

    // -- State export (for persistence) --

    /**
     * @struct ItemExport
     * @brief Serialisable snapshot of a canvas item for save/restore.
     */
    struct ItemExport {
        int type;         /**< Item type: 0=screen, 1=webcam, 2=logo, 3=title. */
        QString label;    /**< Display label. */
        int x, y, w, h;  /**< Position and size on the canvas. */
        int shape;        /**< Webcam shape (0=round, 1=square, 2=rect). */
        QString filePath; /**< File path for logo items. */
        QString device;   /**< Device path for webcam items. */
        int gifLoop = 2;    /**< GIF loop mode: 0=disabled, 1=once, 2=continuous. */
        int gifLoopMax = 3; /**< Maximum number of GIF loop iterations. */
    };

    /**
     * @brief Export all items as a serialisable list.
     * @return List of ItemExport structs representing the current state.
     */
    QList<ItemExport> exportItems() const;
    /**
     * @brief Import and recreate a canvas item from an exported snapshot.
     * @param e The exported item data.
     */
    void importItem(const ItemExport &e);
    /** @brief Return a string identifier for the current orientation mode. */
    QString modeString() const;

signals:
    /**
     * @brief Emitted when the selected item changes.
     * @param index Index of the newly selected item, or -1.
     */
    void selectionChanged(int index);
    /** @brief Emitted when items are added, removed, or reordered. */
    void itemsChanged();

protected:
    /** @brief Paint the canvas background and all overlay items. */
    void paintEvent(QPaintEvent *event) override;
    /** @brief Handle mouse press for item selection and drag start. */
    void mousePressEvent(QMouseEvent *event) override;
    /** @brief Handle mouse move for item dragging. */
    void mouseMoveEvent(QMouseEvent *event) override;
    /** @brief Handle mouse release to end dragging. */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /** @brief Handle mouse wheel for item resizing. */
    void wheelEvent(QWheelEvent *event) override;
    /** @brief Handle keyboard shortcuts (delete, arrow keys). */
    void keyPressEvent(QKeyEvent *event) override;
    /** @brief Recalculate canvas dimensions on resize. */
    void resizeEvent(QResizeEvent *event) override;

private:
    /**
     * @struct CanvasItem
     * @brief Internal representation of a single compositable item on the canvas.
     */
    struct CanvasItem {
        int type;           /**< 0=screen, 1=webcam, 2=logo, 3=title. */
        QString label;      /**< Display label. */
        int x, y, w, h;    /**< Position and size in canvas pixels. */
        int shape = 0;     /**< Webcam shape: 0=round, 1=square, 2=rect. */
        QPixmap pixmap;     /**< Static image content. */
        QString filePath;   /**< Source file path for logos. */
        QString device;     /**< V4L2 device path for webcams. */
        bool visible = true; /**< Whether the item is drawn. */
        // GIF fields
        QMovie *movie = nullptr; /**< QMovie for animated GIF playback. */
        bool isGif = false;      /**< Whether this item is an animated GIF. */
        int gifLoop = 2;         /**< GIF loop mode. */
        int gifLoopMax = 3;      /**< Max GIF loop iterations. */
        // Webcam live capture fields
        QProcess *webcamProc = nullptr; /**< ffmpeg process for live webcam capture. */
        QByteArray webcamBuf;           /**< Raw frame buffer from webcam. */
        QPixmap webcamPixmap;           /**< Decoded webcam frame pixmap. */
        bool webcamNewFrame = false;    /**< Flag indicating a new frame is available. */
    };

    /** @brief Capture a screenshot of the selected monitor. */
    void captureScreen();
    /** @brief Draw the screen background and all overlay items. */
    void drawScreen(QPainter &painter);
    /**
     * @brief Test whether a point hits a canvas item.
     * @param item The item to test.
     * @param mx Mouse x in canvas coordinates.
     * @param my Mouse y in canvas coordinates.
     * @return True if the point is inside the item.
     */
    bool hitTest(const CanvasItem &item, int mx, int my) const;
    /**
     * @brief Start the live webcam capture process for an item.
     * @param itemIdx Index of the webcam item.
     */
    void startWebcamCapture(int itemIdx);
    /**
     * @brief Stop the live webcam capture process for an item.
     * @param idx Index of the webcam item.
     */
    void stopWebcamCapture(int idx);

    /** @brief Ordered list of all canvas overlay items. */
    QList<CanvasItem> m_items;
    /** @brief Cached screenshot of the selected monitor. */
    QPixmap m_screenPixmap;
    /** @brief Name/identifier of the selected monitor. */
    QString m_monitorName;
    /** @brief Full monitor information for the selected display. */
    MonitorInfo m_monitor;
    /** @brief Current title text. */
    QString m_title;
    /** @brief Current title text colour. */
    QString m_titleColor = "#62A4C7";

    /** @brief Current orientation mode (0=landscape, >=1=vertical). */
    int m_mode = 0;
    /** @brief Logical canvas width. */
    int m_cw = 560;
    /** @brief Logical canvas height. */
    int m_ch = 315;
    /** @brief Index of the item currently being dragged, or -1. */
    int m_dragging = -1;
    /** @brief Index of the currently selected item, or -1. */
    int m_selected = -1;
    /** @brief Drag offset X from item origin. */
    int m_dragOffX = 0;
    /** @brief Drag offset Y from item origin. */
    int m_dragOffY = 0;
    /** @brief X offset to center the 16:9 canvas within the widget. */
    int m_offsetX = 0;
    /** @brief Y offset to center the 16:9 canvas within the widget. */
    int m_offsetY = 0;

    /** @brief Timer driving periodic screen capture refresh. */
    QTimer *m_refreshTimer;
    /** @brief Temporary file path for the latest screen capture. */
    QString m_pendingScreenPath;
    /** @brief Mutex protecting shared screen capture state. */
    QMutex m_mutex;
    /** @brief Last known frame rect for rescaling items on resize. */
    QRect m_lastFrameRect;

    static constexpr int WC_W = 160;              /**< Webcam capture width. */
    static constexpr int WC_H = 120;              /**< Webcam capture height. */
    static constexpr int WC_FPS = 10;             /**< Webcam capture frame rate. */
    static constexpr int WC_FRAME_SIZE = WC_W * WC_H * 3; /**< Raw RGB frame size in bytes. */
};
