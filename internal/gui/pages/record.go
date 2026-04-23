package pages

import (
	"fmt"
	"path/filepath"
	"strconv"
	"time"

	qt "github.com/mappu/miqt/qt6"
	"github.com/kartoza/kartoza-screencaster/internal/config"
	"github.com/kartoza/kartoza-screencaster/internal/gui/widgets"
	"github.com/kartoza/kartoza-screencaster/internal/models"
	"github.com/kartoza/kartoza-screencaster/internal/monitor"
	"github.com/kartoza/kartoza-screencaster/internal/recorder"
	"github.com/kartoza/kartoza-screencaster/internal/webcam"
)

// RecordingState tracks what the UI is showing
type RecordingState int

const (
	StateIdle RecordingState = iota
	StateCountdown
	StateRecording
	StatePaused
)

// RecordPage is the recording setup and control page
type RecordPage struct {
	widget *qt.QWidget

	// Form inputs
	titleInput     *qt.QLineEdit
	numberInput    *qt.QLineEdit
	presenterInput *qt.QLineEdit
	descInput      *qt.QTextEdit

	// Toggles
	audioCheck    *qt.QCheckBox
	webcamCheck   *qt.QCheckBox
	screenCheck   *qt.QCheckBox
	verticalCheck *qt.QCheckBox
	leftSplitCheck *qt.QCheckBox
	logosCheck    *qt.QCheckBox

	// Monitor picker
	monitorPicker *widgets.MonitorPicker

	// Controls
	startBtn *qt.QPushButton
	pauseBtn *qt.QPushButton
	stopBtn  *qt.QPushButton

	// Status
	elapsedLabel *qt.QLabel
	statusLabel  *qt.QLabel

	// State
	state       RecordingState
	rec         *recorder.Recorder
	monitors    []models.Monitor
	webcamDevs  []webcam.DeviceInfo
	elapsedTimer *qt.QTimer
	startTime   time.Time
	countdownVal int

	// Webcam preview
	webcamGrid *widgets.WebcamGrid

	// Callbacks
	onStatusChange func(string)
}

// NewRecordPage creates a new record page
func NewRecordPage() *RecordPage {
	p := &RecordPage{
		widget: qt.NewQWidget2(),
		rec:    recorder.New(),
		state:  StateIdle,
	}
	p.detectDevices()
	p.setupUI()
	p.setupTimers()
	return p
}

func (p *RecordPage) detectDevices() {
	p.monitors, _ = monitor.ListMonitors()
	p.webcamDevs, _ = webcam.DetectAllDevices()
}

// SetStatusCallback sets a callback for status bar updates
func (p *RecordPage) SetStatusCallback(cb func(string)) {
	p.onStatusChange = cb
}

