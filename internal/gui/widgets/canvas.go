package widgets

import (
	"fmt"
	"io"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"sync"

	qt "github.com/mappu/miqt/qt6"
	"github.com/kartoza/kartoza-screencaster/internal/models"
	"github.com/kartoza/kartoza-screencaster/internal/webcam"
)

const (
	snapDist       = 15
	defaultBubbleR = 30 // radius in canvas coords
)

// CanvasItemType identifies what kind of draggable element this is
type CanvasItemType int

const (
	ItemWebcam CanvasItemType = iota
	ItemLogo
	ItemTitle
	// Legacy — kept for backward compat with config
	ItemLogoLeft   = 10
	ItemLogoRight  = 11
	ItemLogoBanner = 12
)

// SplitSide controls which side the screen goes on in vertical mode
type SplitSide int

const (
	SplitLeft  SplitSide = 0
	SplitRight SplitSide = 1
)

// WebcamShape controls how a webcam is drawn on the canvas
type WebcamShape int

const (
	ShapeRound  WebcamShape = 0
	ShapeSquare WebcamShape = 1
	ShapeRect   WebcamShape = 2
)

// canvasItem is a draggable element on the canvas
type canvasItem struct {
	itemType CanvasItemType
	label    string
	x, y     int         // center position in canvas coords
	w, h     int         // width/height in canvas coords
	circular bool        // draw as circle
	shape    WebcamShape // for webcam items
	pixmap   *qt.QPixmap // logo thumbnail or nil
	device   string      // webcam device name (for webcam items)
	logoPath string      // original logo file path
	visible  bool
}

// webcamCapture holds a live webcam frame reader
type webcamCapture struct {
	cmd      *exec.Cmd
	buf      []byte // latest frame (RGB24, 160x120)
	newFrame bool
	active   bool
}

const (
	wcPrevW = 160
	wcPrevH = 120
	wcFPS   = 10
	wcFrameSize = wcPrevW * wcPrevH * 3
)

// RecordingCanvas is a WYSIWYG preview of the final video output
type RecordingCanvas struct {
	widget *qt.QWidget
	items  []canvasItem

	// Screen background
	screenPixmap    *qt.QPixmap
	pendingScreenPath string // file path written by goroutine, loaded by timer
	monitor         *models.Monitor

	// Webcam live frames
	webcamCaptures map[string]*webcamCapture

	// Layout mode
	vertical  bool
	leftSplit bool
	splitSide SplitSide
	titleText string
	titleColor string

	// Selection/Drag state
	selectedItem int // highlighted item from layer list, -1 = none
	dragging     int
	dragOffX     int
	dragOffY     int

	// Live refresh
	refreshTimer *qt.QTimer
	refreshCount int
	mu           sync.Mutex

	// Current canvas dimensions (changes with landscape/vertical)
	cw, ch int

	onChange func()
}

// NewRecordingCanvas creates a new WYSIWYG canvas
func NewRecordingCanvas() *RecordingCanvas {
	c := &RecordingCanvas{
		widget:         qt.NewQWidget2(),
		selectedItem:   -1,
		dragging:       -1,
		cw:             560,
		ch:             315,
		webcamCaptures: make(map[string]*webcamCapture),
	}

	c.widget.SetMinimumSize2(400, 225)
	c.widget.SetMouseTracking(true)
	c.widget.SetStyleSheet("background: #11111b; border-radius: 8px;")

	// Use the widget's actual size for all calculations
	c.widget.OnResizeEvent(func(super func(event *qt.QResizeEvent), event *qt.QResizeEvent) {
		super(event)
		size := event.Size()
		c.cw = size.Width()
		c.ch = size.Height()
	})

	c.setupEvents()
	c.startScreenRefresh()

	return c
}

// SetMonitor sets which monitor to capture screenshots from
func (c *RecordingCanvas) SetMonitor(mon *models.Monitor) {
	c.mu.Lock()
	c.monitor = mon
	c.mu.Unlock()
	// Trigger immediate capture in background
	go c.captureScreen()
}

// SetVertical toggles between landscape and vertical preview
func (c *RecordingCanvas) SetVertical(vertical bool) {
	c.vertical = vertical
	c.widget.Update()
}

// SetLeftSplit sets left-split mode for vertical preview
func (c *RecordingCanvas) SetLeftSplit(leftSplit bool) {
	c.leftSplit = leftSplit
	c.widget.Update()
}

// SetSplitSide sets which side the screen appears on in vertical split mode
func (c *RecordingCanvas) SetSplitSide(side SplitSide) {
	c.splitSide = side
	c.widget.Update()
}

