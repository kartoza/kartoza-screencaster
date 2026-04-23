package widgets

import (
	"fmt"
	"math"

	qt "github.com/mappu/miqt/qt6"
	"github.com/kartoza/kartoza-screencaster/internal/webcam"
)

const (
	editorWidth  = 480
	editorHeight = 270 // 16:9
	snapDistance  = 20  // pixels to snap to corners/edges
	defaultSize  = 60  // default bubble diameter in editor coords
	minSize      = 30
	maxSize      = 120
)

// overlayItem represents a draggable webcam overlay in the editor
type overlayItem struct {
	device string
	name   string
	x, y   int // center position in editor coords
	size   int // diameter in editor coords
	mode   webcam.WebcamDisplayMode
}

// OverlayEditor is a custom-painted widget for positioning webcam overlays
type OverlayEditor struct {
	widget *qt.QWidget
	items  []overlayItem

	// Drag state
	dragging  int // index of item being dragged, -1 if none
	dragOffX  int
	dragOffY  int
	snapEdges bool
	snapCorners bool

	// Callbacks
	onChange func()
}

// NewOverlayEditor creates a new overlay position editor
func NewOverlayEditor(devices []webcam.DeviceInfo, configs []webcam.WebcamConfig) *OverlayEditor {
	oe := &OverlayEditor{
		widget:      qt.NewQWidget2(),
		dragging:    -1,
		snapEdges:   true,
		snapCorners: true,
	}

	oe.widget.SetFixedSize2(editorWidth, editorHeight)
	oe.widget.SetMouseTracking(true)

	// Build items from configs
	configMap := make(map[string]webcam.WebcamConfig)
	for _, c := range configs {
		configMap[c.Device] = c
	}

	for i, dev := range devices {
		cfg, ok := configMap[dev.Device]
		if !ok || !cfg.Enabled {
			continue
		}

		item := overlayItem{
			device: dev.Device,
			name:   dev.Name,
			size:   defaultSize,
			mode:   cfg.LandscapeMode,
		}

		// Use saved position or default placement
		if cfg.OverlaySize > 0 {
			// Scale from video coords to editor coords (assume 1920px video width)
			item.size = cfg.OverlaySize * editorWidth / 1920
			if item.size < minSize {
				item.size = minSize
			}
			if item.size > maxSize {
				item.size = maxSize
			}
		}
		if cfg.OverlayX >= 0 && cfg.OverlayY >= 0 {
			item.x = cfg.OverlayX * editorWidth / 1920
			item.y = cfg.OverlayY * editorHeight / 1080
		} else {
			// Default: bottom-right corner, stacked
			item.x = editorWidth - defaultSize/2 - 15 - i*(defaultSize+10)
			item.y = editorHeight - defaultSize/2 - 15
		}

		oe.items = append(oe.items, item)
	}

	oe.setupEvents()

	return oe
}

func (oe *OverlayEditor) setupEvents() {
	// Paint
	oe.widget.OnPaintEvent(func(super func(event *qt.QPaintEvent), event *qt.QPaintEvent) {
		oe.paint()
	})

	// Mouse press
	oe.widget.OnMousePressEvent(func(super func(event *qt.QMouseEvent), event *qt.QMouseEvent) {
		pos := event.Position()
		mx, my := int(pos.X()), int(pos.Y())

		// Find which item was clicked (reverse order = top items first)
		for i := len(oe.items) - 1; i >= 0; i-- {
			item := oe.items[i]
			dx := mx - item.x
			dy := my - item.y
			radius := item.size / 2
			if dx*dx+dy*dy <= radius*radius {
				oe.dragging = i
				oe.dragOffX = dx
				oe.dragOffY = dy
				break
			}
		}
	})

	// Mouse move
	oe.widget.OnMouseMoveEvent(func(super func(event *qt.QMouseEvent), event *qt.QMouseEvent) {
		if oe.dragging < 0 {
			return
		}

		pos := event.Position()
		mx, my := int(pos.X()), int(pos.Y())

		newX := mx - oe.dragOffX
		newY := my - oe.dragOffY

		item := &oe.items[oe.dragging]
		r := item.size / 2

		// Clamp to editor bounds
		if newX-r < 0 {
			newX = r
		}
		if newX+r > editorWidth {
			newX = editorWidth - r
		}
		if newY-r < 0 {
			newY = r
		}
		if newY+r > editorHeight {
			newY = editorHeight - r
		}

		// Snap to corners and edges
		if oe.snapCorners || oe.snapEdges {
			newX, newY = oe.applySnap(newX, newY, r)
		}

		item.x = newX
		item.y = newY
		oe.widget.Update()
	})

	// Mouse release
	oe.widget.OnMouseReleaseEvent(func(super func(event *qt.QMouseEvent), event *qt.QMouseEvent) {
		if oe.dragging >= 0 {
			oe.dragging = -1
			if oe.onChange != nil {
				oe.onChange()
			}
		}
	})
}

