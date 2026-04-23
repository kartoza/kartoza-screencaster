package widgets

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sync"

	qt "github.com/mappu/miqt/qt6"
	"github.com/kartoza/kartoza-screencaster/internal/models"
)

const (
	monPreviewW = 200
	monPreviewH = 112 // 16:9
)

// MonitorPicker displays clickable monitor previews with live screenshots.
// selected = -1 means "no screen" (webcam-only recording).
type MonitorPicker struct {
	widget    *qt.QWidget
	monitors  []models.Monitor
	labels    []*qt.QLabel
	noneLabel *qt.QLabel
	selected  int

	// Live refresh
	refreshTimer *qt.QTimer
	mu           sync.Mutex
	// Pre-captured screenshot paths (written by goroutine, read by main thread)
	pendingPaths map[int]string

	onSelected func(int)
}

// NewMonitorPicker creates a monitor picker with live updating thumbnails
func NewMonitorPicker(monitors []models.Monitor) *MonitorPicker {
	mp := &MonitorPicker{
		widget:       qt.NewQWidget2(),
		monitors:     monitors,
		selected:     -1,
		pendingPaths: make(map[int]string),
	}
	mp.setupUI()

	// Initial capture (blocking, so thumbnails appear immediately)
	mp.captureAll()
	mp.applyPending()

	// Auto-select focused monitor
	for i, m := range monitors {
		if m.Focused {
			mp.selectMonitor(i)
			break
		}
	}
	if mp.selected < 0 && len(monitors) > 0 {
		mp.selectMonitor(0)
	}

	// Live refresh: capture in goroutine every 2s, apply on main thread
	mp.refreshTimer = qt.NewQTimer()
	mp.refreshTimer.SetInterval(2000)
	mp.refreshTimer.OnTimeout(func() {
		// Check if there are pending captures from the last goroutine
		mp.mu.Lock()
		hasPending := len(mp.pendingPaths) > 0
		mp.mu.Unlock()

		if hasPending {
			mp.applyPending()
		}

		// Start next capture in background
		go func() {
			mp.mu.Lock()
			defer mp.mu.Unlock()
			mp.captureAll()
		}()
	})
	mp.refreshTimer.Start2()

	return mp
}

func (mp *MonitorPicker) setupUI() {
	layout := qt.NewQHBoxLayout(mp.widget)
	layout.SetSpacing(12)
	layout.SetContentsMargins(0, 0, 0, 0)

	// "None" option for talking-head (webcam-only) recording
	noneContainer := qt.NewQWidget2()
	noneLayout := qt.NewQVBoxLayout(noneContainer)
	noneLayout.SetContentsMargins(0, 0, 0, 0)
	noneLayout.SetSpacing(4)

	nonePreview := qt.NewQLabel3("No Screen")
	nonePreview.SetFixedSize2(monPreviewW/2, monPreviewH)
	nonePreview.SetAlignment(qt.AlignCenter)
	nonePreview.SetStyleSheet(`
		QLabel {
			background: #313244;
			color: #6c7086;
			border: 2px solid #45475a;
			border-radius: 6px;
			font-size: 12px;
		}
	`)
	mp.noneLabel = nonePreview
	noneLayout.AddWidget3(nonePreview.QFrame.QWidget, 0, qt.AlignHCenter)

	noneName := qt.NewQLabel3("Webcam only")
	noneName.SetStyleSheet("QLabel { color: #6c7086; font-size: 11px; }")
	noneName.SetAlignment(qt.AlignHCenter)
	noneLayout.AddWidget3(noneName.QFrame.QWidget, 0, qt.AlignHCenter)

	noneBtn := qt.NewQPushButton3("Select")
	noneBtn.SetFixedWidth(monPreviewW / 2)
	noneBtn.SetFixedHeight(24)
	noneBtn.SetStyleSheet(`
		QPushButton {
			background: #45475a;
			color: #cdd6f4;
			border: none;
			border-radius: 4px;
			font-size: 11px;
		}
		QPushButton:hover { background: #585b70; }
	`)
	noneBtn.OnClicked(func() {
		mp.selectMonitor(-1)
	})
	noneLayout.AddWidget3(noneBtn.QAbstractButton.QWidget, 0, qt.AlignHCenter)
	layout.AddWidget(noneContainer)

	for i, m := range mp.monitors {
		container := qt.NewQWidget2()
		containerLayout := qt.NewQVBoxLayout(container)
		containerLayout.SetContentsMargins(0, 0, 0, 0)
		containerLayout.SetSpacing(4)

		// Screenshot preview
		preview := qt.NewQLabel2()
		preview.SetFixedSize2(monPreviewW, monPreviewH)
		preview.SetAlignment(qt.AlignCenter)
		preview.SetScaledContents(true)
		preview.SetStyleSheet(`
			QLabel {
				background: #313244;
				border: 2px solid #45475a;
				border-radius: 6px;
			}
		`)
		mp.labels = append(mp.labels, preview)
		containerLayout.AddWidget3(preview.QFrame.QWidget, 0, qt.AlignHCenter)

		// Monitor name centered below thumbnail
		desc := mp.monitorDescription(m)
		nameLabel := qt.NewQLabel3(desc)
		nameLabel.SetStyleSheet("QLabel { color: #6c7086; font-size: 11px; }")
		nameLabel.SetAlignment(qt.AlignHCenter)
		nameLabel.SetWordWrap(true)
		nameLabel.SetFixedWidth(monPreviewW)
		containerLayout.AddWidget3(nameLabel.QFrame.QWidget, 0, qt.AlignHCenter)

		// Click the entire container to select
		idx := i
		selectBtn := qt.NewQPushButton3("Select")
		selectBtn.SetFixedWidth(monPreviewW)
		selectBtn.SetFixedHeight(24)
		selectBtn.SetStyleSheet(`
			QPushButton {
				background: #45475a;
				color: #cdd6f4;
				border: none;
				border-radius: 4px;
				font-size: 11px;
			}
			QPushButton:hover { background: #585b70; }
		`)
		selectBtn.OnClicked(func() {
			mp.selectMonitor(idx)
		})
		containerLayout.AddWidget3(selectBtn.QAbstractButton.QWidget, 0, qt.AlignHCenter)

		layout.AddWidget(container)
	}

	if len(mp.monitors) == 0 {
		noMon := qt.NewQLabel3("No monitors detected")
		noMon.SetStyleSheet("QLabel { color: #6c7086; font-size: 13px; font-style: italic; padding: 20px; }")
		noMon.SetAlignment(qt.AlignCenter)
		layout.AddWidget(noMon.QFrame.QWidget)
	}
}

