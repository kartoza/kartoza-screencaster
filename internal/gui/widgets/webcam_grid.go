package widgets

import (
	qt "github.com/mappu/miqt/qt6"
	"github.com/kartoza/kartoza-screencaster/internal/webcam"
)

// WebcamGrid displays a grid of live webcam previews with enable/disable checkboxes
type WebcamGrid struct {
	widget   *qt.QWidget
	layout   *qt.QGridLayout
	previews []*WebcamPreview
	checks   []*qt.QCheckBox
	devices  []webcam.DeviceInfo
}

// NewWebcamGrid creates a new webcam grid for the detected devices
func NewWebcamGrid(devices []webcam.DeviceInfo) *WebcamGrid {
	g := &WebcamGrid{
		widget:  qt.NewQWidget2(),
		devices: devices,
	}

	g.layout = qt.NewQGridLayout2()
	g.layout.SetSpacing(8)
	g.widget.SetLayout(g.layout.QLayout)

	if len(devices) == 0 {
		noDevLabel := qt.NewQLabel3("No webcams detected")
		noDevLabel.SetStyleSheet(`
			QLabel {
				color: #6c7086;
				font-size: 14px;
				font-style: italic;
				padding: 30px;
			}
		`)
		noDevLabel.SetAlignment(qt.AlignCenter)
		g.layout.AddWidget(noDevLabel.QFrame.QWidget)
		return g
	}

	// Grid: 1 cam = 1x1, 2+ = side by side
	cols := 1
	if len(devices) > 1 {
		cols = 2
	}

	for i, dev := range devices {
		container := qt.NewQWidget2()
		containerLayout := qt.NewQVBoxLayout(container)
		containerLayout.SetContentsMargins(0, 0, 0, 0)
		containerLayout.SetSpacing(2)

		// Webcam preview
		preview := NewWebcamPreview(dev.Device)
		g.previews = append(g.previews, preview)
		containerLayout.AddWidget(preview.Widget())

		// Name + checkbox row
		controlRow := qt.NewQHBoxLayout2()

		check := qt.NewQCheckBox3(dev.Name)
		check.SetChecked(true)
		check.SetStyleSheet("QCheckBox { color: #6c7086; font-size: 11px; } QCheckBox::indicator { width: 14px; height: 14px; }")

		// Connect checkbox to start/stop preview
		idx := i
		check.OnStateChanged(func(state int) {
			if state == 2 { // Checked
				_ = g.previews[idx].Start()
			} else {
				g.previews[idx].Stop()
			}
		})
		g.checks = append(g.checks, check)
		controlRow.AddWidget(check.QAbstractButton.QWidget)
		controlRow.AddStretch()
		containerLayout.AddLayout(controlRow.QLayout)

		row := i / cols
		col := i % cols
		g.layout.AddWidget2(container, row, col)
	}

	return g
}

// Widget returns the underlying QWidget
func (g *WebcamGrid) Widget() *qt.QWidget {
	return g.widget
}

// StartAll starts all enabled webcam previews
func (g *WebcamGrid) StartAll() {
	for i, p := range g.previews {
		if i < len(g.checks) && g.checks[i].IsChecked() {
			_ = p.Start()
		}
	}
}

// StopAll stops all webcam previews
func (g *WebcamGrid) StopAll() {
	for _, p := range g.previews {
		p.Stop()
	}
}

// EnabledDevices returns the list of device names that are checked/enabled
func (g *WebcamGrid) EnabledDevices() []string {
	var enabled []string
	for i, dev := range g.devices {
		if i < len(g.checks) && g.checks[i].IsChecked() {
			enabled = append(enabled, dev.Device)
		}
	}
	return enabled
}
