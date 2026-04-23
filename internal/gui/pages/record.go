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
	audioCheck     *qt.QCheckBox
	webcamCheck    *qt.QCheckBox
	screenCheck    *qt.QCheckBox
	verticalCheck  *qt.QCheckBox
	leftSplitCheck *qt.QCheckBox
	logosCheck     *qt.QCheckBox

	// Logo drop zones
	leftLogo   *widgets.LogoDropZone
	rightLogo  *widgets.LogoDropZone
	bottomLogo *widgets.LogoDropZone

	// Central canvas
	canvas        *widgets.RecordingCanvas
	monitorPicker *widgets.MonitorPicker
	webcamGrid    *widgets.WebcamGrid

	// Controls
	startBtn *qt.QPushButton
	pauseBtn *qt.QPushButton
	stopBtn  *qt.QPushButton

	// Status
	elapsedLabel *qt.QLabel
	statusLabel  *qt.QLabel

	// State
	state        RecordingState
	rec          *recorder.Recorder
	monitors     []models.Monitor
	webcamDevs   []webcam.DeviceInfo
	elapsedTimer *qt.QTimer
	startTime    time.Time
	countdownVal int

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
	layout.SetSpacing(12)
	layout.SetContentsMargins(10, 10, 10, 10)

	cfg, _ := config.Load()
	presets := cfg.RecordingPresets
	if !cfg.PresetsConfigured {
		presets = config.DefaultRecordingPresets()
	}

	labelStyle := "QLabel { color: #cdd6f4; font-size: 12px; }"
	inputStyle := "QLineEdit { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 5px; font-size: 12px; }"
	checkStyle := "QCheckBox { color: #cdd6f4; font-size: 12px; } QCheckBox::indicator { width: 14px; height: 14px; }"
	sectionStyle := "QLabel { color: #89b4fa; font-size: 13px; font-weight: bold; padding-top: 6px; }"

	// === Left sidebar: compact form ===
	leftScroll := qt.NewQScrollArea2()
	leftScroll.SetWidgetResizable(true)
	leftScroll.SetFixedWidth(240)
	leftScroll.SetStyleSheet("QScrollArea { border: none; background: transparent; }")

	leftCol := qt.NewQWidget2()
	leftLayout := qt.NewQVBoxLayout(leftCol)
	leftLayout.SetSpacing(5)
	leftLayout.SetContentsMargins(0, 0, 5, 0)

	// Title — also updates the canvas preview
	p.titleInput = p.addFormRow(leftLayout, "Title:", "Recording title...", labelStyle, inputStyle)
	p.titleInput.SetToolTip("Title for this recording.\nUsed in output filenames.\nDraggable on the canvas preview.")
	p.titleInput.OnTextChanged(func(text string) {
		if p.canvas != nil {
			p.canvas.SetTitle(text)
		}
	})

	// Number
	p.numberInput = p.addFormRow(leftLayout, "Number:", "001", labelStyle, inputStyle)
	p.numberInput.SetText(fmt.Sprintf("%03d", config.GetCurrentRecordingNumber()))
	p.numberInput.SetToolTip("Sequential recording number.")

	// Presenter
	p.presenterInput = p.addFormRow(leftLayout, "Presenter:", "Name...", labelStyle, inputStyle)
	if cfg.DefaultPresenter != "" {
		p.presenterInput.SetText(cfg.DefaultPresenter)
	}
	p.presenterInput.SetToolTip("Presenter name for metadata.")

	// Sources
	srcLabel := qt.NewQLabel3("Sources")
	srcLabel.SetStyleSheet(sectionStyle)
	leftLayout.AddWidget(srcLabel.QFrame.QWidget)

	p.screenCheck = qt.NewQCheckBox3("Screen")
	p.screenCheck.SetChecked(presets.RecordScreen)
	p.screenCheck.SetStyleSheet(checkStyle)
	p.screenCheck.SetToolTip("Record the selected monitor.")
	leftLayout.AddWidget(p.screenCheck.QAbstractButton.QWidget)

	p.audioCheck = qt.NewQCheckBox3("Audio")
	p.audioCheck.SetChecked(presets.RecordAudio)
	p.audioCheck.SetStyleSheet(checkStyle)
	p.audioCheck.SetToolTip("Record microphone audio.\nProcessed after recording.")
	leftLayout.AddWidget(p.audioCheck.QAbstractButton.QWidget)

	p.webcamCheck = qt.NewQCheckBox3("Webcam")
	p.webcamCheck.SetChecked(presets.RecordWebcam)
	p.webcamCheck.SetStyleSheet(checkStyle)
	p.webcamCheck.SetToolTip("Record from webcam cameras.")
	leftLayout.AddWidget(p.webcamCheck.QAbstractButton.QWidget)

	// Output
	outLabel := qt.NewQLabel3("Output")
	outLabel.SetStyleSheet(sectionStyle)
	leftLayout.AddWidget(outLabel.QFrame.QWidget)

	p.verticalCheck = qt.NewQCheckBox3("Vertical video")
	p.verticalCheck.SetChecked(presets.VerticalVideo)
	p.verticalCheck.SetStyleSheet(checkStyle)
	p.verticalCheck.SetToolTip("Create 9:16 vertical video\nfor Shorts/Reels/TikTok.")
	p.verticalCheck.OnStateChanged(func(state int) {
		if p.canvas != nil {
			p.canvas.SetVertical(state == 2)
		}
	})
	leftLayout.AddWidget(p.verticalCheck.QAbstractButton.QWidget)

	// Split side radio buttons
	splitRow := qt.NewQHBoxLayout2()
	radioStyle := "QRadioButton { color: #cdd6f4; font-size: 12px; } QRadioButton::indicator { width: 14px; height: 14px; }"

	leftSplitRadio := qt.NewQRadioButton3("Left split")
	leftSplitRadio.SetStyleSheet(radioStyle)
	leftSplitRadio.SetToolTip("Show left half of screen\nin vertical video.")
	leftSplitRadio.SetChecked(presets.LeftSplit)
	splitRow.AddWidget(leftSplitRadio.QAbstractButton.QWidget)

	rightSplitRadio := qt.NewQRadioButton3("Right split")
	rightSplitRadio.SetStyleSheet(radioStyle)
	rightSplitRadio.SetToolTip("Show right half of screen\nin vertical video.")
	rightSplitRadio.SetChecked(!presets.LeftSplit)
	splitRow.AddWidget(rightSplitRadio.QAbstractButton.QWidget)

	leftSplitRadio.OnClicked(func() {
		if p.canvas != nil {
			p.canvas.SetLeftSplit(true)
			p.canvas.SetSplitSide(widgets.SplitLeft)
		}
	})
	rightSplitRadio.OnClicked(func() {
		if p.canvas != nil {
			p.canvas.SetLeftSplit(true)
			p.canvas.SetSplitSide(widgets.SplitRight)
		}
	})

	// Keep a reference for startRecording
	p.leftSplitCheck = qt.NewQCheckBox2() // hidden, tracks state
	p.leftSplitCheck.SetChecked(presets.LeftSplit)
	p.leftSplitCheck.SetVisible(false)
	leftSplitRadio.OnClicked(func() { p.leftSplitCheck.SetChecked(true) })
	rightSplitRadio.OnClicked(func() { p.leftSplitCheck.SetChecked(true) }) // both mean split is enabled

	leftLayout.AddLayout(splitRow.QLayout)

	p.logosCheck = qt.NewQCheckBox3("Logos")
	p.logosCheck.SetChecked(presets.AddLogos)
	p.logosCheck.SetStyleSheet(checkStyle)
	p.logosCheck.SetToolTip("Add logo overlays.\nDrag logos onto the canvas.")
	leftLayout.AddWidget(p.logosCheck.QAbstractButton.QWidget)

	// Logo drop zones (compact, vertical stack)
	logoLabel := qt.NewQLabel3("Logos")
	logoLabel.SetStyleSheet(sectionStyle)
	leftLayout.AddWidget(logoLabel.QFrame.QWidget)

	p.leftLogo = widgets.NewLogoDropZone("Left")
	p.leftLogo.OnChange(func(path string) {
		p.canvas.RemoveLogo(widgets.ItemLogoLeft)
		if path != "" {
			p.canvas.AddLogo(widgets.ItemLogoLeft, path)
		}
	})
	leftLayout.AddWidget(p.leftLogo.Widget())

	p.rightLogo = widgets.NewLogoDropZone("Right")
	p.rightLogo.OnChange(func(path string) {
		p.canvas.RemoveLogo(widgets.ItemLogoRight)
		if path != "" {
			p.canvas.AddLogo(widgets.ItemLogoRight, path)
		}
	})
	leftLayout.AddWidget(p.rightLogo.Widget())

	p.bottomLogo = widgets.NewLogoDropZone("Banner")
	p.bottomLogo.OnChange(func(path string) {
		p.canvas.RemoveLogo(widgets.ItemLogoBanner)
		if path != "" {
			p.canvas.AddLogo(widgets.ItemLogoBanner, path)
		}
	})
	leftLayout.AddWidget(p.bottomLogo.Widget())

	// Pre-fill logos
	if cfg.LastUsedLogos.LeftLogo != "" {
		p.leftLogo.SetFile(cfg.LastUsedLogos.LeftLogo)
	}
	if cfg.LastUsedLogos.RightLogo != "" {
		p.rightLogo.SetFile(cfg.LastUsedLogos.RightLogo)
	}
	if cfg.LastUsedLogos.BottomLogo != "" {
		p.bottomLogo.SetFile(cfg.LastUsedLogos.BottomLogo)
	}

	// Description
	descLabel := qt.NewQLabel3("Description")
	descLabel.SetStyleSheet(sectionStyle)
	leftLayout.AddWidget(descLabel.QFrame.QWidget)
	p.descInput = qt.NewQTextEdit2()
	p.descInput.SetPlaceholderText("Optional...")
	p.descInput.SetMaximumHeight(60)
	p.descInput.SetStyleSheet("QTextEdit { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 3px; font-size: 12px; }")
	p.descInput.SetToolTip("Optional recording description.")
	leftLayout.AddWidget(p.descInput.QAbstractScrollArea.QFrame.QWidget)

	leftLayout.AddStretch()
	leftScroll.SetWidget(leftCol)
	layout.AddWidget(leftScroll.QAbstractScrollArea.QFrame.QWidget)

	// === Center: WYSIWYG canvas + controls ===
	centerCol := qt.NewQWidget2()
	centerLayout := qt.NewQVBoxLayout(centerCol)
	centerLayout.SetSpacing(8)
	centerLayout.SetContentsMargins(0, 0, 0, 0)

	// Canvas
	p.canvas = widgets.NewRecordingCanvas()
	p.canvas.Widget().SetToolTip("WYSIWYG preview of your video output.\nDrag webcam bubbles and logos to position them.\nLive screen preview updates every 2 seconds.")
	centerLayout.AddWidget3(p.canvas.Widget(), 1, qt.AlignHCenter)

	// Add webcams to canvas
	for i, dev := range p.webcamDevs {
		p.canvas.AddWebcam(dev.Device, dev.Name, i)
	}

	// Add pre-filled logos to canvas
	if cfg.LastUsedLogos.LeftLogo != "" {
		p.canvas.AddLogo(widgets.ItemLogoLeft, cfg.LastUsedLogos.LeftLogo)
	}
	if cfg.LastUsedLogos.RightLogo != "" {
		p.canvas.AddLogo(widgets.ItemLogoRight, cfg.LastUsedLogos.RightLogo)
	}
	if cfg.LastUsedLogos.BottomLogo != "" {
		p.canvas.AddLogo(widgets.ItemLogoBanner, cfg.LastUsedLogos.BottomLogo)
	}

	// Set initial monitor on canvas
	for _, m := range p.monitors {
		if m.Focused {
			mon := m
			p.canvas.SetMonitor(&mon)
			break
		}
	}

	// Monitor picker below canvas
	monLabel := qt.NewQLabel3("Monitor")
	monLabel.SetStyleSheet("QLabel { color: #89b4fa; font-size: 12px; font-weight: bold; }")
	centerLayout.AddWidget(monLabel.QFrame.QWidget)

	p.monitorPicker = widgets.NewMonitorPicker(p.monitors)
	p.monitorPicker.Widget().SetToolTip("Select monitor to record.\nChoose 'No Screen' for webcam-only.")
	p.monitorPicker.OnSelected(func(idx int) {
		mon := p.monitorPicker.SelectedMonitor()
		p.canvas.SetMonitor(mon)
	})
	centerLayout.AddWidget(p.monitorPicker.Widget())

	// Status + elapsed + controls row
	statusRow := qt.NewQHBoxLayout2()
	p.statusLabel = qt.NewQLabel3("Ready")
	p.statusLabel.SetStyleSheet("QLabel { color: #a6e3a1; font-size: 13px; font-weight: bold; }")
	statusRow.AddWidget(p.statusLabel.QFrame.QWidget)
	statusRow.AddStretch()
	p.elapsedLabel = qt.NewQLabel3("00:00:00")
	p.elapsedLabel.SetStyleSheet("QLabel { color: #cdd6f4; font-size: 18px; font-weight: bold; font-family: monospace; }")
	statusRow.AddWidget(p.elapsedLabel.QFrame.QWidget)
	centerLayout.AddLayout(statusRow.QLayout)

	// Buttons
	btnRow := qt.NewQHBoxLayout2()
	btnRow.SetSpacing(8)

	p.startBtn = qt.NewQPushButton3("Start Recording")
	p.startBtn.SetStyleSheet("QPushButton { background: #a6e3a1; color: #1e1e2e; border: none; border-radius: 6px; padding: 10px 24px; font-size: 14px; font-weight: bold; } QPushButton:hover { background: #94e2d5; } QPushButton:disabled { background: #45475a; color: #6c7086; }")
	p.startBtn.OnClicked(func() { p.onStartClicked() })
	btnRow.AddWidget(p.startBtn.QAbstractButton.QWidget)

	p.pauseBtn = qt.NewQPushButton3("Pause")
	p.pauseBtn.SetStyleSheet("QPushButton { background: #fab387; color: #1e1e2e; border: none; border-radius: 6px; padding: 10px 16px; font-size: 13px; font-weight: bold; } QPushButton:hover { background: #f9e2af; }")
	p.pauseBtn.SetVisible(false)
	p.pauseBtn.OnClicked(func() { p.onPauseClicked() })
	btnRow.AddWidget(p.pauseBtn.QAbstractButton.QWidget)

	p.stopBtn = qt.NewQPushButton3("Stop")
	p.stopBtn.SetStyleSheet("QPushButton { background: #f38ba8; color: #1e1e2e; border: none; border-radius: 6px; padding: 10px 16px; font-size: 13px; font-weight: bold; } QPushButton:hover { background: #eba0ac; }")
	p.stopBtn.SetVisible(false)
	p.stopBtn.OnClicked(func() { p.onStopClicked() })
	btnRow.AddWidget(p.stopBtn.QAbstractButton.QWidget)

	centerLayout.AddLayout(btnRow.QLayout)

	layout.AddWidget2(centerCol, 1)

	// === Right panel: webcam previews ===
	if len(p.webcamDevs) > 0 {
		rightCol := qt.NewQWidget2()
		rightCol.SetFixedWidth(200)
		rightLayout := qt.NewQVBoxLayout(rightCol)
		rightLayout.SetSpacing(5)
		rightLayout.SetContentsMargins(5, 0, 0, 0)

		camLabel := qt.NewQLabel3("Cameras")
		camLabel.SetStyleSheet("QLabel { color: #89b4fa; font-size: 12px; font-weight: bold; }")
		rightLayout.AddWidget(camLabel.QFrame.QWidget)

		p.webcamGrid = widgets.NewWebcamGrid(p.webcamDevs)
		rightLayout.AddWidget(p.webcamGrid.Widget())
		p.webcamGrid.StartAll()

		rightLayout.AddStretch()
		layout.AddWidget(rightCol)
	}
}

