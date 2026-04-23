package widgets

import (
	"path/filepath"
	"strings"

	qt "github.com/mappu/miqt/qt6"
)

// LogoDropZone is a single logo drop zone with thumbnail preview
type LogoDropZone struct {
	widget   *qt.QWidget
	preview  *qt.QLabel
	nameLabel *qt.QLabel
	clearBtn *qt.QPushButton
	filePath string
	label    string

	onChange func(string)
}

// NewLogoDropZone creates a drop zone for a logo position (e.g., "Left Logo")
func NewLogoDropZone(label string) *LogoDropZone {
	z := &LogoDropZone{
		widget: qt.NewQWidget2(),
		label:  label,
	}
	z.setupUI()
	return z
}

func (z *LogoDropZone) setupUI() {
	layout := qt.NewQVBoxLayout(z.widget)
	layout.SetContentsMargins(0, 0, 0, 0)
	layout.SetSpacing(4)

	// Label
	titleLabel := qt.NewQLabel3(z.label)
	titleLabel.SetStyleSheet("QLabel { color: #cdd6f4; font-size: 12px; font-weight: bold; }")
	titleLabel.SetAlignment(qt.AlignCenter)
	layout.AddWidget(titleLabel.QFrame.QWidget)

	// Preview area — accepts drops
	z.preview = qt.NewQLabel3("Drop logo here\nor click Browse")
	z.preview.SetFixedSize2(120, 80)
	z.preview.SetAlignment(qt.AlignCenter)
	z.preview.SetScaledContents(true)
	z.preview.SetAcceptDrops(true)
	z.preview.SetStyleSheet(`
		QLabel {
			background: #313244;
			color: #6c7086;
			border: 2px dashed #45475a;
			border-radius: 6px;
			font-size: 10px;
		}
	`)
	z.preview.SetToolTip("Drag and drop an image file here,\nor click Browse to select a logo.\nSupports PNG, JPG, SVG, GIF.")

	// Wire drag-and-drop on the preview label
	z.preview.OnDragEnterEvent(func(super func(event *qt.QDragEnterEvent), event *qt.QDragEnterEvent) {
		if event.MimeData().HasUrls() {
			event.AcceptProposedAction()
		}
	})
	z.preview.OnDropEvent(func(super func(event *qt.QDropEvent), event *qt.QDropEvent) {
		urls := event.MimeData().Urls()
		if len(urls) > 0 {
			path := urls[0].ToLocalFile()
			if isImageFile(path) {
				z.SetFile(path)
			}
		}
	})

	layout.AddWidget3(z.preview.QFrame.QWidget, 0, qt.AlignHCenter)

	// Filename label
	z.nameLabel = qt.NewQLabel3("None")
	z.nameLabel.SetStyleSheet("QLabel { color: #6c7086; font-size: 10px; }")
	z.nameLabel.SetAlignment(qt.AlignCenter)
	z.nameLabel.SetFixedWidth(120)
	layout.AddWidget3(z.nameLabel.QFrame.QWidget, 0, qt.AlignHCenter)

	// Button row
	btnRow := qt.NewQHBoxLayout2()
	btnRow.SetSpacing(4)

	browseBtn := qt.NewQPushButton3("Browse")
	browseBtn.SetFixedHeight(22)
	browseBtn.SetStyleSheet(`
		QPushButton {
			background: #45475a;
			color: #cdd6f4;
			border: none;
			border-radius: 3px;
			font-size: 10px;
			padding: 2px 8px;
		}
		QPushButton:hover { background: #585b70; }
	`)
	browseBtn.OnClicked(func() {
		file := qt.QFileDialog_GetOpenFileName3(z.widget, "Select "+z.label, "")
		if file != "" && isImageFile(file) {
			z.SetFile(file)
		}
	})
	btnRow.AddWidget(browseBtn.QAbstractButton.QWidget)

	z.clearBtn = qt.NewQPushButton3("Clear")
	z.clearBtn.SetFixedHeight(22)
	z.clearBtn.SetVisible(false)
	z.clearBtn.SetStyleSheet(`
		QPushButton {
			background: #f38ba8;
			color: #1e1e2e;
			border: none;
			border-radius: 3px;
			font-size: 10px;
			padding: 2px 8px;
		}
		QPushButton:hover { background: #eba0ac; }
	`)
	z.clearBtn.OnClicked(func() {
		z.Clear()
	})
	btnRow.AddWidget(z.clearBtn.QAbstractButton.QWidget)

	layout.AddLayout(btnRow.QLayout)
}

// SetFile sets the logo file and updates the preview.
// If the file doesn't exist or can't be loaded, it's treated as no logo.
func (z *LogoDropZone) SetFile(path string) {
	if path == "" {
		return
	}
	pixmap := qt.NewQPixmap4(path)
	if pixmap.IsNull() {
		// File doesn't exist or isn't a valid image — ignore
		return
	}
	z.filePath = path
	z.preview.SetPixmap(pixmap)
	z.preview.SetStyleSheet(`
		QLabel {
			background: #313244;
			border: 2px solid #a6e3a1;
			border-radius: 6px;
		}
	`)
	z.nameLabel.SetText(filepath.Base(path))
	z.clearBtn.SetVisible(true)

	if z.onChange != nil {
		z.onChange(path)
	}
}

// Clear removes the logo
func (z *LogoDropZone) Clear() {
	z.filePath = ""
	z.preview.Clear()
	z.preview.SetText("Drop logo here\nor click Browse")
	z.preview.SetStyleSheet(`
		QLabel {
			background: #313244;
			color: #6c7086;
			border: 2px dashed #45475a;
			border-radius: 6px;
			font-size: 10px;
		}
	`)
	z.nameLabel.SetText("None")
	z.clearBtn.SetVisible(false)

	if z.onChange != nil {
		z.onChange("")
	}
}

// FilePath returns the current logo file path
func (z *LogoDropZone) FilePath() string {
	return z.filePath
}

// OnChange sets a callback for when the logo changes
func (z *LogoDropZone) OnChange(cb func(string)) {
	z.onChange = cb
}

// Widget returns the underlying QWidget
func (z *LogoDropZone) Widget() *qt.QWidget {
	return z.widget
}

func isImageFile(path string) bool {
	ext := strings.ToLower(filepath.Ext(path))
	switch ext {
	case ".png", ".jpg", ".jpeg", ".svg", ".gif", ".bmp", ".webp":
		return true
	}
	return false
}