// SetTitle sets the title text shown on the canvas (draggable)
func (c *RecordingCanvas) SetTitle(text string) {
	c.titleText = text
	// Add or update the title item
	for i, item := range c.items {
		if item.itemType == ItemTitle {
			c.items[i].label = text
			c.widget.Update()
			return
		}
	}
	// No title item yet — add one at the bottom-center
	if text != "" {
		c.items = append(c.items, canvasItem{
			itemType: ItemTitle,
			label:    text,
			x:        c.cw / 2,
			y:        c.ch - 25,
			w:        200,
			h:        20,
			circular: false,
			visible:  true,
		})
	}
	c.widget.Update()
}

// SetTitleColor sets the title text color
func (c *RecordingCanvas) SetTitleColor(color string) {
	c.titleColor = color
	c.widget.Update()
}

// AddWebcam adds a draggable webcam bubble to the canvas
func (c *RecordingCanvas) AddWebcam(device, name string, index int) {
	c.AddWebcamWithShape(device, name, ShapeRound)
}

// AddWebcamWithShape adds a webcam with a specific shape
func (c *RecordingCanvas) AddWebcamWithShape(device, name string, shape WebcamShape) {
	r := defaultBubbleR
	// Count existing webcams for positioning
	count := 0
	for _, item := range c.items {
		if item.itemType == ItemWebcam {
			count++
		}
	}
	x := c.cw - r - 15 - count*(r*2+10)
	y := c.ch - r - 15

	w := r * 2
	h := r * 2
	circular := shape == ShapeRound
	if shape == ShapeRect {
		w = r * 3
		h = r * 2
	}

	c.items = append(c.items, canvasItem{
		itemType: ItemWebcam,
		label:    name,
		x:        x,
		y:        y,
		w:        w,
		h:        h,
		circular: circular,
		shape:    shape,
		device:   device,
		visible:  true,
	})

	// Start live capture for this webcam
	c.startWebcamCapture(device)

	c.widget.Update()
}

// startWebcamCapture starts a ffmpeg process to capture frames from a webcam device
func (c *RecordingCanvas) startWebcamCapture(device string) {
	if _, ok := c.webcamCaptures[device]; ok {
		return // already running
	}

	cap := &webcamCapture{
		buf:    make([]byte, wcFrameSize),
		active: true,
	}

	args := []string{
		"-f", "v4l2",
		"-framerate", fmt.Sprintf("%d", wcFPS),
		"-i", "/dev/" + device,
		"-vf", fmt.Sprintf("scale=%d:%d", wcPrevW, wcPrevH),
		"-f", "rawvideo",
		"-pix_fmt", "rgb24",
		"-an",
		"pipe:1",
	}

	cap.cmd = exec.Command("ffmpeg", args...)
	cap.cmd.Stderr = nil

	stdout, err := cap.cmd.StdoutPipe()
	if err != nil {
		return
	}
	if err := cap.cmd.Start(); err != nil {
		return
	}

	c.webcamCaptures[device] = cap

	// Read frames in background
	go func() {
		readBuf := make([]byte, wcFrameSize)
		for {
			c.mu.Lock()
			active := cap.active
			c.mu.Unlock()
			if !active {
				break
			}
			_, err := io.ReadFull(stdout, readBuf)
			if err != nil {
				break
			}
			c.mu.Lock()
			copy(cap.buf, readBuf)
			cap.newFrame = true
			c.mu.Unlock()
		}
	}()
}

// stopWebcamCapture stops a webcam capture process
func (c *RecordingCanvas) stopWebcamCapture(device string) {
	cap, ok := c.webcamCaptures[device]
	if !ok {
		return
	}
	cap.active = false
	if cap.cmd != nil && cap.cmd.Process != nil {
		_ = cap.cmd.Process.Kill()
		_ = cap.cmd.Wait()
	}
	delete(c.webcamCaptures, device)
}

// stopAllWebcams stops all webcam captures
func (c *RecordingCanvas) stopAllWebcams() {
	for device := range c.webcamCaptures {
		c.stopWebcamCapture(device)
	}
}

