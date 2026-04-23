package pages

import (
	qt "github.com/mappu/miqt/qt6"
)

// ProcessingPage shows post-recording processing progress
type ProcessingPage struct {
	widget *qt.QWidget

	// Progress bars
	step1Bar *qt.QProgressBar
	step2Bar *qt.QProgressBar
	step3Bar *qt.QProgressBar
	step4Bar *qt.QProgressBar

	// Labels
	elapsedLabel   *qt.QLabel
	remainingLabel *qt.QLabel
}

// NewProcessingPage creates a new processing page
func NewProcessingPage() *ProcessingPage {
	p := &ProcessingPage{
		widget: qt.NewQWidget2(),
	}
	p.setupUI()
	return p
}

func (p *ProcessingPage) setupUI() {
	layout := qt.NewQVBoxLayout(p.widget)
	layout.SetContentsMargins(30, 30, 30, 30)
	layout.SetSpacing(15)

	// Title
	title := qt.NewQLabel3("Processing Recording...")
	title.SetStyleSheet("QLabel { color: #cdd6f4; font-size: 18px; font-weight: bold; }")
	layout.AddWidget(title.QFrame.QWidget)

	// Video thumbnail placeholder
	thumbnail := qt.NewQLabel3("Video Preview")
	thumbnail.SetStyleSheet(`
		QLabel {
			background: #313244;
			color: #6c7086;
			border: 2px dashed #45475a;
			border-radius: 8px;
			padding: 30px;
			font-size: 14px;
		}
	`)
	thumbnail.SetAlignment(qt.AlignCenter)
	thumbnail.SetMinimumHeight(200)
	layout.AddWidget(thumbnail.QFrame.QWidget)

	barStyle := `
		QProgressBar {
			background: #313244;
			border: none;
			border-radius: 4px;
			height: 20px;
			color: #cdd6f4;
			text-align: center;
		}
		QProgressBar::chunk {
			background: #89b4fa;
			border-radius: 4px;
		}
	`

	// Step 1
	step1Row := qt.NewQHBoxLayout2()
	step1Label := qt.NewQLabel3("Step 1: Analyzing audio")
	step1Label.SetStyleSheet("QLabel { color: #cdd6f4; }")
	step1Label.SetFixedWidth(250)
	step1Row.AddWidget(step1Label.QFrame.QWidget)
	p.step1Bar = qt.NewQProgressBar2()
	p.step1Bar.SetStyleSheet(barStyle)
	p.step1Bar.SetValue(0)
	step1Row.AddWidget(p.step1Bar.QWidget)
	layout.AddLayout(step1Row.QLayout)

	// Step 2
	step2Row := qt.NewQHBoxLayout2()
	step2Label := qt.NewQLabel3("Step 2: Normalizing audio")
	step2Label.SetStyleSheet("QLabel { color: #cdd6f4; }")
	step2Label.SetFixedWidth(250)
	step2Row.AddWidget(step2Label.QFrame.QWidget)
	p.step2Bar = qt.NewQProgressBar2()
	p.step2Bar.SetStyleSheet(barStyle)
	p.step2Bar.SetValue(0)
	step2Row.AddWidget(p.step2Bar.QWidget)
	layout.AddLayout(step2Row.QLayout)

	// Step 3
	step3Row := qt.NewQHBoxLayout2()
	step3Label := qt.NewQLabel3("Step 3: Merging video & audio")
	step3Label.SetStyleSheet("QLabel { color: #cdd6f4; }")
	step3Label.SetFixedWidth(250)
	step3Row.AddWidget(step3Label.QFrame.QWidget)
	p.step3Bar = qt.NewQProgressBar2()
	p.step3Bar.SetStyleSheet(barStyle)
	p.step3Bar.SetValue(0)
	step3Row.AddWidget(p.step3Bar.QWidget)
	layout.AddLayout(step3Row.QLayout)

	// Step 4
	step4Row := qt.NewQHBoxLayout2()
	step4Label := qt.NewQLabel3("Step 4: Creating vertical video")
	step4Label.SetStyleSheet("QLabel { color: #cdd6f4; }")
	step4Label.SetFixedWidth(250)
	step4Row.AddWidget(step4Label.QFrame.QWidget)
	p.step4Bar = qt.NewQProgressBar2()
	p.step4Bar.SetStyleSheet(barStyle)
	p.step4Bar.SetValue(0)
	step4Row.AddWidget(p.step4Bar.QWidget)
	layout.AddLayout(step4Row.QLayout)

	// Time info
	timeRow := qt.NewQHBoxLayout2()
	p.elapsedLabel = qt.NewQLabel3("Elapsed: 00:00")
	p.elapsedLabel.SetStyleSheet("QLabel { color: #6c7086; }")
	timeRow.AddWidget(p.elapsedLabel.QFrame.QWidget)
	timeRow.AddStretch()
	p.remainingLabel = qt.NewQLabel3("Estimated remaining: --:--")
	p.remainingLabel.SetStyleSheet("QLabel { color: #6c7086; }")
	timeRow.AddWidget(p.remainingLabel.QFrame.QWidget)
	layout.AddLayout(timeRow.QLayout)

	// Cancel button
	cancelBtn := qt.NewQPushButton3("Cancel")
	cancelBtn.SetStyleSheet(`
		QPushButton {
			background-color: #f38ba8;
			color: #1e1e2e;
			border: none;
			border-radius: 4px;
			padding: 8px 20px;
			font-weight: bold;
		}
		QPushButton:hover { background-color: #eba0ac; }
	`)
	cancelBtn.SetFixedWidth(120)
	layout.AddWidget(cancelBtn.QAbstractButton.QWidget)

	layout.AddStretch()
}

// Widget returns the underlying QWidget
func (p *ProcessingPage) Widget() *qt.QWidget {
	return p.widget
}

// SetProgress updates a step's progress bar
func (p *ProcessingPage) SetProgress(step int, value int) {
	switch step {
	case 1:
		p.step1Bar.SetValue(value)
	case 2:
		p.step2Bar.SetValue(value)
	case 3:
		p.step3Bar.SetValue(value)
	case 4:
		p.step4Bar.SetValue(value)
	}
}
