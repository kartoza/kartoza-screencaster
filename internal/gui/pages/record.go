package pages

import (
	"fmt"
	"os"
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
	audioCheck     *qt.QCheckBox

	// Central canvas
	canvas    *widgets.RecordingCanvas
	layerList *qt.QListWidget

	// Add element menu
	addMenu *qt.QMenu

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

	// Track what's been added
	hasScreen   bool
	screenMonitor *models.Monitor
	leftSplit   bool

	// Callbacks
	onStatusChange func(string)
	onNavigate     func(int) // navigate to a page (e.g., processing, player)
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

// SetNavigateCallback sets a callback for page navigation (used to switch to processing/player)
func (p *RecordPage) SetNavigateCallback(cb func(int)) {
	p.onNavigate = cb
}

// GetProgressChannel creates a progress channel and starts processing
func (p *RecordPage) GetProgressChannel() <-chan recorder.ProgressUpdate {
	ch := make(chan recorder.ProgressUpdate, 20)
	go p.rec.ProcessWithProgress(ch)
	return ch
}

func (p *RecordPage) setupUI() {
	layout := qt.NewQHBoxLayout(p.widget)
	layout.SetSpacing(10)
	layout.SetContentsMargins(10, 10, 10, 10)

	cfg, _ := config.Load()

	labelStyle := "QLabel { color: #cdd6f4; font-size: 12px; }"
	inputStyle := "QLineEdit { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 5px; font-size: 12px; }"

	// === Left sidebar: minimal metadata ===
	leftCol := qt.NewQWidget2()
	leftCol.SetFixedWidth(220)
	leftLayout := qt.NewQVBoxLayout(leftCol)
	leftLayout.SetSpacing(6)
	leftLayout.SetContentsMargins(0, 0, 5, 0)

	metaLabel := qt.NewQLabel3("Recording")
	metaLabel.SetStyleSheet("QLabel { color: #89b4fa; font-size: 14px; font-weight: bold; }")
	leftLayout.AddWidget(metaLabel.QFrame.QWidget)

	p.titleInput = p.addFormRow(leftLayout, "Title:", "Title...", labelStyle, inputStyle)
	p.titleInput.SetToolTip("Recording title.\nAppears on the canvas and in filenames.")
	p.titleInput.OnTextChanged(func(text string) {
		if p.canvas != nil {
			p.canvas.SetTitle(text)
		}
	})

	p.numberInput = p.addFormRow(leftLayout, "Number:", "001", labelStyle, inputStyle)
	p.numberInput.SetText(fmt.Sprintf("%03d", config.GetCurrentRecordingNumber()))
	p.numberInput.SetToolTip("Sequential recording number.")

	p.presenterInput = p.addFormRow(leftLayout, "Presenter:", "Name...", labelStyle, inputStyle)
	if cfg.DefaultPresenter != "" {
		p.presenterInput.SetText(cfg.DefaultPresenter)
	}
	p.presenterInput.SetToolTip("Presenter name.")

	p.audioCheck = qt.NewQCheckBox3("Record Audio")
	p.audioCheck.SetChecked(true)
	p.audioCheck.SetStyleSheet("QCheckBox { color: #cdd6f4; font-size: 12px; } QCheckBox::indicator { width: 14px; height: 14px; }")
	p.audioCheck.SetToolTip("Record microphone audio.")
	leftLayout.AddWidget(p.audioCheck.QAbstractButton.QWidget)

	descLabel := qt.NewQLabel3("Description")
	descLabel.SetStyleSheet("QLabel { color: #89b4fa; font-size: 12px; font-weight: bold; padding-top: 6px; }")
	leftLayout.AddWidget(descLabel.QFrame.QWidget)

	p.descInput = qt.NewQTextEdit2()
	p.descInput.SetPlaceholderText("Optional...")
	p.descInput.SetMaximumHeight(60)
	p.descInput.SetStyleSheet("QTextEdit { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 3px; font-size: 12px; }")
	p.descInput.SetToolTip("Optional recording description.")
	leftLayout.AddWidget(p.descInput.QAbstractScrollArea.QFrame.QWidget)

	// Layout mode
	modeLabel := qt.NewQLabel3("Canvas Mode")
	modeLabel.SetStyleSheet("QLabel { color: #89b4fa; font-size: 12px; font-weight: bold; padding-top: 6px; }")
	leftLayout.AddWidget(modeLabel.QFrame.QWidget)

	radioStyle := "QRadioButton { color: #cdd6f4; font-size: 12px; } QRadioButton::indicator { width: 14px; height: 14px; }"

	modeGroup := qt.NewQButtonGroup()

	landscapeRadio := qt.NewQRadioButton3("Landscape 16:9")
	landscapeRadio.SetChecked(true)
	landscapeRadio.SetStyleSheet(radioStyle)
	landscapeRadio.SetToolTip("Standard widescreen layout.")
	modeGroup.AddButton2(landscapeRadio.QAbstractButton, 0)
	leftLayout.AddWidget(landscapeRadio.QAbstractButton.QWidget)

	verticalRadio := qt.NewQRadioButton3("Vertical 9:16")
	verticalRadio.SetStyleSheet(radioStyle)
	verticalRadio.SetToolTip("Vertical layout for Shorts/Reels.")
	modeGroup.AddButton2(verticalRadio.QAbstractButton, 1)
	leftLayout.AddWidget(verticalRadio.QAbstractButton.QWidget)

	leftSplitRadio := qt.NewQRadioButton3("Vertical (Left Split)")
	leftSplitRadio.SetStyleSheet(radioStyle)
	leftSplitRadio.SetToolTip("Left half of screen fills\nvertical frame.")
	modeGroup.AddButton2(leftSplitRadio.QAbstractButton, 2)
	leftLayout.AddWidget(leftSplitRadio.QAbstractButton.QWidget)

	rightSplitRadio := qt.NewQRadioButton3("Vertical (Right Split)")
	rightSplitRadio.SetStyleSheet(radioStyle)
	rightSplitRadio.SetToolTip("Right half of screen fills\nvertical frame.")
	modeGroup.AddButton2(rightSplitRadio.QAbstractButton, 3)
	leftLayout.AddWidget(rightSplitRadio.QAbstractButton.QWidget)

	modeGroup.OnButtonClicked(func(btn *qt.QAbstractButton) {
		id := modeGroup.Id(btn)
		switch id {
		case 0: // Landscape
			p.leftSplit = false
			p.canvas.SetVertical(false)
			p.canvas.SetLeftSplit(false)
		case 1: // Vertical
			p.leftSplit = false
			p.canvas.SetVertical(true)
			p.canvas.SetLeftSplit(false)
		case 2: // Left split
			p.leftSplit = true
			p.canvas.SetVertical(true)
			p.canvas.SetLeftSplit(true)
			p.canvas.SetSplitSide(widgets.SplitLeft)
		case 3: // Right split
			p.leftSplit = true
			p.canvas.SetVertical(true)
			p.canvas.SetLeftSplit(true)
			p.canvas.SetSplitSide(widgets.SplitRight)
		}
	})

	// === Layer list ===
	layerLabel := qt.NewQLabel3("Layers")
	layerLabel.SetStyleSheet("QLabel { color: #89b4fa; font-size: 12px; font-weight: bold; padding-top: 6px; }")
	layerLabel.SetToolTip("Canvas elements in draw order.\nDrag to reorder (bottom = drawn last = on top).\nPress Delete to remove selected item.")
	leftLayout.AddWidget(layerLabel.QFrame.QWidget)

	p.layerList = qt.NewQListWidget2()
	p.layerList.SetStyleSheet(`
		QListWidget {
			background: #313244;
			color: #cdd6f4;
			border: 1px solid #45475a;
			border-radius: 4px;
			font-size: 11px;
		}
		QListWidget::item { padding: 4px 8px; }
		QListWidget::item:selected { background: #45475a; }
		QListWidget::item:hover { background: #3b3b52; }
	`)
	p.layerList.SetDragDropMode(qt.QAbstractItemView__InternalMove)
	p.layerList.SetDefaultDropAction(qt.MoveAction)
	p.layerList.SetMaximumHeight(150)
	p.layerList.SetToolTip("Drag items to change draw order.\nPress Delete to remove.")
	leftLayout.AddWidget(p.layerList.QListView.QAbstractItemView.QAbstractScrollArea.QFrame.QWidget)

	// After drag-drop reorder, sync back to canvas
	p.layerList.OnDropEvent(func(super func(event *qt.QDropEvent), event *qt.QDropEvent) {
		super(event) // let Qt handle the visual reorder
		// Rebuild canvas item order from list
		p.syncLayerOrderToCanvas()
	})

	// Delete key on layer list
	p.layerList.OnKeyPressEvent(func(super func(event *qt.QKeyEvent), event *qt.QKeyEvent) {
		if event.Key() == int(qt.Key_Delete) {
			row := int(p.layerList.CurrentRow())
			if row >= 0 {
				p.canvas.RemoveItem(row)
				p.layerList.TakeItem(row)
			}
		} else {
			super(event)
		}
	})

	// GIF loop controls (shown when a GIF is selected)
	gifBox := qt.NewQWidget2()
	gifLayout := qt.NewQHBoxLayout(gifBox)
	gifLayout.SetContentsMargins(0, 0, 0, 0)
	gifLayout.SetSpacing(4)

	gifLabel := qt.NewQLabel3("GIF:")
	gifLabel.SetStyleSheet("QLabel { color: #6c7086; font-size: 10px; }")
	gifLayout.AddWidget(gifLabel.QFrame.QWidget)

	gifCombo := qt.NewQComboBox2()
	gifCombo.SetStyleSheet("QComboBox { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 3px; padding: 2px; font-size: 10px; } QComboBox QAbstractItemView { background: #313244; color: #cdd6f4; }")
	gifCombo.AddItem("Disabled")        // 0
	gifCombo.AddItem("Once")            // 1
	gifCombo.AddItem("Continuous")      // 2
	gifCombo.AddItem("Count")           // 3
	gifCombo.AddItem("Once then hide")  // 4
	gifCombo.AddItem("Count then hide") // 5
	gifCombo.SetCurrentIndex(2) // default continuous

	gifCountSpin := qt.NewQSpinBox2()
	gifCountSpin.SetMinimum(1)
	gifCountSpin.SetMaximum(100)
	gifCountSpin.SetValue(3)
	gifCountSpin.SetFixedWidth(50)
	gifCountSpin.SetStyleSheet("QSpinBox { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 3px; padding: 2px; font-size: 10px; }")
	gifCountSpin.SetVisible(false)
	gifCountSpin.SetToolTip("Number of times to loop the GIF.")
	gifCountSpin.OnValueChanged(func(val int) {
		row := int(p.layerList.CurrentRow())
		if row >= 0 && p.canvas.IsGifItem(row) {
			p.canvas.SetGifLoopMode(row, widgets.GifLoopCount, val)
		}
	})

	gifCombo.OnCurrentIndexChanged(func(index int) {
		row := int(p.layerList.CurrentRow())
		gifCountSpin.SetVisible(index == 3 || index == 5)
		if row < 0 || !p.canvas.IsGifItem(row) {
			return
		}
		switch index {
		case 0:
			p.canvas.SetGifLoopMode(row, widgets.GifLoopDisabled, 0)
		case 1:
			p.canvas.SetGifLoopMode(row, widgets.GifLoopOnce, 0)
		case 2:
			p.canvas.SetGifLoopMode(row, widgets.GifLoopContinuous, 0)
		case 3:
			p.canvas.SetGifLoopMode(row, widgets.GifLoopCount, gifCountSpin.Value())
		case 4:
			p.canvas.SetGifLoopMode(row, widgets.GifLoopOnceThenHide, 0)
		case 5:
			p.canvas.SetGifLoopMode(row, widgets.GifLoopCountThenHide, gifCountSpin.Value())
		}
	})
	gifLayout.AddWidget(gifCombo.QWidget)
	gifLayout.AddWidget(gifCountSpin.QAbstractSpinBox.QWidget)

	gifBox.SetVisible(false) // hidden until a GIF is selected
	leftLayout.AddWidget(gifBox)

	// Update GIF controls when layer selection changes
	p.layerList.OnCurrentRowChanged(func(row int) {
		if p.canvas != nil {
			p.canvas.SetSelectedItem(row)
		}
		// Show/hide GIF controls
		if row >= 0 && p.canvas.IsGifItem(row) {
			gifBox.SetVisible(true)
			mode, count := p.canvas.GetGifLoopMode(row)
			switch mode {
			case widgets.GifLoopDisabled:
				gifCombo.SetCurrentIndex(0)
				gifCountSpin.SetVisible(false)
			case widgets.GifLoopOnce:
				gifCombo.SetCurrentIndex(1)
				gifCountSpin.SetVisible(false)
			case widgets.GifLoopContinuous:
				gifCombo.SetCurrentIndex(2)
				gifCountSpin.SetVisible(false)
			case widgets.GifLoopCount:
				gifCombo.SetCurrentIndex(3)
				gifCountSpin.SetValue(count)
				gifCountSpin.SetVisible(true)
			case widgets.GifLoopOnceThenHide:
				gifCombo.SetCurrentIndex(4)
				gifCountSpin.SetVisible(false)
			case widgets.GifLoopCountThenHide:
				gifCombo.SetCurrentIndex(5)
				gifCountSpin.SetValue(count)
				gifCountSpin.SetVisible(true)
			}
		} else {
			gifBox.SetVisible(false)
		}
	})

	// Up/Down buttons for z-order
	layerBtnRow := qt.NewQHBoxLayout2()
	layerBtnRow.SetSpacing(4)

	upBtn := qt.NewQPushButton3("Up")
	upBtn.SetFixedHeight(22)
	upBtn.SetStyleSheet("QPushButton { background: #45475a; color: #cdd6f4; border: none; border-radius: 3px; font-size: 10px; padding: 2px 8px; } QPushButton:hover { background: #585b70; }")
	upBtn.SetToolTip("Move selected layer up (drawn earlier, behind others).")
	upBtn.OnClicked(func() {
		row := int(p.layerList.CurrentRow())
		if row > 0 {
			item := p.layerList.TakeItem(row)
			p.layerList.InsertItem(row-1, item)
			p.layerList.SetCurrentRow(row - 1)
			p.canvas.SwapItems(row, row-1)
			p.saveCanvasState()
		}
	})
	layerBtnRow.AddWidget(upBtn.QAbstractButton.QWidget)

	downBtn := qt.NewQPushButton3("Down")
	downBtn.SetFixedHeight(22)
	downBtn.SetStyleSheet("QPushButton { background: #45475a; color: #cdd6f4; border: none; border-radius: 3px; font-size: 10px; padding: 2px 8px; } QPushButton:hover { background: #585b70; }")
	downBtn.SetToolTip("Move selected layer down (drawn later, in front of others).")
	downBtn.OnClicked(func() {
		row := int(p.layerList.CurrentRow())
		if row >= 0 && row < int(p.layerList.Count())-1 {
			item := p.layerList.TakeItem(row)
			p.layerList.InsertItem(row+1, item)
			p.layerList.SetCurrentRow(row + 1)
			p.canvas.SwapItems(row, row+1)
			p.saveCanvasState()
		}
	})
	layerBtnRow.AddWidget(downBtn.QAbstractButton.QWidget)

	delBtn := qt.NewQPushButton3("Delete")
	delBtn.SetFixedHeight(22)
	delBtn.SetStyleSheet("QPushButton { background: #f38ba8; color: #1e1e2e; border: none; border-radius: 3px; font-size: 10px; padding: 2px 8px; font-weight: bold; } QPushButton:hover { background: #eba0ac; }")
	delBtn.SetToolTip("Remove selected element from canvas.")
	delBtn.OnClicked(func() {
		row := int(p.layerList.CurrentRow())
		if row >= 0 {
			p.canvas.RemoveItem(row)
			p.layerList.TakeItem(row)
		}
	})
	layerBtnRow.AddWidget(delBtn.QAbstractButton.QWidget)

	leftLayout.AddLayout(layerBtnRow.QLayout)

	leftLayout.AddStretch()
	layout.AddWidget(leftCol)

	// === Center: Canvas + Add + Controls ===
	centerCol := qt.NewQWidget2()
	centerLayout := qt.NewQVBoxLayout(centerCol)
	centerLayout.SetSpacing(8)
	centerLayout.SetContentsMargins(0, 0, 0, 0)

	// Canvas — starts empty
	p.canvas = widgets.NewRecordingCanvas()
	p.canvas.Widget().SetToolTip("WYSIWYG preview.\nAdd elements with + button.\nDrag to move, scroll to resize,\narrow keys to nudge.")
	centerLayout.AddWidget2(p.canvas.Widget(), 1)

	// Set title color from settings
	titleColor := cfg.LastUsedLogos.TitleColor
	if titleColor == "" {
		titleColor = config.DefaultTitleColor
	}
	p.canvas.SetTitleColor(titleColor)

	// Auto-save canvas state on any change
	p.canvas.OnChange(func() {
		p.saveCanvasState()
	})

	// Restore canvas state from config
	p.restoreCanvasState()

	// Sync canvas selection → layer list
	p.canvas.OnSelectionChanged(func(index int) {
		if p.layerList != nil && index >= 0 && index < int(p.layerList.Count()) {
			p.layerList.SetCurrentRow(index)
		} else if p.layerList != nil {
			p.layerList.SetCurrentRow(-1)
		}
	})

	// Add element button + controls row
	addRow := qt.NewQHBoxLayout2()
	addRow.SetSpacing(8)

	addBtn := qt.NewQPushButton3("+ Add Element")
	addBtn.SetStyleSheet(`
		QPushButton {
			background: #89b4fa;
			color: #1e1e2e;
			border: none;
			border-radius: 6px;
			padding: 8px 16px;
			font-size: 13px;
			font-weight: bold;
		}
		QPushButton:hover { background: #74c7ec; }
	`)
	addBtn.SetToolTip("Add a screen, webcam, logo,\nor title text to the canvas.")

	// Build the add menu
	p.addMenu = qt.NewQMenu2()
	p.addMenu.SetStyleSheet(`
		QMenu {
			background: #1e1e2e;
			color: #cdd6f4;
			border: 1px solid #45475a;
			padding: 4px;
		}
		QMenu::item { padding: 6px 20px; }
		QMenu::item:selected { background: #45475a; }
		QMenu::separator { height: 1px; background: #313244; margin: 4px 8px; }
	`)

	// Screen submenu
	screenMenu := p.addMenu.AddMenuWithTitle("Screen")
	for _, m := range p.monitors {
		mon := m // capture
		action := screenMenu.AddActionWithText(fmt.Sprintf("%s (%dx%d)", mon.Description, mon.Width, mon.Height))
		if mon.Description == "" {
			action.SetText(fmt.Sprintf("%s (%dx%d)", mon.Name, mon.Width, mon.Height))
		}
		action.OnTriggered(func() {
			p.addScreen(&mon)
			p.refreshLayerList()
		})
	}

	// Webcam submenu with shape options
	webcamMenu := p.addMenu.AddMenuWithTitle("Webcam")
	for _, dev := range p.webcamDevs {
		d := dev // capture
		devMenu := webcamMenu.AddMenuWithTitle(d.Name)

		roundAction := devMenu.AddActionWithText("Round bubble")
		roundAction.OnTriggered(func() {
			p.canvas.AddWebcamWithShape(d.Device, d.Name, widgets.ShapeRound)
			p.refreshLayerList()
		})

		squareAction := devMenu.AddActionWithText("Square")
		squareAction.OnTriggered(func() {
			p.canvas.AddWebcamWithShape(d.Device, d.Name, widgets.ShapeSquare)
			p.refreshLayerList()
		})

		rectAction := devMenu.AddActionWithText("Rectangle")
		rectAction.OnTriggered(func() {
			p.canvas.AddWebcamWithShape(d.Device, d.Name, widgets.ShapeRect)
			p.refreshLayerList()
		})
	}

	// Logo — unlimited, no position names
	addLogoAction := p.addMenu.AddActionWithText("Logo")
	addLogoAction.OnTriggered(func() {
		p.addLogo(widgets.ItemLogo)
		p.refreshLayerList()
	})

	// Title text
	titleAction := p.addMenu.AddActionWithText("Title Text")
	titleAction.OnTriggered(func() {
		title := p.titleInput.Text()
		if title == "" {
			title = "Title"
		}
		p.canvas.SetTitle(title)
		p.refreshLayerList()
	})

	addBtn.OnClicked(func() {
		// Show menu below button
		pos := addBtn.MapToGlobalWithQPoint(qt.NewQPoint2(0, addBtn.Height()))
		p.addMenu.Popup(pos)
	})
	addRow.AddWidget(addBtn.QAbstractButton.QWidget)

	resetBtn := qt.NewQPushButton3("Reset Preview")
	resetBtn.SetStyleSheet(`
		QPushButton {
			background: #45475a;
			color: #cdd6f4;
			border: none;
			border-radius: 6px;
			padding: 8px 12px;
			font-size: 12px;
		}
		QPushButton:hover { background: #585b70; }
	`)
	resetBtn.SetToolTip("Reset all animations to start.\nShows hidden items and restarts GIFs.")
	resetBtn.OnClicked(func() {
		p.canvas.ResetPreview()
		p.refreshLayerList()
	})
	addRow.AddWidget(resetBtn.QAbstractButton.QWidget)

	addRow.AddStretch()

	// Status
	p.statusLabel = qt.NewQLabel3("Ready")
	p.statusLabel.SetStyleSheet("QLabel { color: #a6e3a1; font-size: 13px; font-weight: bold; }")
	addRow.AddWidget(p.statusLabel.QFrame.QWidget)

	p.elapsedLabel = qt.NewQLabel3("00:00:00")
	p.elapsedLabel.SetStyleSheet("QLabel { color: #cdd6f4; font-size: 16px; font-weight: bold; font-family: monospace; }")
	addRow.AddWidget(p.elapsedLabel.QFrame.QWidget)

	centerLayout.AddLayout(addRow.QLayout)

	// Record buttons
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
}

func (p *RecordPage) addScreen(mon *models.Monitor) {
	p.hasScreen = true
	p.screenMonitor = mon
	p.canvas.SetMonitor(mon)
	// The screen is a background element, but we add it as a named item
	// so it appears in the layer list
	desc := mon.Description
	if desc == "" {
		desc = mon.Name
	}
	p.canvas.AddScreenItem(desc)
}

func (p *RecordPage) addLogo(itemType widgets.CanvasItemType) {
	file := qt.QFileDialog_GetOpenFileName3(p.widget, "Select Logo", "")
	if file != "" {
		p.canvas.AddLogo(itemType, file)
	}
}

func (p *RecordPage) addFormRow(layout *qt.QVBoxLayout, label, placeholder, labelStyle, inputStyle string) *qt.QLineEdit {
	row := qt.NewQHBoxLayout2()
	lbl := qt.NewQLabel3(label)
	lbl.SetStyleSheet(labelStyle)
	lbl.SetFixedWidth(60)
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
	noScreen := true
	if p.screenMonitor != nil {
		monitorName = p.screenMonitor.Name
		monitorRes = fmt.Sprintf("%dx%d", p.screenMonitor.Width, p.screenMonitor.Height)
		noScreen = false
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
	recordingInfo.Settings.ScreenEnabled = !noScreen
	recordingInfo.Settings.AudioEnabled = p.audioCheck.IsChecked()
	recordingInfo.Settings.WebcamEnabled = p.canvas.HasWebcams()
	recordingInfo.Settings.VerticalEnabled = p.canvas.IsVertical()
	recordingInfo.Settings.LeftSplitEnabled = p.leftSplit

	// Collect logo paths from canvas
	logoSelection := config.LogoSelection{}
	logos := p.canvas.GetLogoPaths()
	if len(logos) > 0 {
		logoSelection.LeftLogo = logos[widgets.ItemLogoLeft]
		logoSelection.RightLogo = logos[widgets.ItemLogoRight]
		logoSelection.BottomLogo = logos[widgets.ItemLogoBanner]
		recordingInfo.Settings.LogosEnabled = true
		recordingInfo.Settings.LeftLogo = logoSelection.LeftLogo
		recordingInfo.Settings.RightLogo = logoSelection.RightLogo
		recordingInfo.Settings.BottomLogo = logoSelection.BottomLogo
	}

	opts := recorder.Options{
		Monitor:        monitorName,
		NoAudio:        !p.audioCheck.IsChecked(),
		NoWebcam:       !p.canvas.HasWebcams(),
		NoScreen:       noScreen,
		OutputDir:      recordingDir,
		RecordingInfo:  recordingInfo,
		CreateVertical: p.canvas.IsVertical(),
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
		// Stop recording WITHOUT processing (process=false)
		err := p.rec.Stop()
		runOnUI(func() {
			if err != nil {
				p.statusLabel.SetText(fmt.Sprintf("Error: %v", err))
				p.statusLabel.SetStyleSheet("QLabel { color: #f38ba8; font-size: 13px; font-weight: bold; }")
				p.resetToIdle()
				return
			}

			p.resetToIdle()
			if p.onStatusChange != nil {
				p.onStatusChange("Processing")
			}

			// Navigate to processing page and start processing
			if p.onNavigate != nil {
				p.onNavigate(3) // PageProcessing
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

// saveCanvasState persists the current canvas state to config
func (p *RecordPage) saveCanvasState() {
	cfg, _ := config.Load()
	if cfg == nil {
		return
	}

	exported := p.canvas.ExportState()
	var items []config.CanvasItemState
	for _, e := range exported {
		items = append(items, config.CanvasItemState{
			Type:       e.Type,
			Label:      e.Label,
			X:          e.X,
			Y:          e.Y,
			W:          e.W,
			H:          e.H,
			Device:     e.Device,
			FilePath:   e.FilePath,
			Shape:      e.Shape,
			GifLoop:    e.GifLoop,
			GifLoopMax: e.GifLoopMax,
		})
	}

	cfg.CanvasState = &config.CanvasState{
		Mode:         p.canvas.GetMode(),
		Items:        items,
		TitleColor:   cfg.LastUsedLogos.TitleColor,
		AudioEnabled: p.audioCheck.IsChecked(),
		Presenter:    p.presenterInput.Text(),
	}
	_ = config.Save(cfg)
}

// restoreCanvasState loads canvas state from config and validates resources
func (p *RecordPage) restoreCanvasState() {
	cfg, _ := config.Load()
	if cfg == nil || cfg.CanvasState == nil || len(cfg.CanvasState.Items) == 0 {
		return
	}

	state := cfg.CanvasState

	// Restore mode
	p.canvas.SetMode(state.Mode)

	// Build sets of available devices for validation
	availableDevices := make(map[string]bool)
	for _, dev := range p.webcamDevs {
		availableDevices[dev.Device] = true
	}
	availableMonitors := make(map[string]bool)
	for _, mon := range p.monitors {
		availableMonitors[mon.Name] = true
	}

	for _, item := range state.Items {
		switch item.Type {
		case "screen":
			// Validate monitor still exists
			monName := ""
			for _, mon := range p.monitors {
				desc := mon.Description
				if desc == "" {
					desc = mon.Name
				}
				if "Screen: "+desc == item.Label || mon.Name == item.Monitor {
					monName = mon.Name
					p.addScreen(&mon)
					break
				}
			}
			if monName == "" {
				continue // monitor disconnected, skip
			}

		case "webcam":
			if !availableDevices[item.Device] {
				continue // webcam disconnected, skip
			}
			p.canvas.ImportItem(widgets.CanvasItemExport{
				Type:   "webcam",
				Label:  item.Label,
				X:      item.X,
				Y:      item.Y,
				W:      item.W,
				H:      item.H,
				Device: item.Device,
				Shape:  item.Shape,
			})

		case "logo":
			if item.FilePath == "" {
				continue
			}
			// Validate file still exists
			if _, err := os.Stat(item.FilePath); err != nil {
				continue // file deleted, skip
			}
			p.canvas.ImportItem(widgets.CanvasItemExport{
				Type:       "logo",
				Label:      item.Label,
				X:          item.X,
				Y:          item.Y,
				W:          item.W,
				H:          item.H,
				FilePath:   item.FilePath,
				GifLoop:    item.GifLoop,
				GifLoopMax: item.GifLoopMax,
			})

		case "title":
			p.canvas.ImportItem(widgets.CanvasItemExport{
				Type:  "title",
				Label: item.Label,
				X:     item.X,
				Y:     item.Y,
				W:     item.W,
				H:     item.H,
			})
		}
	}

	// Restore audio and presenter
	if state.AudioEnabled {
		p.audioCheck.SetChecked(true)
	}
	if state.Presenter != "" {
		p.presenterInput.SetText(state.Presenter)
	}

	// Refresh the layer list
	p.refreshLayerList()
}

// refreshLayerList rebuilds the layer list from canvas items
func (p *RecordPage) refreshLayerList() {
	if p.layerList == nil || p.canvas == nil {
		return
	}
	p.layerList.Clear()
	names := p.canvas.ItemNames()
	for _, name := range names {
		p.layerList.AddItem(name)
	}
}

// syncLayerOrderToCanvas reads the current list widget order and reorders canvas items
func (p *RecordPage) syncLayerOrderToCanvas() {
	if p.layerList == nil || p.canvas == nil {
		return
	}
	var order []int
	for i := 0; i < int(p.layerList.Count()); i++ {
		text := p.layerList.Item(i).Text()
		idx := p.canvas.FindItemByName(text)
		if idx >= 0 {
			order = append(order, idx)
		}
	}
	p.canvas.ReorderItems(order)
}

// Widget returns the underlying QWidget
func (p *RecordPage) Widget() *qt.QWidget { return p.widget }

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
func (p *RecordPage) TriggerStop() { p.onStopClicked() }

// StopPreviews stops screen refresh
func (p *RecordPage) StopPreviews() {
	if p.canvas != nil {
		p.canvas.Stop()
	}
}