// AddLogo adds a draggable logo to the canvas. Can be called multiple times
// for unlimited logos. Each new logo is placed with a slight offset.
func (c *RecordingCanvas) AddLogo(itemType CanvasItemType, path string) {
	if path == "" {
		return
	}

	pixmap := qt.NewQPixmap4(path)
	if pixmap.IsNull() {
		return
	}

	// Count existing logos for offset
	logoCount := 0
	for _, item := range c.items {
		if item.itemType == ItemLogo || item.itemType == ItemLogoLeft ||
			item.itemType == ItemLogoRight || item.itemType == ItemLogoBanner {
			logoCount++
		}
	}

	// Scale to ~1/6 of canvas width
	w := c.cw / 6
	if w < 40 {
		w = 40
	}
	h := w * pixmap.Height() / pixmap.Width()
	if h < 15 {
		h = 15
	}

	// Place at top-left with offset per logo
	x := w/2 + 10 + logoCount*(w+10)
	y := h/2 + 10
	// Wrap to next row if too far right
	if x+w/2 > c.cw {
		x = w/2 + 10
		y = h + 20 + h/2
	}

	c.items = append(c.items, canvasItem{
		itemType: ItemLogo,
		label:    filepath.Base(path),
		x:        x,
		y:        y,
		w:        w,
		h:        h,
		circular: false,
		pixmap:   pixmap,
		logoPath: path,
		visible:  true,
	})
	c.widget.Update()
}

// RemoveLogo removes a logo by type (removes last match)
func (c *RecordingCanvas) RemoveLogo(itemType CanvasItemType) {
	for i := len(c.items) - 1; i >= 0; i-- {
		if c.items[i].itemType == itemType || c.items[i].itemType == ItemLogo {
			c.items = append(c.items[:i], c.items[i+1:]...)
			break // remove only one
		}
	}
	c.widget.Update()
}

// ClearAll removes all items
func (c *RecordingCanvas) ClearAll() {
	c.items = nil
	c.widget.Update()
}

func (c *RecordingCanvas) setupEvents() {
	c.widget.OnPaintEvent(func(super func(event *qt.QPaintEvent), event *qt.QPaintEvent) {
		c.paint()
	})

	c.widget.OnMousePressEvent(func(super func(event *qt.QMouseEvent), event *qt.QMouseEvent) {
		pos := event.Position()
		mx, my := int(pos.X()), int(pos.Y())
		for i := len(c.items) - 1; i >= 0; i-- {
			item := c.items[i]
			if !item.visible {
				continue
			}
			if c.hitTest(item, mx, my) {
				c.dragging = i
				c.dragOffX = mx - item.x
				c.dragOffY = my - item.y
				break
			}
		}
	})

	c.widget.OnMouseMoveEvent(func(super func(event *qt.QMouseEvent), event *qt.QMouseEvent) {
		if c.dragging < 0 {
			return
		}
		pos := event.Position()
		mx, my := int(pos.X()), int(pos.Y())
		item := &c.items[c.dragging]

		newX := mx - c.dragOffX
		newY := my - c.dragOffY

		// Clamp
		halfW := item.w / 2
		halfH := item.h / 2
		if newX-halfW < 0 {
			newX = halfW
		}
		if newX+halfW > c.cw {
			newX = c.cw - halfW
		}
		if newY-halfH < 0 {
			newY = halfH
		}
		if newY+halfH > c.ch {
			newY = c.ch - halfH
		}

		// Snap
		newX, newY = c.applySnap(newX, newY, halfW, halfH)

		item.x = newX
		item.y = newY
		c.widget.Update()
	})

	c.widget.OnMouseReleaseEvent(func(super func(event *qt.QMouseEvent), event *qt.QMouseEvent) {
		if c.dragging >= 0 {
			c.dragging = -1
			if c.onChange != nil {
				c.onChange()
			}
		}
	})

	// Key press: Delete removes selected item
	c.widget.OnKeyPressEvent(func(super func(event *qt.QKeyEvent), event *qt.QKeyEvent) {
		if event.Key() == int(qt.Key_Delete) && c.selectedItem >= 0 {
			c.RemoveItem(c.selectedItem)
			c.selectedItem = -1
			if c.onChange != nil {
				c.onChange()
			}
		} else {
			super(event)
		}
	})

	// Mouse wheel: resize item under cursor
	c.widget.OnWheelEvent(func(super func(event *qt.QWheelEvent), event *qt.QWheelEvent) {
		pos := event.Position()
		mx, my := int(pos.X()), int(pos.Y())
		delta := event.AngleDelta().Y()

		// Find item under cursor
		for i := len(c.items) - 1; i >= 0; i-- {
			item := &c.items[i]
			if !item.visible {
				continue
			}
			if c.hitTest(*item, mx, my) {
				// Resize by 5px per scroll notch
				scale := 5
				if delta > 0 {
					item.w += scale
					item.h += scale
				} else if delta < 0 {
					item.w -= scale
					item.h -= scale
				}
				// Minimum size
				if item.w < 20 {
					item.w = 20
				}
				if item.h < 15 {
					item.h = 15
				}
				// For rectangles, maintain aspect ratio
				if item.shape == ShapeRect && item.itemType == ItemWebcam {
					item.h = item.w * 2 / 3
				}
				c.widget.Update()
				break
			}
		}
	})
}