func (mp *MonitorPicker) monitorDescription(m models.Monitor) string {
	desc := m.Description
	if desc == "" {
		desc = m.Name
	}
	if desc == "" {
		desc = "Unknown"
	}
	return fmt.Sprintf("%s\n%dx%d", desc, m.Width, m.Height)
}

func (mp *MonitorPicker) selectMonitor(idx int) {
	if idx < -1 || idx >= len(mp.monitors) {
		return
	}

	// Deselect previous
	if mp.selected >= 0 && mp.selected < len(mp.labels) {
		mp.labels[mp.selected].SetStyleSheet(`
			QLabel {
				background: #313244;
				border: 2px solid #45475a;
				border-radius: 6px;
			}
		`)
	}
	if mp.selected == -1 && mp.noneLabel != nil {
		mp.noneLabel.SetStyleSheet(`
			QLabel {
				background: #313244;
				color: #6c7086;
				border: 2px solid #45475a;
				border-radius: 6px;
				font-size: 12px;
			}
		`)
	}

	mp.selected = idx

	// Highlight new selection
	if idx == -1 && mp.noneLabel != nil {
		mp.noneLabel.SetStyleSheet(`
			QLabel {
				background: #313244;
				color: #89b4fa;
				border: 2px solid #89b4fa;
				border-radius: 6px;
				font-size: 12px;
				font-weight: bold;
			}
		`)
	} else if idx >= 0 {
		mp.labels[idx].SetStyleSheet(`
			QLabel {
				background: #313244;
				border: 2px solid #89b4fa;
				border-radius: 6px;
			}
		`)
	}

	if mp.onSelected != nil {
		mp.onSelected(idx)
	}
}

// captureAll captures screenshots of all monitors to temp files.
// Must be called with mp.mu held.
func (mp *MonitorPicker) captureAll() {
	tmpDir := os.TempDir()
	for i, m := range mp.monitors {
		if m.Name == "" {
			continue
		}
		path := filepath.Join(tmpDir, fmt.Sprintf("kartoza-mon-%d.png", i))
		cmd := exec.Command("grim", "-o", m.Name, "-t", "png", "-l", "0", path)
		cmd.Stderr = nil
		cmd.Stdout = nil
		if err := cmd.Run(); err != nil {
			continue
		}
		mp.pendingPaths[i] = path
	}
}

// applyPending loads pending screenshot files into the labels. Must be called on main thread.
func (mp *MonitorPicker) applyPending() {
	mp.mu.Lock()
	paths := make(map[int]string)
	for k, v := range mp.pendingPaths {
		paths[k] = v
	}
	mp.pendingPaths = make(map[int]string)
	mp.mu.Unlock()

	for idx, path := range paths {
		if _, err := os.Stat(path); err == nil {
			pixmap := qt.NewQPixmap4(path)
			if !pixmap.IsNull() && idx < len(mp.labels) {
				mp.labels[idx].SetPixmap(pixmap)
			}
			_ = os.Remove(path)
		}
	}
}

// Widget returns the underlying QWidget
func (mp *MonitorPicker) Widget() *qt.QWidget {
	return mp.widget
}

// SelectedIndex returns the currently selected monitor index
func (mp *MonitorPicker) SelectedIndex() int {
	return mp.selected
}

// SelectedMonitor returns the currently selected monitor
func (mp *MonitorPicker) SelectedMonitor() *models.Monitor {
	if mp.selected >= 0 && mp.selected < len(mp.monitors) {
		return &mp.monitors[mp.selected]
	}
	return nil
}

// OnSelected sets a callback for when a monitor is selected
func (mp *MonitorPicker) OnSelected(cb func(int)) {
	mp.onSelected = cb
}

// Stop stops the live refresh timer
func (mp *MonitorPicker) Stop() {
	if mp.refreshTimer != nil {
		mp.refreshTimer.Stop()
	}
}