func (p *RecordPage) addFormRow(layout *qt.QVBoxLayout, label, placeholder, labelStyle, inputStyle string) *qt.QLineEdit {
	row := qt.NewQHBoxLayout2()
	lbl := qt.NewQLabel3(label)
	lbl.SetStyleSheet(labelStyle)
	lbl.SetFixedWidth(65)
	row.AddWidget(lbl.QFrame.QWidget)
	input := qt.NewQLineEdit2()
	input.SetPlaceholderText(placeholder)
	input.SetStyleSheet(inputStyle)
	row.AddWidget(input.QWidget)
	layout.AddLayout(row.QLayout)
	return input
}

func (p *RecordPage) setupTimers() {
	p.elapsedTimer = qt.NewQTimer()
	p.elapsedTimer.OnTimeout(func() {
		switch p.state {
		case StateRecording:
			elapsed := time.Since(p.startTime)
			h := int(elapsed.Hours())
			m := int(elapsed.Minutes()) % 60
			s := int(elapsed.Seconds()) % 60
			p.elapsedLabel.SetText(fmt.Sprintf("%02d:%02d:%02d", h, m, s))
		case StateCountdown:
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
	title := p.titleInput.Text()
	if title == "" {
		p.statusLabel.SetText("Enter a title")
		p.statusLabel.SetStyleSheet("QLabel { color: #f38ba8; font-size: 13px; font-weight: bold; }")
		return
	}
	p.state = StateCountdown
	p.countdownVal = 5
	p.statusLabel.SetText("Starting in 5...")
	p.statusLabel.SetStyleSheet("QLabel { color: #fab387; font-size: 13px; font-weight: bold; }")
	p.startBtn.SetEnabled(false)
	p.elapsedTimer.Start(1000)
}

func (p *RecordPage) startRecording() {
	p.elapsedTimer.Stop()

	monitorName := ""
	monitorRes := ""
	if m := p.monitorPicker.SelectedMonitor(); m != nil {
		monitorName = m.Name
		monitorRes = fmt.Sprintf("%dx%d", m.Width, m.Height)
	}

	number, _ := strconv.Atoi(p.numberInput.Text())
	if number == 0 {
		number = config.GetCurrentRecordingNumber()
	}

	cfg, _ := config.Load()
	outputDir := cfg.OutputDir
	if outputDir == "" {
		outputDir = config.GetDefaultVideosDir()
	}
	timestamp := time.Now().Format("2006-01-02_15-04-05")
	recordingDir := filepath.Join(outputDir, fmt.Sprintf("%03d-%s", number, timestamp))

	metadata := models.RecordingMetadata{
		Title:       p.titleInput.Text(),
		Description: p.descInput.ToPlainText(),
		Number:      number,
		Presenter:   p.presenterInput.Text(),
	}

	recordingInfo := models.NewRecordingInfo(metadata, monitorName, monitorRes)
	recordingInfo.Files.FolderPath = recordingDir
	recordingInfo.Settings.ScreenEnabled = p.screenCheck.IsChecked()
	recordingInfo.Settings.AudioEnabled = p.audioCheck.IsChecked()
	recordingInfo.Settings.WebcamEnabled = p.webcamCheck.IsChecked()
	recordingInfo.Settings.VerticalEnabled = p.verticalCheck.IsChecked()
	recordingInfo.Settings.LeftSplitEnabled = p.leftSplitCheck.IsChecked()
	recordingInfo.Settings.LogosEnabled = p.logosCheck.IsChecked()

	logoSelection := config.LogoSelection{}
	if p.logosCheck.IsChecked() {
		logoSelection.LeftLogo = p.leftLogo.FilePath()
		logoSelection.RightLogo = p.rightLogo.FilePath()
		logoSelection.BottomLogo = p.bottomLogo.FilePath()
		recordingInfo.Settings.LeftLogo = logoSelection.LeftLogo
		recordingInfo.Settings.RightLogo = logoSelection.RightLogo
		recordingInfo.Settings.BottomLogo = logoSelection.BottomLogo
	}

	noScreen := !p.screenCheck.IsChecked() || monitorName == ""
	opts := recorder.Options{
		Monitor:        monitorName,
		NoAudio:        !p.audioCheck.IsChecked(),
		NoWebcam:       !p.webcamCheck.IsChecked(),
		NoScreen:       noScreen,
		OutputDir:      recordingDir,
		RecordingInfo:  recordingInfo,
		CreateVertical: p.verticalCheck.IsChecked(),
		LogoSelection:  logoSelection,
	}

	go func() {
		err := p.rec.StartWithOptions(opts)
		if err != nil {
			runOnUI(func() {
				p.statusLabel.SetText(fmt.Sprintf("Error: %v", err))
				p.statusLabel.SetStyleSheet("QLabel { color: #f38ba8; font-size: 13px; font-weight: bold; }")
				p.resetToIdle()
			})
			return
		}
		runOnUI(func() {
			p.state = StateRecording
			p.startTime = time.Now()
			p.statusLabel.SetText("Recording")
			p.statusLabel.SetStyleSheet("QLabel { color: #f38ba8; font-size: 13px; font-weight: bold; }")
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
	switch p.state {
	case StateRecording:
		go func() {
			err := p.rec.Pause()
			runOnUI(func() {
				if err != nil {
					p.statusLabel.SetText(fmt.Sprintf("Error: %v", err))
					return
				}
				p.state = StatePaused
				p.elapsedTimer.Stop()
				p.statusLabel.SetText("Paused")
				p.statusLabel.SetStyleSheet("QLabel { color: #fab387; font-size: 13px; font-weight: bold; }")
				p.pauseBtn.SetText("Resume")
				if p.onStatusChange != nil {
					p.onStatusChange("Paused")
				}
			})
		}()
	case StatePaused:
		go func() {
			err := p.rec.Resume()
			runOnUI(func() {
				if err != nil {
					p.statusLabel.SetText(fmt.Sprintf("Error: %v", err))
					return
				}
				p.state = StateRecording
				p.elapsedTimer.Start(1000)
				p.statusLabel.SetText("Recording")
				p.statusLabel.SetStyleSheet("QLabel { color: #f38ba8; font-size: 13px; font-weight: bold; }")
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
	p.statusLabel.SetStyleSheet("QLabel { color: #fab387; font-size: 13px; font-weight: bold; }")
	p.stopBtn.SetEnabled(false)
	p.pauseBtn.SetEnabled(false)

	go func() {
		err := p.rec.StopAndProcess(true)
		runOnUI(func() {
			if err != nil {
				p.statusLabel.SetText(fmt.Sprintf("Error: %v", err))
				p.statusLabel.SetStyleSheet("QLabel { color: #f38ba8; font-size: 13px; font-weight: bold; }")
			} else {
				p.statusLabel.SetText("Complete!")
				p.statusLabel.SetStyleSheet("QLabel { color: #a6e3a1; font-size: 13px; font-weight: bold; }")
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
		p.onPauseClicked()
	}
}

// TriggerStop stops the recording (called from tray)
func (p *RecordPage) TriggerStop() {
	p.onStopClicked()
}

// StopPreviews stops all webcam preview captures and screen refresh
func (p *RecordPage) StopPreviews() {
	if p.webcamGrid != nil {
		p.webcamGrid.StopAll()
	}
	if p.canvas != nil {
		p.canvas.Stop()
	}
	if p.monitorPicker != nil {
		p.monitorPicker.Stop()
	}
}
