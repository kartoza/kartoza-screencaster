package pages

import (
	"fmt"
	"time"

	qt "github.com/mappu/miqt/qt6"
	"github.com/kartoza/kartoza-screencaster/internal/recorder"
)

// Step names matching the merger pipeline
var stepNames = []string{
	"Stopping recorders",
	"Analyzing audio",
	"Normalizing audio",
	"Merging video & audio",
	"Creating vertical video",
}

// ProcessingPage shows post-recording processing progress
type ProcessingPage struct {
	widget *qt.QWidget

	// UI
	titleLabel     *qt.QLabel
	stepBars       []*qt.QProgressBar
	stepLabels     []*qt.QLabel
	stepStatusLabels []*qt.QLabel
	elapsedLabel   *qt.QLabel
	cancelBtn      *qt.QPushButton

	// State
	startTime    time.Time
	elapsedTimer *qt.QTimer
	pollTimer    *qt.QTimer
	progressChan <-chan recorder.ProgressUpdate

	// Callbacks
	onComplete func(success bool)
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
	layout.SetContentsMargins(30, 20, 30, 20)
	layout.SetSpacing(10)

	p.titleLabel = qt.NewQLabel3("Processing Recording...")
	p.titleLabel.SetStyleSheet("QLabel { color: #cdd6f4; font-size: 18px; font-weight: bold; }")
	layout.AddWidget(p.titleLabel.QFrame.QWidget)

	barStyle := `
		QProgressBar {
			background: #313244;
			border: none;
			border-radius: 4px;
			height: 18px;
			color: #cdd6f4;
			text-align: center;
			font-size: 11px;
		}
		QProgressBar::chunk {
			background: #89b4fa;
			border-radius: 4px;
		}
	`
	doneBarStyle := `
		QProgressBar {
			background: #313244;
			border: none;
			border-radius: 4px;
			height: 18px;
			color: #cdd6f4;
			text-align: center;
			font-size: 11px;
		}
		QProgressBar::chunk {
			background: #a6e3a1;
			border-radius: 4px;
		}
	`
	_ = doneBarStyle // used in updateStep

	for i, name := range stepNames {
		row := qt.NewQHBoxLayout2()
		row.SetSpacing(8)

		label := qt.NewQLabel3(fmt.Sprintf("Step %d:", i+1))
		label.SetStyleSheet("QLabel { color: #6c7086; font-size: 12px; }")
		label.SetFixedWidth(50)
		row.AddWidget(label.QFrame.QWidget)

		nameLabel := qt.NewQLabel3(name)
		nameLabel.SetStyleSheet("QLabel { color: #cdd6f4; font-size: 12px; }")
		nameLabel.SetFixedWidth(180)
		p.stepLabels = append(p.stepLabels, nameLabel)
		row.AddWidget(nameLabel.QFrame.QWidget)

		bar := qt.NewQProgressBar2()
		bar.SetStyleSheet(barStyle)
		bar.SetValue(0)
		p.stepBars = append(p.stepBars, bar)
		row.AddWidget(bar.QWidget)

		statusLabel := qt.NewQLabel3("Pending")
		statusLabel.SetStyleSheet("QLabel { color: #6c7086; font-size: 11px; }")
		statusLabel.SetFixedWidth(60)
		p.stepStatusLabels = append(p.stepStatusLabels, statusLabel)
		row.AddWidget(statusLabel.QFrame.QWidget)

		layout.AddLayout(row.QLayout)
	}

	// Elapsed time
	p.elapsedLabel = qt.NewQLabel3("Elapsed: 00:00")
	p.elapsedLabel.SetStyleSheet("QLabel { color: #6c7086; font-size: 12px; padding-top: 10px; }")
	layout.AddWidget(p.elapsedLabel.QFrame.QWidget)

	// Cancel button
	p.cancelBtn = qt.NewQPushButton3("Cancel")
	p.cancelBtn.SetFixedWidth(100)
	p.cancelBtn.SetStyleSheet(`
		QPushButton {
			background: #f38ba8;
			color: #1e1e2e;
			border: none;
			border-radius: 4px;
			padding: 8px;
			font-weight: bold;
		}
		QPushButton:hover { background: #eba0ac; }
	`)
	layout.AddWidget(p.cancelBtn.QAbstractButton.QWidget)

	layout.AddStretch()
}