func (p *RecordPage) setupUI() {
	layout := qt.NewQHBoxLayout(p.widget)
	layout.SetSpacing(15)
	layout.SetContentsMargins(15, 15, 15, 15)

	labelStyle := "QLabel { color: #cdd6f4; font-size: 13px; }"
	inputStyle := "QLineEdit { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 6px; font-size: 13px; }"
	checkStyle := "QCheckBox { color: #cdd6f4; font-size: 13px; } QCheckBox::indicator { width: 16px; height: 16px; }"

	// === Left column: setup form ===
	leftCol := qt.NewQWidget2()
	leftLayout := qt.NewQVBoxLayout(leftCol)
	leftLayout.SetSpacing(8)

	sectionTitle := qt.NewQLabel3("Recording Setup")
	sectionTitle.SetStyleSheet("QLabel { color: #cdd6f4; font-size: 18px; font-weight: bold; padding-bottom: 5px; }")
	leftLayout.AddWidget(sectionTitle.QFrame.QWidget)

	// Title
	p.titleInput = p.addFormRow(leftLayout, "Title:", "Enter recording title...", labelStyle, inputStyle)
	p.titleInput.SetToolTip("The title for this recording.\nUsed in output filenames and metadata.")

	// Number
	p.numberInput = p.addFormRow(leftLayout, "Number:", "001", labelStyle, inputStyle)
	recordingNumber := config.GetCurrentRecordingNumber()
	p.numberInput.SetText(fmt.Sprintf("%03d", recordingNumber))
	p.numberInput.SetToolTip("Sequential recording number.\nAuto-increments after each recording.\nUsed as prefix in output filenames (e.g. 001-title.mp4).")

	// Presenter
	cfg, _ := config.Load()
	p.presenterInput = p.addFormRow(leftLayout, "Presenter:", "Presenter name...", labelStyle, inputStyle)
	if cfg.DefaultPresenter != "" {
		p.presenterInput.SetText(cfg.DefaultPresenter)
	}
	p.presenterInput.SetToolTip("Name of the presenter.\nSaved in recording metadata.\nDefault can be set in Settings.")

	// Monitor selector (visual preview)
	monLabel := qt.NewQLabel3("Monitor:")
	monLabel.SetStyleSheet(labelStyle)
	leftLayout.AddWidget(monLabel.QFrame.QWidget)
	p.monitorPicker = widgets.NewMonitorPicker(p.monitors)
	p.monitorPicker.Widget().SetToolTip("Select which monitor to record.\nLive preview updates every 2 seconds.\nChoose 'No Screen' for webcam-only recording.")
	leftLayout.AddWidget(p.monitorPicker.Widget())

	// Separator
	sep1 := qt.NewQLabel3("Sources")
	sep1.SetStyleSheet("QLabel { color: #89b4fa; font-size: 14px; font-weight: bold; padding-top: 8px; }")
	leftLayout.AddWidget(sep1.QFrame.QWidget)

	// Source toggles
	presets := cfg.RecordingPresets
	if !cfg.PresetsConfigured {
		presets = config.DefaultRecordingPresets()
	}

	p.screenCheck = qt.NewQCheckBox3("Record Screen")
	p.screenCheck.SetChecked(presets.RecordScreen)
	p.screenCheck.SetStyleSheet(checkStyle)
	p.screenCheck.SetToolTip("Capture the selected monitor's screen.\nUses wl-screenrec on Wayland compositors.")
	leftLayout.AddWidget(p.screenCheck.QAbstractButton.QWidget)

	p.audioCheck = qt.NewQCheckBox3("Record Audio")
	p.audioCheck.SetChecked(presets.RecordAudio)
	p.audioCheck.SetStyleSheet(checkStyle)
	p.audioCheck.SetToolTip("Record audio from your microphone.\nUses PipeWire/PulseAudio.\nAudio is processed (denoised, normalized) after recording.")
	leftLayout.AddWidget(p.audioCheck.QAbstractButton.QWidget)

	p.webcamCheck = qt.NewQCheckBox3("Record Webcam")
	p.webcamCheck.SetChecked(presets.RecordWebcam)
	p.webcamCheck.SetStyleSheet(checkStyle)
	p.webcamCheck.SetToolTip("Record from webcam cameras.\nEnable/disable individual cameras\nin the preview panel on the right.")
	leftLayout.AddWidget(p.webcamCheck.QAbstractButton.QWidget)

	// Show detected webcams
	if len(p.webcamDevs) > 0 {
		for _, dev := range p.webcamDevs {
			devLabel := qt.NewQLabel3(fmt.Sprintf("    %s (%s)", dev.Device, dev.Name))
			devLabel.SetStyleSheet("QLabel { color: #6c7086; font-size: 11px; }")
			leftLayout.AddWidget(devLabel.QFrame.QWidget)
		}
	} else {
		noDevLabel := qt.NewQLabel3("    No webcams detected")
		noDevLabel.SetStyleSheet("QLabel { color: #6c7086; font-size: 11px; font-style: italic; }")
		leftLayout.AddWidget(noDevLabel.QFrame.QWidget)
	}

	// Output options separator
	sep2 := qt.NewQLabel3("Output")
	sep2.SetStyleSheet("QLabel { color: #89b4fa; font-size: 14px; font-weight: bold; padding-top: 8px; }")
	leftLayout.AddWidget(sep2.QFrame.QWidget)

	p.verticalCheck = qt.NewQCheckBox3("Create vertical video")
	p.verticalCheck.SetChecked(presets.VerticalVideo)
	p.verticalCheck.SetStyleSheet(checkStyle)
	p.verticalCheck.SetToolTip("Create a 9:16 vertical video\n(1080x1920) for YouTube Shorts,\nInstagram Reels, or TikTok.")
	leftLayout.AddWidget(p.verticalCheck.QAbstractButton.QWidget)

	p.leftSplitCheck = qt.NewQCheckBox3("Left split mode")
	p.leftSplitCheck.SetChecked(presets.LeftSplit)
	p.leftSplitCheck.SetStyleSheet(checkStyle)
	p.leftSplitCheck.SetToolTip("Use the left half of the screen\nfor vertical video instead of\nthe full screen scaled down.")
	leftLayout.AddWidget(p.leftSplitCheck.QAbstractButton.QWidget)

	p.logosCheck = qt.NewQCheckBox3("Add logos")
	p.logosCheck.SetChecked(presets.AddLogos)
	p.logosCheck.SetStyleSheet(checkStyle)
	p.logosCheck.SetToolTip("Overlay logos on the output video.\nConfigure logos in Settings.\nShown for the first 15 seconds.")
	leftLayout.AddWidget(p.logosCheck.QAbstractButton.QWidget)

	// Description
	descLabel := qt.NewQLabel3("Description:")
	descLabel.SetStyleSheet(labelStyle)
	leftLayout.AddWidget(descLabel.QFrame.QWidget)
	p.descInput = qt.NewQTextEdit2()
	p.descInput.SetPlaceholderText("Optional description...")
	p.descInput.SetToolTip("Optional description for this recording.\nSaved in recording metadata.")
	p.descInput.SetMaximumHeight(80)
	p.descInput.SetStyleSheet("QTextEdit { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 4px; font-size: 13px; }")
	leftLayout.AddWidget(p.descInput.QAbstractScrollArea.QFrame.QWidget)

	leftLayout.AddStretch()
	layout.AddWidget(leftCol)

	// === Right column: preview & controls ===
	rightCol := qt.NewQWidget2()
	rightLayout := qt.NewQVBoxLayout(rightCol)
	rightLayout.SetSpacing(10)

	previewTitle := qt.NewQLabel3("Preview & Controls")
	previewTitle.SetStyleSheet("QLabel { color: #cdd6f4; font-size: 18px; font-weight: bold; padding-bottom: 5px; }")
	rightLayout.AddWidget(previewTitle.QFrame.QWidget)

	// Status + elapsed in a compact row
	statusRow := qt.NewQHBoxLayout2()
	p.statusLabel = qt.NewQLabel3("Ready to record")
	p.statusLabel.SetStyleSheet("QLabel { color: #a6e3a1; font-size: 14px; font-weight: bold; }")
	statusRow.AddWidget(p.statusLabel.QFrame.QWidget)
	statusRow.AddStretch()
	p.elapsedLabel = qt.NewQLabel3("00:00:00")
	p.elapsedLabel.SetStyleSheet("QLabel { color: #cdd6f4; font-size: 20px; font-weight: bold; font-family: monospace; }")
	statusRow.AddWidget(p.elapsedLabel.QFrame.QWidget)
	rightLayout.AddLayout(statusRow.QLayout)

	// Live webcam preview grid
	p.webcamGrid = widgets.NewWebcamGrid(p.webcamDevs)
	p.webcamGrid.Widget().SetMinimumHeight(180)
	rightLayout.AddWidget(p.webcamGrid.Widget())

	// Auto-start webcam previews if webcams are detected
	if len(p.webcamDevs) > 0 {
		p.webcamGrid.StartAll()
	}

	// Control buttons
	btnRow := qt.NewQHBoxLayout2()
	btnRow.SetSpacing(10)

	p.startBtn = qt.NewQPushButton3("Start Recording")
	p.startBtn.SetStyleSheet(`
		QPushButton {
			background-color: #a6e3a1;
			color: #1e1e2e;
			border: none;
			border-radius: 8px;
			padding: 15px 30px;
			font-size: 16px;
			font-weight: bold;
		}
		QPushButton:hover { background-color: #94e2d5; }
		QPushButton:disabled { background-color: #45475a; color: #6c7086; }
	`)
	p.startBtn.OnClicked(func() { p.onStartClicked() })
	btnRow.AddWidget(p.startBtn.QAbstractButton.QWidget)

	p.pauseBtn = qt.NewQPushButton3("Pause")
	p.pauseBtn.SetStyleSheet(`
		QPushButton {
			background-color: #fab387;
			color: #1e1e2e;
			border: none;
			border-radius: 8px;
			padding: 15px 20px;
			font-size: 14px;
			font-weight: bold;
		}
		QPushButton:hover { background-color: #f9e2af; }
	`)
	p.pauseBtn.SetVisible(false)
	p.pauseBtn.OnClicked(func() { p.onPauseClicked() })
	btnRow.AddWidget(p.pauseBtn.QAbstractButton.QWidget)

	p.stopBtn = qt.NewQPushButton3("Stop")
	p.stopBtn.SetStyleSheet(`
		QPushButton {
			background-color: #f38ba8;
			color: #1e1e2e;
			border: none;
			border-radius: 8px;
			padding: 15px 20px;
			font-size: 14px;
			font-weight: bold;
		}
		QPushButton:hover { background-color: #eba0ac; }
	`)
	p.stopBtn.SetVisible(false)
	p.stopBtn.OnClicked(func() { p.onStopClicked() })
	btnRow.AddWidget(p.stopBtn.QAbstractButton.QWidget)

	rightLayout.AddLayout(btnRow.QLayout)
	rightLayout.AddStretch()
	layout.AddWidget(rightCol)
}

