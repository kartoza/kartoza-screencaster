package pages

import (
	qt "github.com/mappu/miqt/qt6"
)

// PlayerPage is the built-in video player page
type PlayerPage struct {
	widget *qt.QWidget
}

// NewPlayerPage creates a new player page
func NewPlayerPage() *PlayerPage {
	p := &PlayerPage{
		widget: qt.NewQWidget2(),
	}
	p.setupUI()
	return p
}

func (p *PlayerPage) setupUI() {
	layout := qt.NewQVBoxLayout(p.widget)
	layout.SetContentsMargins(20, 20, 20, 20)
	layout.SetSpacing(10)

	// Back button
	backBtn := qt.NewQPushButton3("< Back to History")
	backBtn.SetStyleSheet(`
		QPushButton {
			background: transparent;
			color: #89b4fa;
			border: none;
			padding: 5px;
			text-align: left;
			font-size: 13px;
		}
		QPushButton:hover { color: #74c7ec; }
	`)
	backBtn.SetFixedWidth(200)
	layout.AddWidget(backBtn.QAbstractButton.QWidget)

	// Video display area (placeholder - will use QMediaPlayer + QVideoWidget)
	videoArea := qt.NewQLabel3("Video Player")
	videoArea.SetStyleSheet(`
		QLabel {
			background: #000000;
			color: #6c7086;
			border-radius: 8px;
			font-size: 16px;
		}
	`)
	videoArea.SetAlignment(qt.AlignCenter)
	videoArea.SetMinimumHeight(400)
	layout.AddWidget(videoArea.QFrame.QWidget)

	// Controls row
	controlsRow := qt.NewQHBoxLayout2()
	controlsRow.SetSpacing(10)

	playBtn := qt.NewQPushButton3("Play")
	playBtn.SetStyleSheet(`
		QPushButton {
			background: #a6e3a1;
			color: #1e1e2e;
			border: none;
			border-radius: 4px;
			padding: 8px 20px;
			font-weight: bold;
		}
		QPushButton:hover { background: #94e2d5; }
	`)
	controlsRow.AddWidget(playBtn.QAbstractButton.QWidget)

	// Seek slider
	seekSlider := qt.NewQSlider2()
	seekSlider.SetOrientation(qt.Horizontal)
	seekSlider.SetStyleSheet(`
		QSlider::groove:horizontal {
			background: #313244;
			height: 6px;
			border-radius: 3px;
		}
		QSlider::handle:horizontal {
			background: #89b4fa;
			width: 14px;
			height: 14px;
			margin: -4px 0;
			border-radius: 7px;
		}
		QSlider::sub-page:horizontal {
			background: #89b4fa;
			border-radius: 3px;
		}
	`)
	controlsRow.AddWidget(seekSlider.QAbstractSlider.QWidget)

	timeLabel := qt.NewQLabel3("00:00 / 00:00")
	timeLabel.SetStyleSheet("QLabel { color: #cdd6f4; font-size: 12px; }")
	controlsRow.AddWidget(timeLabel.QFrame.QWidget)

	layout.AddLayout(controlsRow.QLayout)

	// Source toggle row
	sourceRow := qt.NewQHBoxLayout2()
	sourceRow.SetSpacing(5)

	sourceLabel := qt.NewQLabel3("Source:")
	sourceLabel.SetStyleSheet("QLabel { color: #6c7086; }")
	sourceRow.AddWidget(sourceLabel.QFrame.QWidget)

	btnStyle := `
		QPushButton {
			background: #313244;
			color: #cdd6f4;
			border: none;
			border-radius: 4px;
			padding: 6px 12px;
		}
		QPushButton:hover { background: #45475a; }
		QPushButton:checked { background: #89b4fa; color: #1e1e2e; }
	`

	mergedBtn := qt.NewQPushButton3("Merged")
	mergedBtn.SetCheckable(true)
	mergedBtn.SetChecked(true)
	mergedBtn.SetStyleSheet(btnStyle)
	sourceRow.AddWidget(mergedBtn.QAbstractButton.QWidget)

	verticalBtn := qt.NewQPushButton3("Vertical")
	verticalBtn.SetCheckable(true)
	verticalBtn.SetStyleSheet(btnStyle)
	sourceRow.AddWidget(verticalBtn.QAbstractButton.QWidget)

	rawScreenBtn := qt.NewQPushButton3("Raw Screen")
	rawScreenBtn.SetCheckable(true)
	rawScreenBtn.SetStyleSheet(btnStyle)
	sourceRow.AddWidget(rawScreenBtn.QAbstractButton.QWidget)

	rawWebcamBtn := qt.NewQPushButton3("Raw Webcam")
	rawWebcamBtn.SetCheckable(true)
	rawWebcamBtn.SetStyleSheet(btnStyle)
	sourceRow.AddWidget(rawWebcamBtn.QAbstractButton.QWidget)

	sourceRow.AddStretch()
	layout.AddLayout(sourceRow.QLayout)
}

// Widget returns the underlying QWidget
func (p *PlayerPage) Widget() *qt.QWidget {
	return p.widget
}
