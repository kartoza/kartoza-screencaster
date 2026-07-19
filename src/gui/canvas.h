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
     * @brief Add a new, independent text box overlay and select it.
     * @param text Initial text content.
     * @return Index of the newly added item.
     */
    int addTextBox(const QString &text);
    /** @brief Set the text content of a text item. */
    void setItemText(int index, const QString &text);
    /** @brief Set the font family of a text item. */
    void setItemFont(int index, const QString &family);
    /** @brief Set the font weight (400=normal, 700=bold) of a text item. */
    void setItemFontWeight(int index, int weight);
    /** @brief Set the colour of a text item (CSS-style "#RRGGBB"). */
    void setItemTextColor(int index, const QString &color);
    /** @brief Whether the item at the given index is a text item. */
    bool isTextItem(int index) const { return index >= 0 && index < m_items.size() && m_items[index].type == 3; }
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
     * @brief Add a sound effect layer (start or end).
     * @param filePath Path to the audio file.
     * @param isEnd If true, this is an end sound; otherwise a start sound.
     */
    void addSound(const QString &filePath, bool isEnd);
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
    /** @brief Toggle the user-initiated pause of the live screen preview. */
    void togglePreviewPause();
    /** @brief Whether the user has manually paused the live screen preview. */
    bool isPreviewPaused() const { return m_userPaused; }

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
        int cropTop = 0;    /**< Pixels cropped from top. */
        int cropBottom = 0; /**< Pixels cropped from bottom. */
        int cropLeft = 0;   /**< Pixels cropped from left. */
        int cropRight = 0;  /**< Pixels cropped from right. */
        QString fontFamily; /**< Text items: font family (empty = default "Sans"). */
        int fontWeight = 400; /**< Text items: font weight (400=normal, 700=bold). */
        QString textColor;  /**< Text items: colour (empty = canvas default). */
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
    /** @brief Update the screen item's position and crop from an export struct. */
    void updateScreenItem(const ItemExport &e);
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
    /**
     * @brief Emitted when the live preview pause state changes.
     * @param paused True if the preview is now paused, false if running.
     */
    void previewPausedChanged(bool paused);

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
    /** @brief Handle keyboard shortcuts (delete, arrow keys) and Alt-mode tracking. */
    void keyPressEvent(QKeyEvent *event) override;
    /** @brief Track Alt release to switch handles back to resize mode. */
    void keyReleaseEvent(QKeyEvent *event) override;
    /** @brief Recalculate canvas dimensions on resize. */
    void resizeEvent(QResizeEvent *event) override;
    /** @brief Track when the cursor enters the canvas (reveals the pause toggle). */
    void enterEvent(QEnterEvent *event) override;
    /** @brief Track when the cursor leaves the canvas (hides the pause toggle). */
    void leaveEvent(QEvent *event) override;
    /** @brief Accept drag enter for image/audio files. */
    void dragEnterEvent(QDragEnterEvent *event) override;
    /** @brief Track drag position to highlight drop targets. */
    void dragMoveEvent(QDragMoveEvent *event) override;
    /** @brief Hide drop targets when drag leaves. */
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    /** @brief Handle drop of image/audio files onto the canvas. */
    void dropEvent(QDropEvent *event) override;

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
        // Crop insets (pixels cropped from each edge)
        int cropTop = 0;    /**< Pixels cropped from the top edge. */
        int cropBottom = 0; /**< Pixels cropped from the bottom edge. */
        int cropLeft = 0;   /**< Pixels cropped from the left edge. */
        int cropRight = 0;  /**< Pixels cropped from the right edge. */
        // Text fields (type 3)
        QString fontFamily = "Sans"; /**< Font family for text items. */
        int fontWeight = 400;        /**< Font weight (400=normal, 700=bold). */
        QString textColor;           /**< Per-item text colour; empty falls back to m_titleColor. */
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
    /** @brief Start or stop the refresh timer to match the current gating flags. */
    void updateTimerState();
    /** @brief Whether a screen-capture layer currently exists on the canvas. */
    bool hasScreenItem() const;
    /** @brief Rect of the centre pause/continue toggle button, in canvas coords (empty if no screen item). */
    QRect previewToggleRect() const;
    /** @brief Draw the centre pause/continue toggle over the screen preview. */
    void drawPreviewToggle(QPainter &painter);
    /** @brief Draw the screen background and all overlay items. */
    void drawScreen(QPainter &painter);
    /** @brief Draw crop handles on a selected item. */
    void drawCropHandles(QPainter &painter, const CanvasItem &item);
    /**
     * @brief Test whether a point hits a canvas item.
     * @param item The item to test.
     * @param mx Mouse x in canvas coordinates.
     * @param my Mouse y in canvas coordinates.
     * @return True if the point is inside the item.
     */
    bool hitTest(const CanvasItem &item, int mx, int my) const;
    /** @brief Test which crop handle (if any) is hit at the given point. Returns 0=none, 1=top, 2=bottom, 3=left, 4=right. */
    int hitCropHandle(const CanvasItem &item, int mx, int my) const;
    /** @brief Test which resize (corner) handle is hit. Returns 0=none, 1=TL, 2=TR, 3=BL, 4=BR. */
    int hitResizeHandle(const CanvasItem &item, int mx, int my) const;
    /** @brief Draw the corner resize handles on a selected item. */
    void drawResizeHandles(QPainter &painter, const CanvasItem &item);
    /** @brief Whether the selected item's handles currently act as crop (Alt held) rather than resize. */
    bool cropModeActive() const;
    /**
     * @brief Snap a dragged item to the frame, other objects, and half-dimension guides.
     * @param item The item being dragged (its x/y are adjusted in place).
     *
     * Records the active guide lines in the snap-guide members so paintEvent can
     * draw them. Callers skip this when Shift is held (free placement).
     */
    void applySnapping(CanvasItem &item);
    /**
     * @brief Set an item's width and derive its height to preserve aspect.
     * @param item The item to resize.
     * @param newW Desired width (clamped per item type).
     * @param textHeight For text items only, the desired height; ignored (<0) otherwise.
     *
     * Shared by the scroll-wheel and handle-drag resize paths so both scale
     * items identically (aspect-locked, about the item centre).
     */
    void setItemWidthKeepingAspect(CanvasItem &item, int newW, int textHeight = -1);
    /** @brief Get the rect for a sound drop target (in canvas coords). */
    QRect soundDropTargetRect(bool isEnd) const;
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
    /** @brief Which crop handle is being dragged: 0=none, 1=top, 2=bottom, 3=left, 4=right. */
    int m_cropHandle = 0;
    /** @brief Index of item whose crop handle is being dragged. */
    int m_cropItem = -1;
    /** @brief Which resize handle is dragged: 0=none, 1=TL, 2=TR, 3=BL, 4=BR, 5=top, 6=bottom, 7=left, 8=right. */
    int m_resizeHandle = 0;
    /** @brief Index of item whose resize handle is being dragged. */
    int m_resizeItem = -1;
    /** @brief Item width at the start of a resize drag. */
    int m_resizeStartW = 0;
    /** @brief Item height at the start of a resize drag. */
    int m_resizeStartH = 0;
    /** @brief Fixed anchor point X (opposite the grabbed handle) during a resize drag. */
    int m_resizeAnchorX = 0;
    /** @brief Fixed anchor point Y (opposite the grabbed handle) during a resize drag. */
    int m_resizeAnchorY = 0;
    /** @brief Whether Alt is currently held (switches handles from resize to crop). */
    bool m_altActive = false;
    /** @brief Whether a vertical snap guide is active during the current drag. */
    bool m_snapXActive = false;
    /** @brief X position (canvas coords) of the active vertical snap guide. */
    int m_snapXLine = 0;
    /** @brief Whether a horizontal snap guide is active during the current drag. */
    bool m_snapYActive = false;
    /** @brief Y position (canvas coords) of the active horizontal snap guide. */
    int m_snapYLine = 0;
    /** @brief Whether audio drop targets are visible (during audio file drag). */
    bool m_showSoundDropTargets = false;
    /** @brief Which sound drop target is hovered: 0=none, 1=start(left), 2=end(right). */
    int m_soundDropHover = 0;
    /** @brief X offset to center the 16:9 canvas within the widget. */
    int m_offsetX = 0;
    /** @brief Y offset to center the 16:9 canvas within the widget. */
    int m_offsetY = 0;

    /** @brief Timer driving periodic screen capture refresh. */
    QTimer *m_refreshTimer;
    /** @brief True when previews are suspended by the app (window hidden, tab inactive, recording). */
    bool m_suspended = false;
    /** @brief True when the user has manually paused the live screen preview. */
    bool m_userPaused = false;
    /** @brief True while the cursor is over the canvas (used to reveal the pause toggle). */
    bool m_hovered = false;
    /** @brief Temporary file path for the latest screen capture. */
    QString m_pendingScreenPath;
    /** @brief Mutex protecting shared screen capture state. */
    QMutex m_mutex;
    /** @brief Last known frame rect for rescaling items on resize. */
    QRect m_lastFrameRect;

    static constexpr int CROP_HANDLE_SIZE = 6; /**< Radius of crop handle hit area. */
    static constexpr int RESIZE_HANDLE_SIZE = 7; /**< Radius of corner resize handle hit area. */
    static constexpr int WC_W = 160;              /**< Webcam capture width. */
    static constexpr int WC_H = 120;              /**< Webcam capture height. */
    static constexpr int WC_FPS = 10;             /**< Webcam capture frame rate. */
    static constexpr int WC_FRAME_SIZE = WC_W * WC_H * 3; /**< Raw RGB frame size in bytes. */
};