func (p *RecordPage) addFormRow(layout *qt.QVBoxLayout, label, placeholder, labelStyle, inputStyle string) *qt.QLineEdit {
	row := qt.NewQHBoxLayout2()
	lbl := qt.NewQLabel3(label)
	lbl.SetStyleSheet(labelStyle)
	lbl.SetFixedWidth(80)
	row.AddWidget(lbl.QFrame.QWidget)
	input := qt.NewQLineEdit2()
	input.SetPlaceholderText(placeholder)
	input.SetStyleSheet(inputStyle)
	row.AddWidget(input.QWidget)
	layout.AddLayout(row.QLayout)
	return input
}

func (p *RecordPage) setupTimers() {
	// Elapsed time timer - ticks every second while recording
	p.elapsedTimer = qt.NewQTimer()
	p.elapsedTimer.OnTimeout(func() {
		if p.state == StateRecording {
			elapsed := time.Since(p.startTime)
			h := int(elapsed.Hours())
			m := int(elapsed.Minutes()) % 60
			s := int(elapsed.Seconds()) % 60
			p.elapsedLabel.SetText(fmt.Sprintf("%02d:%02d:%02d", h, m, s))
		} else if p.state == StateCountdown {
			p.countdownVal--
			if p.countdownVal <= 0 {
				p.startRecording()
			} else {
				p.statusLabel.SetText(fmt.Sprintf("Starting in %d...", p.countdownVal))
			}
		}
	})
}