func (c *RecordingCanvas) hitTest(item canvasItem, mx, my int) bool {
	if item.circular {
		dx := mx - item.x
		dy := my - item.y
		r := item.w / 2
		return dx*dx+dy*dy <= r*r
	}
	return mx >= item.x-item.w/2 && mx <= item.x+item.w/2 &&
		my >= item.y-item.h/2 && my <= item.y+item.h/2
}

func (c *RecordingCanvas) applySnap(x, y, halfW, halfH int) (int, int) {
	margin := 8

	// Edge snap
	if x-halfW < snapDist+margin {
		x = halfW + margin
	} else if c.cw-x-halfW < snapDist+margin {
		x = c.cw - halfW - margin
	}
	if y-halfH < snapDist+margin {
		y = halfH + margin
	} else if c.ch-y-halfH < snapDist+margin {
		y = c.ch - halfH - margin
	}

	// Corner snap
	corners := [][2]int{
		{halfW + margin, halfH + margin},
		{c.cw - halfW - margin, halfH + margin},
		{halfW + margin, c.ch - halfH - margin},
		{c.cw - halfW - margin, c.ch - halfH - margin},
	}
	for _, corner := range corners {
		dx := x - corner[0]
		dy := y - corner[1]
		if int(math.Sqrt(float64(dx*dx+dy*dy))) < snapDist*2 {
			return corner[0], corner[1]
		}
	}

	return x, y
}

