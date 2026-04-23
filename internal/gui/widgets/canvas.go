package widgets

import (
	"fmt"
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

// RecordingCanvas is a WYSIWYG preview of the final video output
type RecordingCanvas struct {
	widget *qt.QWidget
	items  []canvasItem

	// Screen background
	screenPixmap *qt.QPixmap
	monitor      *models.Monitor

	// Layout mode
	vertical  bool
	leftSplit bool
	splitSide SplitSide
	titleText string
	titleColor string

	// Drag state
	dragging int
	dragOffX int
	dragOffY int

	// Live refresh
	refreshTimer *qt.QTimer
	mu           sync.Mutex

	// Current canvas dimensions (changes with landscape/vertical)
	cw, ch int

	onChange func()
}

// NewRecordingCanvas creates a new WYSIWYG canvas
func NewRecordingCanvas() *RecordingCanvas {
	c := &RecordingCanvas{
		widget:   qt.NewQWidget2(),
		dragging: -1,
		cw:       560,
		ch:       315,
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
	c.captureScreen()
	c.widget.Update()
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
	c.widget.Update()
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

	// Screen area
	screenX, screenY, screenW, screenH := c.screenArea()

	c.mu.Lock()
	hasScreen := c.screenPixmap != nil && !c.screenPixmap.IsNull()
	c.mu.Unlock()

	if hasScreen {
		c.mu.Lock()
		// Draw the screen screenshot
		targetRect := qt.NewQRect4(screenX, screenY, screenW, screenH)
		painter.DrawPixmap10(targetRect, c.screenPixmap)
		c.mu.Unlock()
	} else {
		// Draw placeholder
		painter.FillRect5(screenX, screenY, screenW, screenH, qt.NewQColor3(30, 30, 46))
		placeholderPen := qt.NewQPen3(qt.NewQColor3(69, 71, 90))
		painter.SetPenWithPen(placeholderPen)
		painter.DrawRect2(screenX, screenY, screenW, screenH)
		textPen := qt.NewQPen3(qt.NewQColor3(108, 112, 134))
		painter.SetPenWithPen(textPen)
		painter.DrawText2(qt.NewQPoint2(screenX+screenW/2-20, screenY+screenH/2), "Screen")
	}

	// Canvas border
	borderPen := qt.NewQPen3(qt.NewQColor3(69, 71, 90))
	painter.SetPenWithPen(borderPen)
	painter.SetBrushWithStyle(qt.NoBrush)
	painter.DrawRect2(0, 0, c.cw-1, c.ch-1)

	// Split crop overlay — dim the side that won't be used in vertical video
	if c.vertical && c.leftSplit {
		dimBrush := qt.NewQColor3(0, 0, 0)
		if c.splitSide == SplitLeft {
			// Dim right half
			painter.SetOpacity(0.5)
			painter.FillRect5(c.cw/2, 0, c.cw/2, c.ch, dimBrush)
			painter.SetOpacity(1.0)
			// Draw crop guide line
			guidePen := qt.NewQPen3(qt.NewQColor3(137, 180, 250))
			painter.SetPenWithPen(guidePen)
			painter.DrawLine2(c.cw/2, 0, c.cw/2, c.ch)
		} else {
			// Dim left half
			painter.SetOpacity(0.5)
			painter.FillRect5(0, 0, c.cw/2, c.ch, dimBrush)
			painter.SetOpacity(1.0)
			guidePen := qt.NewQPen3(qt.NewQColor3(137, 180, 250))
			painter.SetPenWithPen(guidePen)
			painter.DrawLine2(c.cw/2, 0, c.cw/2, c.ch)
		}
	}

	// Draw items
	for i, item := range c.items {
		if !item.visible {
			continue
		}
		isDragging := i == c.dragging

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
			// Webcam — shape depends on mode
			if isDragging {
				painter.SetBrush(qt.NewQBrush3(qt.NewQColor3(137, 180, 250)))
			} else {
				painter.SetBrush(qt.NewQBrush3(qt.NewQColor3(166, 227, 161)))
			}
			outlinePen := qt.NewQPen3(qt.NewQColor3(205, 214, 244))
			painter.SetPenWithPen(outlinePen)

			switch item.shape {
			case ShapeRound:
				r := item.w / 2
				painter.DrawEllipse2(item.x-r, item.y-r, item.w, item.h)
			case ShapeSquare:
				r := item.w / 2
				painter.DrawRect2(item.x-r, item.y-r, item.w, item.w)
			case ShapeRect:
				painter.DrawRect2(item.x-item.w/2, item.y-item.h/2, item.w, item.h)
			}

			// Label inside
			labelPen := qt.NewQPen3(qt.NewQColor3(30, 30, 46))
			painter.SetPenWithPen(labelPen)
			name := item.label
			if len(name) > 6 {
				name = name[:6]
			}
			painter.DrawText2(qt.NewQPoint2(item.x-item.w/4, item.y+4), name)
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

// screenArea returns where the screen content should be drawn.
// Screen always fills the full canvas width. In vertical + split modes,
// a crop overlay is drawn to show which half will be used.
func (c *RecordingCanvas) screenArea() (x, y, w, h int) {
	// Screen always fills the full canvas
	return 0, 0, c.cw, c.ch
}

func (c *RecordingCanvas) startScreenRefresh() {
	c.refreshTimer = qt.NewQTimer()
	c.refreshTimer.SetInterval(2000)
	c.refreshTimer.OnTimeout(func() {
		go func() {
			c.captureScreen()
			// Schedule repaint on main thread
			t := qt.NewQTimer()
			t.SetSingleShot(true)
			t.OnTimeout(func() { c.widget.Update() })
			t.Start(0)
		}()
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

	pixmap := qt.NewQPixmap4(tmpPath)
	_ = os.Remove(tmpPath)

	if !pixmap.IsNull() {
		c.mu.Lock()
		c.screenPixmap = pixmap
		c.mu.Unlock()
	}
}

// Widget returns the underlying QWidget
func (c *RecordingCanvas) Widget() *qt.QWidget {
	return c.widget
}

// OnChange sets a callback for when positions change
func (c *RecordingCanvas) OnChange(cb func()) {
	c.onChange = cb
}

// Stop stops the screen refresh timer
func (c *RecordingCanvas) Stop() {
	if c.refreshTimer != nil {
		c.refreshTimer.Stop()
	}
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