func (p *RecordPage) onStartClicked() {
	if p.state != StateIdle {
		return
	}

	// Validate title
	title := p.titleInput.Text()
	if title == "" {
		p.statusLabel.SetText("Please enter a title")
		p.statusLabel.SetStyleSheet("QLabel { color: #f38ba8; font-size: 16px; font-weight: bold; padding: 10px; }")
		return
	}

	// Start countdown
	p.state = StateCountdown
	p.countdownVal = 5
	p.statusLabel.SetText(fmt.Sprintf("Starting in %d...", p.countdownVal))
	p.statusLabel.SetStyleSheet("QLabel { color: #fab387; font-size: 16px; font-weight: bold; padding: 10px; }")
	p.startBtn.SetEnabled(false)
	p.elapsedTimer.Start(1000)
}

func (p *RecordPage) startRecording() {
	p.elapsedTimer.Stop()

	// Get monitor from picker
	monitorName := ""
	if m := p.monitorPicker.SelectedMonitor(); m != nil {
		monitorName = m.Name
	}

	// Get recording number
	number, _ := strconv.Atoi(p.numberInput.Text())
	if number == 0 {
		number = config.GetCurrentRecordingNumber()
	}

	// Build recording directory
	cfg, _ := config.Load()
	outputDir := cfg.OutputDir
	if outputDir == "" {
		outputDir = config.GetDefaultVideosDir()
	}
	timestamp := time.Now().Format("2006-01-02_15-04-05")
	recordingDir := filepath.Join(outputDir, fmt.Sprintf("%03d-%s", number, timestamp))

	// Get monitor resolution
	monitorRes := ""
	if m := p.monitorPicker.SelectedMonitor(); m != nil {
		monitorRes = fmt.Sprintf("%dx%d", m.Width, m.Height)
	}

	// Create metadata
	metadata := models.RecordingMetadata{
		Title:       p.titleInput.Text(),
		Description: p.descInput.ToPlainText(),
		Number:      number,
		Presenter:   p.presenterInput.Text(),
	}

	// Create RecordingInfo
	recordingInfo := models.NewRecordingInfo(metadata, monitorName, monitorRes)
	recordingInfo.Files.FolderPath = recordingDir
	recordingInfo.Settings.ScreenEnabled = p.screenCheck.IsChecked()
	recordingInfo.Settings.AudioEnabled = p.audioCheck.IsChecked()
	recordingInfo.Settings.WebcamEnabled = p.webcamCheck.IsChecked()
	recordingInfo.Settings.VerticalEnabled = p.verticalCheck.IsChecked()
	recordingInfo.Settings.LeftSplitEnabled = p.leftSplitCheck.IsChecked()
	recordingInfo.Settings.LogosEnabled = p.logosCheck.IsChecked()

	// Build recorder options
	noScreen := !p.screenCheck.IsChecked() || monitorName == ""
	opts := recorder.Options{
		Monitor:        monitorName,
		NoAudio:        !p.audioCheck.IsChecked(),
		NoWebcam:       !p.webcamCheck.IsChecked(),
		NoScreen:       noScreen,
		OutputDir:      recordingDir,
		RecordingInfo:  recordingInfo,
		CreateVertical: p.verticalCheck.IsChecked(),
	}

	// Start recording in goroutine to not block the UI
	go func() {
		err := p.rec.StartWithOptions(opts)
		if err != nil {
			// Use QTimer.singleShot equivalent to update UI from main thread
			runOnUI(func() {
				p.statusLabel.SetText(fmt.Sprintf("Error: %v", err))
				p.statusLabel.SetStyleSheet("QLabel { color: #f38ba8; font-size: 16px; font-weight: bold; padding: 10px; }")
				p.resetToIdle()
			})
			return
		}

		runOnUI(func() {
			p.state = StateRecording
			p.startTime = time.Now()
			p.statusLabel.SetText("Recording")
			p.statusLabel.SetStyleSheet("QLabel { color: #f38ba8; font-size: 16px; font-weight: bold; padding: 10px; }")
			p.startBtn.SetVisible(false)
			p.pauseBtn.SetVisible(true)
			p.stopBtn.SetVisible(true)
			p.elapsedTimer.Start(1000)
			if p.onStatusChange != nil {
				p.onStatusChange("Recording")
			}
		})
	}()
}