func (c *RecordingCanvas) paint() {
	painter := qt.NewQPainter2(c.widget.QPaintDevice)
	defer painter.End()

	// Background
	painter.FillRect5(0, 0, c.cw, c.ch, qt.NewQColor3(17, 17, 27))

	// Draw screen and mode overlays
	c.drawScreen(painter)

	// Draw items
	for i, item := range c.items {
		if !item.visible {
			continue
		}
		isDragging := i == c.dragging
		isSelected := i == c.selectedItem

		if item.pixmap != nil {
			// Logo: draw the pixmap
			targetRect := qt.NewQRect4(item.x-item.w/2, item.y-item.h/2, item.w, item.h)
			painter.DrawPixmap10(targetRect, item.pixmap)
			if isDragging {
				outlinePen := qt.NewQPen3(qt.NewQColor3(137, 180, 250))
				painter.SetPenWithPen(outlinePen)
				painter.SetBrushWithStyle(qt.NoBrush)
				painter.DrawRect2(item.x-item.w/2, item.y-item.h/2, item.w, item.h)
			}
		} else if item.itemType == ItemWebcam {
			// Webcam — draw live frame if available, else colored shape
			c.mu.Lock()
			cap, hasCapture := c.webcamCaptures[item.device]
			var framePixmap *qt.QPixmap
			if hasCapture && cap.newFrame {
				img := qt.NewQImage6(&cap.buf[0], wcPrevW, wcPrevH, int64(wcPrevW*3), qt.QImage__Format_RGB888)
				framePixmap = qt.QPixmap_FromImage(img)
				cap.newFrame = false
			}
			c.mu.Unlock()

			outlinePen := qt.NewQPen3(qt.NewQColor3(205, 214, 244))
			if isDragging {
				outlinePen = qt.NewQPen3(qt.NewQColor3(137, 180, 250))
			}
			painter.SetPenWithPen(outlinePen)

			switch item.shape {
			case ShapeRound:
				r := item.w / 2
				if framePixmap != nil && !framePixmap.IsNull() {
					// Clip to circle using save/restore and clip path
					painter.Save()
					path := qt.NewQPainterPath()
					path.AddEllipse2(float64(item.x-r), float64(item.y-r), float64(item.w), float64(item.h))
					painter.SetClipPath(path)
					targetRect := qt.NewQRect4(item.x-r, item.y-r, item.w, item.h)
					painter.DrawPixmap10(targetRect, framePixmap)
					painter.Restore()
				} else {
					painter.SetBrush(qt.NewQBrush3(qt.NewQColor3(166, 227, 161)))
				}
				painter.SetBrushWithStyle(qt.NoBrush)
				painter.DrawEllipse2(item.x-r, item.y-r, item.w, item.h)
			case ShapeSquare:
				r := item.w / 2
				if framePixmap != nil && !framePixmap.IsNull() {
					targetRect := qt.NewQRect4(item.x-r, item.y-r, item.w, item.w)
					painter.DrawPixmap10(targetRect, framePixmap)
				} else {
					painter.SetBrush(qt.NewQBrush3(qt.NewQColor3(166, 227, 161)))
					painter.DrawRect2(item.x-r, item.y-r, item.w, item.w)
				}
				painter.SetBrushWithStyle(qt.NoBrush)
				painter.DrawRect2(item.x-r, item.y-r, item.w, item.w)
			case ShapeRect:
				if framePixmap != nil && !framePixmap.IsNull() {
					targetRect := qt.NewQRect4(item.x-item.w/2, item.y-item.h/2, item.w, item.h)
					painter.DrawPixmap10(targetRect, framePixmap)
				} else {
					painter.SetBrush(qt.NewQBrush3(qt.NewQColor3(166, 227, 161)))
					painter.DrawRect2(item.x-item.w/2, item.y-item.h/2, item.w, item.h)
				}
				painter.SetBrushWithStyle(qt.NoBrush)
				painter.DrawRect2(item.x-item.w/2, item.y-item.h/2, item.w, item.h)
			}

			// Label below
			labelPen := qt.NewQPen3(qt.NewQColor3(205, 214, 244))
			painter.SetPenWithPen(labelPen)
			name := item.label
			if len(name) > 10 {
				name = name[:10]
			}
			painter.DrawText2(qt.NewQPoint2(item.x-item.w/3, item.y+item.h/2+12), name)
		} else if item.itemType == ItemTitle {
			// Title text — draw as styled text
			titleFont := qt.NewQFont6("Sans", 10)
			painter.SetFont(titleFont)
			color := c.titleColor
			if color == "" {
				color = "#62A4C7"
			}
			if isDragging {
				painter.SetPen(qt.NewQColor3(137, 180, 250))
			} else {
				painter.SetPen(qt.NewQColor6(color))
			}
			painter.DrawText2(qt.NewQPoint2(item.x-item.w/2, item.y+item.h/4), item.label)
			// Draw a subtle bounding box
			if isDragging {
				painter.SetBrushWithStyle(qt.NoBrush)
				painter.SetPenWithPen(qt.NewQPen3(qt.NewQColor3(137, 180, 250)))
				painter.DrawRect2(item.x-item.w/2-2, item.y-item.h/2-2, item.w+4, item.h+4)
			}
		} else {
			// Rectangle item
			if isDragging {
				painter.SetBrush(qt.NewQBrush3(qt.NewQColor3(137, 180, 250)))
			} else {
				painter.SetBrush(qt.NewQBrush3(qt.NewQColor3(166, 227, 161)))
			}
			outlinePen := qt.NewQPen3(qt.NewQColor3(205, 214, 244))
			painter.SetPenWithPen(outlinePen)
			painter.DrawRect2(item.x-item.w/2, item.y-item.h/2, item.w, item.h)
		}

		// Selection highlight
		if isSelected && !isDragging {
			selPen := qt.NewQPen3(qt.NewQColor3(249, 226, 175)) // yellow
			painter.SetPenWithPen(selPen)
			painter.SetBrushWithStyle(qt.NoBrush)
			painter.DrawRect2(item.x-item.w/2-3, item.y-item.h/2-3, item.w+6, item.h+6)
		}
	}

	// Snap guides while dragging
	if c.dragging >= 0 {
		guidePen := qt.NewQPen3(qt.NewQColor3(137, 180, 250))
		painter.SetPenWithPen(guidePen)
		painter.SetBrushWithStyle(qt.NoBrush)
		margin := 8
		for _, corner := range [][2]int{
			{margin, margin}, {c.cw - margin, margin},
			{margin, c.ch - margin}, {c.cw - margin, c.ch - margin},
		} {
			painter.DrawRect2(corner[0]-3, corner[1]-3, 6, 6)
		}
	}

	// Mode label
	modePen := qt.NewQPen3(qt.NewQColor3(108, 112, 134))
	painter.SetPenWithPen(modePen)
	mode := "Landscape 16:9"
	if c.vertical {
		mode = "Vertical 9:16"
		if c.leftSplit {
			mode = "Vertical 9:16 (Left Split)"
		}
	}
	painter.DrawText2(qt.NewQPoint2(5, c.ch-5), mode)
}