func (oe *OverlayEditor) applySnap(x, y, r int) (int, int) {
	margin := 15

	// Corner snap points
	corners := [][2]int{
		{r + margin, r + margin},                                     // top-left
		{editorWidth - r - margin, r + margin},                       // top-right
		{r + margin, editorHeight - r - margin},                      // bottom-left
		{editorWidth - r - margin, editorHeight - r - margin},        // bottom-right
	}

	if oe.snapCorners {
		for _, c := range corners {
			dx := x - c[0]
			dy := y - c[1]
			if int(math.Sqrt(float64(dx*dx+dy*dy))) < snapDistance {
				return c[0], c[1]
			}
		}
	}

	if oe.snapEdges {
		// Snap to edges
		if x-r < snapDistance+margin {
			x = r + margin
		} else if editorWidth-x-r < snapDistance+margin {
			x = editorWidth - r - margin
		}
		if y-r < snapDistance+margin {
			y = r + margin
		} else if editorHeight-y-r < snapDistance+margin {
			y = editorHeight - r - margin
		}
	}

	return x, y
}

func (oe *OverlayEditor) paint() {
	painter := qt.NewQPainter2(oe.widget.QPaintDevice)
	defer painter.End()

	// Background — represent the screen
	bgColor := qt.NewQColor3(30, 30, 46)
	painter.FillRect5(0, 0, editorWidth, editorHeight, bgColor)

	// Screen border
	borderPen := qt.NewQPen3(qt.NewQColor3(69, 71, 90))
	painter.SetPenWithPen(borderPen)
	painter.SetBrushWithStyle(qt.NoBrush)
	painter.DrawRect2(0, 0, editorWidth-1, editorHeight-1)

	// "Screen" label
	textPen := qt.NewQPen3(qt.NewQColor3(108, 112, 134))
	painter.SetPenWithPen(textPen)
	painter.DrawText2(qt.NewQPoint2(editorWidth/2-30, editorHeight/2), "Screen")

	// Draw each overlay item
	for i, item := range oe.items {
		r := item.size / 2
		isDragging := i == oe.dragging

		if item.mode == webcam.DisplayBubble || item.mode == "" {
			// Circle bubble
			if isDragging {
				painter.SetBrush(qt.NewQBrush3(qt.NewQColor3(137, 180, 250))) // blue
			} else {
				painter.SetBrush(qt.NewQBrush3(qt.NewQColor3(166, 227, 161))) // green
			}
			outlinePen := qt.NewQPen3(qt.NewQColor3(205, 214, 244))
			painter.SetPenWithPen(outlinePen)
			painter.DrawEllipse2(item.x-r, item.y-r, item.size, item.size)
		} else {
			// Rectangle
			if isDragging {
				painter.SetBrush(qt.NewQBrush3(qt.NewQColor3(137, 180, 250)))
			} else {
				painter.SetBrush(qt.NewQBrush3(qt.NewQColor3(166, 227, 161)))
			}
			outlinePen := qt.NewQPen3(qt.NewQColor3(205, 214, 244))
			painter.SetPenWithPen(outlinePen)
			painter.DrawRect2(item.x-r, item.y-r*2/3, item.size, item.size*2/3)
		}

		// Label
		labelPen := qt.NewQPen3(qt.NewQColor3(30, 30, 46))
		painter.SetPenWithPen(labelPen)
		// Truncate name
		name := item.name
		if len(name) > 8 {
			name = name[:8]
		}
		painter.DrawText2(qt.NewQPoint2(item.x-r/2, item.y+4), name)
	}

	// Snap guides — show corners when dragging
	if oe.dragging >= 0 {
		guidePen := qt.NewQPen3(qt.NewQColor3(137, 180, 250))
		painter.SetPenWithPen(guidePen)
		painter.SetBrushWithStyle(qt.NoBrush)
		margin := 15
		// Draw small markers at snap corners
		for _, c := range [][2]int{
			{margin, margin},
			{editorWidth - margin, margin},
			{margin, editorHeight - margin},
			{editorWidth - margin, editorHeight - margin},
		} {
			painter.DrawRect2(c[0]-3, c[1]-3, 6, 6)
		}
	}
}

// Widget returns the underlying QWidget
func (oe *OverlayEditor) Widget() *qt.QWidget {
	return oe.widget
}

// GetPositions returns the current overlay positions scaled to video coordinates
func (oe *OverlayEditor) GetPositions() []webcam.WebcamConfig {
	var configs []webcam.WebcamConfig
	for _, item := range oe.items {
		configs = append(configs, webcam.WebcamConfig{
			Device:    item.device,
			OverlayX:  item.x * 1920 / editorWidth,
			OverlayY:  item.y * 1080 / editorHeight,
			OverlaySize: item.size * 1920 / editorWidth,
		})
	}
	return configs
}

// SetSize changes the size of a specific overlay item
func (oe *OverlayEditor) SetSize(index int, delta int) {
	if index < 0 || index >= len(oe.items) {
		return
	}
	oe.items[index].size += delta
	if oe.items[index].size < minSize {
		oe.items[index].size = minSize
	}
	if oe.items[index].size > maxSize {
		oe.items[index].size = maxSize
	}
	oe.widget.Update()
}

// OnChange sets a callback for when positions change
func (oe *OverlayEditor) OnChange(cb func()) {
	oe.onChange = cb
}

// ItemCount returns the number of overlay items
func (oe *OverlayEditor) ItemCount() int {
	return len(oe.items)
}

// ItemName returns the name of an overlay item
func (oe *OverlayEditor) ItemName(index int) string {
	if index < 0 || index >= len(oe.items) {
		return ""
	}
	return fmt.Sprintf("%s (%dpx)", oe.items[index].name, oe.items[index].size*1920/editorWidth)
}