func (p *RecordPage) onPauseClicked() {
	if p.state == StateRecording {
		go func() {
			err := p.rec.Pause()
			runOnUI(func() {
				if err != nil {
					p.statusLabel.SetText(fmt.Sprintf("Pause error: %v", err))
					return
				}
				p.state = StatePaused
				p.elapsedTimer.Stop()
				p.statusLabel.SetText("Paused")
				p.statusLabel.SetStyleSheet("QLabel { color: #fab387; font-size: 16px; font-weight: bold; padding: 10px; }")
				p.pauseBtn.SetText("Resume")
				if p.onStatusChange != nil {
					p.onStatusChange("Paused")
				}
			})
		}()
	} else if p.state == StatePaused {
		go func() {
			err := p.rec.Resume()
			runOnUI(func() {
				if err != nil {
					p.statusLabel.SetText(fmt.Sprintf("Resume error: %v", err))
					return
				}
				p.state = StateRecording
				p.elapsedTimer.Start(1000)
				p.statusLabel.SetText("Recording")
				p.statusLabel.SetStyleSheet("QLabel { color: #f38ba8; font-size: 16px; font-weight: bold; padding: 10px; }")
				p.pauseBtn.SetText("Pause")
				if p.onStatusChange != nil {
					p.onStatusChange("Recording")
				}
			})
		}()
	}
}