// drawScreen renders the screen screenshot and mode overlays
func (c *RecordingCanvas) drawScreen(painter *qt.QPainter) {
	hasScreen := c.screenPixmap != nil && !c.screenPixmap.IsNull()

	if !c.vertical {
		// === MODE 1: Landscape 16:9 ===
		// Screen fills the entire canvas
		if hasScreen {
			target := qt.NewQRect4(0, 0, c.cw, c.ch)
			painter.DrawPixmap10(target, c.screenPixmap)
		} else {
			c.drawScreenPlaceholder(painter, 0, 0, c.cw, c.ch)
		}
	} else if c.vertical && !c.leftSplit {
		// === MODE 2: Vertical 9:16 ===
		// 9:16 frame centered. Screen fills top of frame (full frame width, 16:9 height)
		fx, fy, fw, fh := c.verticalFrame()

		// Dim outside frame
		c.dimOutsideFrame(painter, fx, fy, fw, fh)

		// Screen at top of frame: full frame width, height maintains 16:9 from source
		screenH := fw * 9 / 16 // 16:9 source scaled to frame width
		if hasScreen {
			target := qt.NewQRect4(fx, fy, fw, screenH)
			painter.DrawPixmap10(target, c.screenPixmap)
		} else {
			c.drawScreenPlaceholder(painter, fx, fy, fw, screenH)
		}

		// White space below screen within frame
		belowY := fy + screenH
		belowH := fh - screenH
		if belowH > 0 {
			painter.FillRect5(fx, belowY, fw, belowH, qt.NewQColor3(255, 255, 255))
		}

		// Frame border
		c.drawFrameBorder(painter, fx, fy, fw, fh, "9:16 Vertical")

	} else if c.leftSplit && c.splitSide == SplitLeft {
		// === MODE 3: Vertical Left Split ===
		// Left half of screen scaled so:
		// - top-left of screen = top-left of output frame
		// - horizontal midpoint of screen = right edge of output frame
		// Source: left half of pixmap. Target: full frame width, proportional height.
		fx, fy, fw, fh := c.verticalFrame()

		c.dimOutsideFrame(painter, fx, fy, fw, fh)

		// Left half of 16:9 screen is 8:9 aspect (half width, full height)
		// Scaled to fill frame width: height = fw * 9/8
		screenH := fw * 9 / 8
		if screenH > fh {
			screenH = fh
		}

		if hasScreen {
			// Source: left half of the pixmap
			pw := c.screenPixmap.Width()
			ph := c.screenPixmap.Height()
			sourceRect := qt.NewQRect4(0, 0, pw/2, ph)
			targetRect := qt.NewQRect4(fx, fy, fw, screenH)
			painter.DrawPixmap2(targetRect, c.screenPixmap, sourceRect)
		} else {
			c.drawScreenPlaceholder(painter, fx, fy, fw, screenH)
		}

		// White space below
		belowY := fy + screenH
		belowH := fh - screenH
		if belowH > 0 {
			painter.FillRect5(fx, belowY, fw, belowH, qt.NewQColor3(255, 255, 255))
		}

		c.drawFrameBorder(painter, fx, fy, fw, fh, "9:16 (Left Split)")

	} else if c.leftSplit && c.splitSide == SplitRight {
		// === MODE 4: Vertical Right Split ===
		// Right half of screen scaled so:
		// - top-center of screen = top-left of output frame
		// - bottom-right of screen = right edge of output frame (proportionally)
		fx, fy, fw, fh := c.verticalFrame()

		c.dimOutsideFrame(painter, fx, fy, fw, fh)

		// Right half of 16:9 screen is 8:9 aspect
		screenH := fw * 9 / 8
		if screenH > fh {
			screenH = fh
		}

		if hasScreen {
			// Source: right half of the pixmap
			pw := c.screenPixmap.Width()
			ph := c.screenPixmap.Height()
			sourceRect := qt.NewQRect4(pw/2, 0, pw/2, ph)
			targetRect := qt.NewQRect4(fx, fy, fw, screenH)
			painter.DrawPixmap2(targetRect, c.screenPixmap, sourceRect)
		} else {
			c.drawScreenPlaceholder(painter, fx, fy, fw, screenH)
		}

		// White space below
		belowY := fy + screenH
		belowH := fh - screenH
		if belowH > 0 {
			painter.FillRect5(fx, belowY, fw, belowH, qt.NewQColor3(255, 255, 255))
		}

		c.drawFrameBorder(painter, fx, fy, fw, fh, "9:16 (Right Split)")
	}

	// Canvas border
	borderPen := qt.NewQPen3(qt.NewQColor3(69, 71, 90))
	painter.SetPenWithPen(borderPen)
	painter.SetBrushWithStyle(qt.NoBrush)
	painter.DrawRect2(0, 0, c.cw-1, c.ch-1)
}