// StartProcessing begins monitoring a progress channel
func (p *ProcessingPage) StartProcessing(ch <-chan recorder.ProgressUpdate) {
	p.progressChan = ch
	p.startTime = time.Now()
	p.titleLabel.SetText("Processing Recording...")

	// Reset all bars
	for i := range p.stepBars {
		p.stepBars[i].SetValue(0)
		p.stepBars[i].SetStyleSheet(`
			QProgressBar { background: #313244; border: none; border-radius: 4px; height: 18px; color: #cdd6f4; text-align: center; font-size: 11px; }
			QProgressBar::chunk { background: #89b4fa; border-radius: 4px; }
		`)
		p.stepStatusLabels[i].SetText("Pending")
		p.stepStatusLabels[i].SetStyleSheet("QLabel { color: #6c7086; font-size: 11px; }")
	}

	// Elapsed timer
	if p.elapsedTimer == nil {
		p.elapsedTimer = qt.NewQTimer()
		p.elapsedTimer.OnTimeout(func() {
			elapsed := time.Since(p.startTime)
			m := int(elapsed.Minutes())
			s := int(elapsed.Seconds()) % 60
			p.elapsedLabel.SetText(fmt.Sprintf("Elapsed: %02d:%02d", m, s))
		})
	}
	p.elapsedTimer.Start(1000)

	// Poll timer — reads from the progress channel on the main thread
	if p.pollTimer == nil {
		p.pollTimer = qt.NewQTimer()
		p.pollTimer.OnTimeout(func() {
			p.pollProgress()
		})
	}
	p.pollTimer.Start(100)
}

func (p *ProcessingPage) pollProgress() {
	for {
		select {
		case update, ok := <-p.progressChan:
			if !ok {
				// Channel closed — processing complete
				p.elapsedTimer.Stop()
				p.pollTimer.Stop()
				p.titleLabel.SetText("Processing Complete!")
				p.titleLabel.SetStyleSheet("QLabel { color: #a6e3a1; font-size: 18px; font-weight: bold; }")
				if p.onComplete != nil {
					p.onComplete(true)
				}
				return
			}
			p.handleUpdate(update)
		default:
			return // no more updates right now
		}
	}
}

func (p *ProcessingPage) handleUpdate(u recorder.ProgressUpdate) {
	step := u.Step
	if step < 0 || step >= len(p.stepBars) {
		return
	}

	if u.Percent >= 0 {
		// Percent update
		p.stepBars[step].SetValue(int(u.Percent))
		p.stepStatusLabels[step].SetText(fmt.Sprintf("%d%%", int(u.Percent)))
		p.stepStatusLabels[step].SetStyleSheet("QLabel { color: #89b4fa; font-size: 11px; }")
	} else if u.Completed {
		if u.Skipped {
			p.stepBars[step].SetValue(100)
			p.stepStatusLabels[step].SetText("Skipped")
			p.stepStatusLabels[step].SetStyleSheet("QLabel { color: #6c7086; font-size: 11px; }")
			p.stepBars[step].SetStyleSheet(`
				QProgressBar { background: #313244; border: none; border-radius: 4px; height: 18px; color: #cdd6f4; text-align: center; font-size: 11px; }
				QProgressBar::chunk { background: #6c7086; border-radius: 4px; }
			`)
		} else if u.Error != nil {
			p.stepBars[step].SetValue(100)
			p.stepStatusLabels[step].SetText("Error")
			p.stepStatusLabels[step].SetStyleSheet("QLabel { color: #f38ba8; font-size: 11px; font-weight: bold; }")
			p.stepBars[step].SetStyleSheet(`
				QProgressBar { background: #313244; border: none; border-radius: 4px; height: 18px; color: #cdd6f4; text-align: center; font-size: 11px; }
				QProgressBar::chunk { background: #f38ba8; border-radius: 4px; }
			`)
		} else {
			p.stepBars[step].SetValue(100)
			p.stepStatusLabels[step].SetText("Done")
			p.stepStatusLabels[step].SetStyleSheet("QLabel { color: #a6e3a1; font-size: 11px; font-weight: bold; }")
			p.stepBars[step].SetStyleSheet(`
				QProgressBar { background: #313244; border: none; border-radius: 4px; height: 18px; color: #cdd6f4; text-align: center; font-size: 11px; }
				QProgressBar::chunk { background: #a6e3a1; border-radius: 4px; }
			`)
		}
	} else {
		// Step started
		p.stepStatusLabels[step].SetText("Running")
		p.stepStatusLabels[step].SetStyleSheet("QLabel { color: #89b4fa; font-size: 11px; }")
	}
}

// OnComplete sets a callback for when processing finishes
func (p *ProcessingPage) OnComplete(cb func(success bool)) {
	p.onComplete = cb
}

// Widget returns the underlying QWidget
func (p *ProcessingPage) Widget() *qt.QWidget {
	return p.widget
}

// SetProgress updates a step's progress bar (legacy API)
func (p *ProcessingPage) SetProgress(step int, value int) {
	if step >= 0 && step < len(p.stepBars) {
		p.stepBars[step].SetValue(value)
	}
}