func (p *RecordPage) onStopClicked() {
	if p.state != StateRecording && p.state != StatePaused {
		return
	}

	p.statusLabel.SetText("Stopping...")
	p.statusLabel.SetStyleSheet("QLabel { color: #fab387; font-size: 16px; font-weight: bold; padding: 10px; }")
	p.stopBtn.SetEnabled(false)
	p.pauseBtn.SetEnabled(false)

	go func() {
		err := p.rec.StopAndProcess(true)
		runOnUI(func() {
			if err != nil {
				p.statusLabel.SetText(fmt.Sprintf("Error: %v", err))
				p.statusLabel.SetStyleSheet("QLabel { color: #f38ba8; font-size: 16px; font-weight: bold; padding: 10px; }")
			} else {
				p.statusLabel.SetText("Recording complete!")
				p.statusLabel.SetStyleSheet("QLabel { color: #a6e3a1; font-size: 16px; font-weight: bold; padding: 10px; }")
			}
			p.resetToIdle()
			if p.onStatusChange != nil {
				p.onStatusChange("Idle")
			}
		})
	}()
}

func (p *RecordPage) resetToIdle() {
	p.state = StateIdle
	p.elapsedTimer.Stop()
	p.elapsedLabel.SetText("00:00:00")
	p.startBtn.SetVisible(true)
	p.startBtn.SetEnabled(true)
	p.pauseBtn.SetVisible(false)
	p.pauseBtn.SetEnabled(true)
	p.pauseBtn.SetText("Pause")
	p.stopBtn.SetVisible(false)
	p.stopBtn.SetEnabled(true)

	// Increment recording number
	num, _ := strconv.Atoi(p.numberInput.Text())
	p.numberInput.SetText(fmt.Sprintf("%03d", num+1))
}

// Widget returns the underlying QWidget
func (p *RecordPage) Widget() *qt.QWidget {
	return p.widget
}

// TriggerPause pauses the recording (called from tray)
func (p *RecordPage) TriggerPause() {
	if p.state == StateRecording {
		p.onPauseClicked()
	}
}

// TriggerResume resumes the recording (called from tray)
func (p *RecordPage) TriggerResume() {
	if p.state == StatePaused {
		p.onPauseClicked() // onPauseClicked handles both pause and resume
	}
}

// TriggerStop stops the recording (called from tray)
func (p *RecordPage) TriggerStop() {
	p.onStopClicked()
}

// StopPreviews stops all webcam preview captures
func (p *RecordPage) StopPreviews() {
	if p.webcamGrid != nil {
		p.webcamGrid.StopAll()
	}
}