// drawScreenPlaceholder draws a placeholder when no screenshot is available
func (c *RecordingCanvas) drawScreenPlaceholder(painter *qt.QPainter, x, y, w, h int) {
	painter.FillRect5(x, y, w, h, qt.NewQColor3(30, 30, 46))
	pen := qt.NewQPen3(qt.NewQColor3(69, 71, 90))
	painter.SetPenWithPen(pen)
	painter.DrawRect2(x, y, w, h)
	textPen := qt.NewQPen3(qt.NewQColor3(108, 112, 134))
	painter.SetPenWithPen(textPen)
	painter.DrawText2(qt.NewQPoint2(x+w/2-20, y+h/2), "Screen")
}

// dimOutsideFrame dims everything outside the vertical output frame
func (c *RecordingCanvas) dimOutsideFrame(painter *qt.QPainter, fx, fy, fw, fh int) {
	painter.SetOpacity(0.6)
	dim := qt.NewQColor3(0, 0, 0)
	painter.FillRect5(0, 0, fx, c.ch, dim)                      // left
	painter.FillRect5(fx+fw, 0, c.cw-fx-fw, c.ch, dim)          // right
	painter.FillRect5(fx, 0, fw, fy, dim)                        // above
	painter.FillRect5(fx, fy+fh, fw, c.ch-fy-fh, dim)           // below
	painter.SetOpacity(1.0)
}

// drawFrameBorder draws the output frame border and label
func (c *RecordingCanvas) drawFrameBorder(painter *qt.QPainter, fx, fy, fw, fh int, label string) {
	framePen := qt.NewQPen3(qt.NewQColor3(137, 180, 250))
	painter.SetPenWithPen(framePen)
	painter.SetBrushWithStyle(qt.NoBrush)
	painter.DrawRect2(fx, fy, fw, fh)
	painter.DrawText2(qt.NewQPoint2(fx+5, fy+fh+14), label)
}

// screenArea returns where the screen content should be drawn.
// In all modes the screen fills the full canvas width, with height
// calculated to maintain 16:9 aspect ratio.
func (c *RecordingCanvas) screenArea() (x, y, w, h int) {
	// Screen always fills full canvas width, height preserves 16:9
	screenH := c.cw * 9 / 16
	if screenH > c.ch {
		screenH = c.ch
	}
	return 0, 0, c.cw, screenH
}

// verticalFrame returns the 9:16 output frame dimensions within the canvas
func (c *RecordingCanvas) verticalFrame() (x, y, w, h int) {
	frameH := c.ch - 20
	frameW := frameH * 9 / 16
	if frameW > c.cw-20 {
		frameW = c.cw - 20
		frameH = frameW * 16 / 9
	}
	frameX := (c.cw - frameW) / 2
	frameY := (c.ch - frameH) / 2
	return frameX, frameY, frameW, frameH
}

func (c *RecordingCanvas) startScreenRefresh() {
	c.refreshTimer = qt.NewQTimer()
	c.refreshTimer.SetInterval(100) // 10Hz for smooth webcam frames + periodic screen refresh
	c.refreshTimer.OnTimeout(func() {
		// Check if background goroutine captured a new screenshot
		c.mu.Lock()
		path := c.pendingScreenPath
		c.pendingScreenPath = ""
		c.mu.Unlock()

		if path != "" {
			// Load pixmap on the main thread (safe for Qt)
			pixmap := qt.NewQPixmap4(path)
			_ = os.Remove(path)
			if !pixmap.IsNull() {
				c.screenPixmap = pixmap
				c.widget.Update()
			}
		}

		// Also repaint for webcam frame updates
		hasNewWebcamFrame := false
		c.mu.Lock()
		for _, cap := range c.webcamCaptures {
			if cap.newFrame {
				hasNewWebcamFrame = true
				break
			}
		}
		c.mu.Unlock()
		if hasNewWebcamFrame {
			c.widget.Update()
		}

		// Capture screen every 20 ticks (~2 seconds)
		c.refreshCount++
		if c.refreshCount >= 20 {
			c.refreshCount = 0
			go c.captureScreen()
		}
	})
	c.refreshTimer.Start2()
}

func (c *RecordingCanvas) captureScreen() {
	c.mu.Lock()
	mon := c.monitor
	c.mu.Unlock()

	if mon == nil || mon.Name == "" {
		return
	}

	tmpPath := filepath.Join(os.TempDir(), fmt.Sprintf("kartoza-canvas-%s.png", mon.Name))
	cmd := exec.Command("grim", "-o", mon.Name, "-t", "png", "-l", "0", tmpPath)
	cmd.Stderr = nil
	cmd.Stdout = nil
	if err := cmd.Run(); err != nil {
		return
	}

	// Store the path — the main thread timer will create the QPixmap
	c.mu.Lock()
	c.pendingScreenPath = tmpPath
	c.mu.Unlock()
}

// Widget returns the underlying QWidget
func (c *RecordingCanvas) Widget() *qt.QWidget {
	return c.widget
}

// OnChange sets a callback for when positions change
func (c *RecordingCanvas) OnChange(cb func()) {
	c.onChange = cb
}

// Stop stops the screen refresh timer and all webcam captures
func (c *RecordingCanvas) Stop() {
	if c.refreshTimer != nil {
		c.refreshTimer.Stop()
	}
	c.stopAllWebcams()
}

// HasWebcams returns true if any webcam items are on the canvas
func (c *RecordingCanvas) HasWebcams() bool {
	for _, item := range c.items {
		if item.itemType == ItemWebcam && item.visible {
			return true
		}
	}
	return false
}

// IsVertical returns whether the canvas is in vertical mode
func (c *RecordingCanvas) IsVertical() bool {
	return c.vertical
}

// GetLogoPaths returns a map of logo item types to file paths.
// For unlimited logos, all are stored under ItemLogo key as a comma-separated list isn't ideal,
// so this returns the first three logos mapped to legacy left/right/banner positions.
func (c *RecordingCanvas) GetLogoPaths() map[CanvasItemType]string {
	paths := make(map[CanvasItemType]string)
	logoIdx := 0
	legacyKeys := []CanvasItemType{ItemLogoLeft, ItemLogoRight, ItemLogoBanner}
	for _, item := range c.items {
		if (item.itemType == ItemLogo || item.itemType == ItemLogoLeft ||
			item.itemType == ItemLogoRight || item.itemType == ItemLogoBanner) &&
			item.logoPath != "" && item.visible {
			if logoIdx < len(legacyKeys) {
				paths[legacyKeys[logoIdx]] = item.logoPath
			}
			logoIdx++
		}
	}
	return paths
}

// GetAllLogoPaths returns all logo file paths
func (c *RecordingCanvas) GetAllLogoPaths() []string {
	var paths []string
	for _, item := range c.items {
		if (item.itemType == ItemLogo || item.itemType == ItemLogoLeft ||
			item.itemType == ItemLogoRight || item.itemType == ItemLogoBanner) &&
			item.logoPath != "" && item.visible {
			paths = append(paths, item.logoPath)
		}
	}
	return paths
}

// SetSelectedItem highlights an item on the canvas (e.g., from layer list selection)
func (c *RecordingCanvas) SetSelectedItem(index int) {
	c.selectedItem = index
	c.widget.Update()
}

// RemoveItem removes an item by index and stops its webcam capture if applicable
func (c *RecordingCanvas) RemoveItem(index int) {
	if index < 0 || index >= len(c.items) {
		return
	}
	item := c.items[index]
	if item.itemType == ItemWebcam {
		c.stopWebcamCapture(item.device)
	}
	c.items = append(c.items[:index], c.items[index+1:]...)
	c.widget.Update()
}

// SwapItems swaps two items by index (for z-order changes)
func (c *RecordingCanvas) SwapItems(i, j int) {
	if i < 0 || j < 0 || i >= len(c.items) || j >= len(c.items) {
		return
	}
	c.items[i], c.items[j] = c.items[j], c.items[i]
	c.widget.Update()
}

// ItemNames returns the display names of all items (for the layer list)
func (c *RecordingCanvas) ItemNames() []string {
	var names []string
	for _, item := range c.items {
		names = append(names, item.label)
	}
	return names
}

// FindItemByName returns the index of the first item with the given name, or -1
func (c *RecordingCanvas) FindItemByName(name string) int {
	for i, item := range c.items {
		if item.label == name {
			return i
		}
	}
	return -1
}

// ReorderItems reorders items based on the given index order
func (c *RecordingCanvas) ReorderItems(order []int) {
	if len(order) != len(c.items) {
		return
	}
	newItems := make([]canvasItem, len(c.items))
	for i, oldIdx := range order {
		if oldIdx >= 0 && oldIdx < len(c.items) {
			newItems[i] = c.items[oldIdx]
		}
	}
	c.items = newItems
	c.widget.Update()
}

// GetWebcamPositions returns webcam positions scaled to video coordinates (1920x1080)
func (c *RecordingCanvas) GetWebcamPositions() []webcam.WebcamConfig {
	var configs []webcam.WebcamConfig
	for _, item := range c.items {
		if item.itemType == ItemWebcam {
			configs = append(configs, webcam.WebcamConfig{
				Device:      item.device,
				OverlayX:    item.x * 1920 / c.cw,
				OverlayY:    item.y * 1080 / c.ch,
				OverlaySize: item.w * 1920 / c.cw,
			})
		}
	}
	return configs
}
