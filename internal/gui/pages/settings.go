package pages

import (
	"fmt"
	"sort"
	"strings"

	qt "github.com/mappu/miqt/qt6"
	"github.com/kartoza/kartoza-screencaster/internal/config"
	"github.com/kartoza/kartoza-screencaster/internal/models"
)

// SettingsPage is the configuration page
type SettingsPage struct {
	widget *qt.QWidget
	cfg    *config.Config

	// Form fields
	outputDirInput *qt.QLineEdit
	presenterInput *qt.QLineEdit
	normalizeCheck *qt.QCheckBox
	logoDirInput   *qt.QLineEdit
	titleColorBtn  *qt.QPushButton
	titleColorHex  *qt.QLineEdit
	bgColorBtn     *qt.QPushButton
	bgColorHex     *qt.QLineEdit
	gifLoopCombo   *qt.QComboBox
	topicsList     *qt.QListWidget
	topicInput     *qt.QLineEdit
	ytStatusLabel  *qt.QLabel

	// Current color values
	titleColor string
	bgColor    string
}

// NewSettingsPage creates a new settings page
func NewSettingsPage() *SettingsPage {
	cfg, _ := config.Load()
	p := &SettingsPage{
		widget: qt.NewQWidget2(),
		cfg:    cfg,
	}
	p.setupUI()
	return p
}

func (p *SettingsPage) setupUI() {
	// Scrollable layout
	scrollArea := qt.NewQScrollArea2()
	scrollArea.SetWidgetResizable(true)
	scrollArea.SetStyleSheet("QScrollArea { border: none; background: #1e1e2e; }")

	scrollContent := qt.NewQWidget2()
	layout := qt.NewQVBoxLayout(scrollContent)
	layout.SetContentsMargins(20, 20, 20, 20)
	layout.SetSpacing(10)

	sectionStyle := "QLabel { color: #89b4fa; font-size: 15px; font-weight: bold; padding-top: 10px; }"
	labelStyle := "QLabel { color: #cdd6f4; font-size: 13px; }"
	inputStyle := "QLineEdit { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 6px; font-size: 13px; }"
	comboStyle := "QComboBox { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 6px; font-size: 13px; } QComboBox QAbstractItemView { background: #313244; color: #cdd6f4; selection-background-color: #45475a; }"
	checkStyle := "QCheckBox { color: #cdd6f4; font-size: 13px; } QCheckBox::indicator { width: 16px; height: 16px; }"
	btnStyle := "QPushButton { background: #45475a; color: #cdd6f4; border: none; border-radius: 4px; padding: 6px 12px; font-size: 13px; } QPushButton:hover { background: #585b70; }"

	// Title
	title := qt.NewQLabel3("Settings")
	title.SetStyleSheet("QLabel { color: #cdd6f4; font-size: 18px; font-weight: bold; }")
	layout.AddWidget(title.QFrame.QWidget)

	// === Recording Defaults ===
	recSection := qt.NewQLabel3("Recording Defaults")
	recSection.SetStyleSheet(sectionStyle)
	layout.AddWidget(recSection.QFrame.QWidget)

	// Output directory
	outRow := qt.NewQHBoxLayout2()
	outLabel := qt.NewQLabel3("Output directory:")
	outLabel.SetStyleSheet(labelStyle)
	outLabel.SetFixedWidth(130)
	outRow.AddWidget(outLabel.QFrame.QWidget)
	p.outputDirInput = qt.NewQLineEdit2()
	p.outputDirInput.SetStyleSheet(inputStyle)
	p.outputDirInput.SetText(p.cfg.OutputDir)
	p.outputDirInput.OnEditingFinished(func() { p.saveConfig() })
	outRow.AddWidget(p.outputDirInput.QWidget)
	browseBtn := qt.NewQPushButton3("Browse")
	browseBtn.SetStyleSheet(btnStyle)
	browseBtn.OnClicked(func() {
		dir := qt.QFileDialog_GetExistingDirectory3(p.widget, "Select Output Directory", p.outputDirInput.Text())
		if dir != "" {
			p.outputDirInput.SetText(dir)
			p.saveConfig()
		}
	})
	outRow.AddWidget(browseBtn.QAbstractButton.QWidget)
	layout.AddLayout(outRow.QLayout)

	// Default presenter
	presRow := qt.NewQHBoxLayout2()
	presLabel := qt.NewQLabel3("Default presenter:")
	presLabel.SetStyleSheet(labelStyle)
	presLabel.SetFixedWidth(130)
	presRow.AddWidget(presLabel.QFrame.QWidget)
	p.presenterInput = qt.NewQLineEdit2()
	p.presenterInput.SetStyleSheet(inputStyle)
	p.presenterInput.SetText(p.cfg.DefaultPresenter)
	p.presenterInput.SetPlaceholderText("Presenter name...")
	p.presenterInput.OnEditingFinished(func() { p.saveConfig() })
	presRow.AddWidget(p.presenterInput.QWidget)
	layout.AddLayout(presRow.QLayout)

	// === Audio Processing ===
	audioSection := qt.NewQLabel3("Audio Processing")
	audioSection.SetStyleSheet(sectionStyle)
	layout.AddWidget(audioSection.QFrame.QWidget)

	p.normalizeCheck = qt.NewQCheckBox3("Normalize and process audio (denoise, compress, normalize)")
	p.normalizeCheck.SetChecked(p.cfg.AudioProcessing.NormalizeEnabled)
	p.normalizeCheck.SetStyleSheet(checkStyle)
	p.normalizeCheck.OnStateChanged(func(state int) { p.saveConfig() })
	layout.AddWidget(p.normalizeCheck.QAbstractButton.QWidget)

	// Room noise file
	noiseRow := qt.NewQHBoxLayout2()
	noiseLabel := qt.NewQLabel3("Room noise file:")
	noiseLabel.SetStyleSheet(labelStyle)
	noiseLabel.SetFixedWidth(130)
	noiseRow.AddWidget(noiseLabel.QFrame.QWidget)
	noiseInput := qt.NewQLineEdit2()
	noiseInput.SetStyleSheet(inputStyle)
	noiseInput.SetText(p.cfg.AudioProcessing.RoomNoiseFile)
	noiseInput.SetPlaceholderText("Optional: path to room noise WAV")
	noiseInput.SetReadOnly(true)
	noiseRow.AddWidget(noiseInput.QWidget)
	noiseBrowseBtn := qt.NewQPushButton3("Browse")
	noiseBrowseBtn.SetStyleSheet(btnStyle)
	noiseBrowseBtn.OnClicked(func() {
		file := qt.QFileDialog_GetOpenFileName3(p.widget, "Select Room Noise File", "")
		if file != "" {
			noiseInput.SetText(file)
			p.cfg.AudioProcessing.RoomNoiseFile = file
			p.saveConfig()
		}
	})
	noiseRow.AddWidget(noiseBrowseBtn.QAbstractButton.QWidget)
	layout.AddLayout(noiseRow.QLayout)

	// === Logo Library ===
	logoSection := qt.NewQLabel3("Logo Library")
	logoSection.SetStyleSheet(sectionStyle)
	layout.AddWidget(logoSection.QFrame.QWidget)

	logoDirRow := qt.NewQHBoxLayout2()
	logoDirLabel := qt.NewQLabel3("Logo directory:")
	logoDirLabel.SetStyleSheet(labelStyle)
	logoDirLabel.SetFixedWidth(130)
	logoDirRow.AddWidget(logoDirLabel.QFrame.QWidget)
	p.logoDirInput = qt.NewQLineEdit2()
	p.logoDirInput.SetStyleSheet(inputStyle)
	p.logoDirInput.SetText(p.cfg.LogoDirectory)
	p.logoDirInput.SetPlaceholderText("~/Pictures/Logos")
	p.logoDirInput.OnEditingFinished(func() { p.saveConfig() })
	logoDirRow.AddWidget(p.logoDirInput.QWidget)
	logoBrowseBtn := qt.NewQPushButton3("Browse")
	logoBrowseBtn.SetStyleSheet(btnStyle)
	logoBrowseBtn.OnClicked(func() {
		dir := qt.QFileDialog_GetExistingDirectory3(p.widget, "Select Logo Directory", p.logoDirInput.Text())
		if dir != "" {
			p.logoDirInput.SetText(dir)
			p.saveConfig()
		}
	})
	logoDirRow.AddWidget(logoBrowseBtn.QAbstractButton.QWidget)
	layout.AddLayout(logoDirRow.QLayout)

	// Title color — swatch + hex input
	p.titleColor = p.cfg.LastUsedLogos.TitleColor
	if p.titleColor == "" {
		p.titleColor = config.DefaultTitleColor
	}
	colorRow := qt.NewQHBoxLayout2()
	colorLabel := qt.NewQLabel3("Title color:")
	colorLabel.SetStyleSheet(labelStyle)
	colorLabel.SetFixedWidth(130)
	colorRow.AddWidget(colorLabel.QFrame.QWidget)
	p.titleColorBtn = qt.NewQPushButton2()
	p.titleColorBtn.SetFixedSize2(32, 32)
	p.updateSwatchStyle(p.titleColorBtn, p.titleColor)
	p.titleColorBtn.OnClicked(func() {
		if hex := openColorDialog(p.widget, "Select Title Color", p.titleColor); hex != "" {
			p.titleColor = hex
			p.updateSwatchStyle(p.titleColorBtn, p.titleColor)
			p.titleColorHex.SetText(p.titleColor)
			p.saveConfig()
		}
	})
	colorRow.AddWidget(p.titleColorBtn.QAbstractButton.QWidget)
	p.titleColorHex = qt.NewQLineEdit2()
	p.titleColorHex.SetStyleSheet(inputStyle)
	p.titleColorHex.SetFixedWidth(100)
	p.titleColorHex.SetText(p.titleColor)
	p.titleColorHex.SetPlaceholderText("#RRGGBB")
	p.titleColorHex.OnEditingFinished(func() {
		hex := p.titleColorHex.Text()
		if qt.QColor_IsValidColor(hex) {
			p.titleColor = hex
			p.updateSwatchStyle(p.titleColorBtn, p.titleColor)
			p.saveConfig()
		}
	})
	colorRow.AddWidget(p.titleColorHex.QWidget)
	colorRow.AddStretch()
	layout.AddLayout(colorRow.QLayout)

	// Background color — swatch + hex input
	p.bgColor = p.cfg.BgColor
	if p.bgColor == "" {
		p.bgColor = config.DefaultBgColor
	}
	bgRow := qt.NewQHBoxLayout2()
	bgLabel := qt.NewQLabel3("Background color:")
	bgLabel.SetStyleSheet(labelStyle)
	bgLabel.SetFixedWidth(130)
	bgRow.AddWidget(bgLabel.QFrame.QWidget)
	p.bgColorBtn = qt.NewQPushButton2()
	p.bgColorBtn.SetFixedSize2(32, 32)
	p.updateSwatchStyle(p.bgColorBtn, p.bgColor)
	p.bgColorBtn.OnClicked(func() {
		if hex := openColorDialog(p.widget, "Select Background Color", p.bgColor); hex != "" {
			p.bgColor = hex
			p.updateSwatchStyle(p.bgColorBtn, p.bgColor)
			p.bgColorHex.SetText(p.bgColor)
			p.saveConfig()
		}
	})
	bgRow.AddWidget(p.bgColorBtn.QAbstractButton.QWidget)
	p.bgColorHex = qt.NewQLineEdit2()
	p.bgColorHex.SetStyleSheet(inputStyle)
	p.bgColorHex.SetFixedWidth(100)
	p.bgColorHex.SetText(p.bgColor)
	p.bgColorHex.SetPlaceholderText("#RRGGBB")
	p.bgColorHex.OnEditingFinished(func() {
		hex := p.bgColorHex.Text()
		if qt.QColor_IsValidColor(hex) {
			p.bgColor = hex
			p.updateSwatchStyle(p.bgColorBtn, p.bgColor)
			p.saveConfig()
		}
	})
	bgRow.AddWidget(p.bgColorHex.QWidget)
	bgRow.AddStretch()
	layout.AddLayout(bgRow.QLayout)

	// GIF loop mode
	gifRow := qt.NewQHBoxLayout2()
	gifLabel := qt.NewQLabel3("GIF loop mode:")
	gifLabel.SetStyleSheet(labelStyle)
	gifLabel.SetFixedWidth(130)
	gifRow.AddWidget(gifLabel.QFrame.QWidget)
	p.gifLoopCombo = qt.NewQComboBox2()
	p.gifLoopCombo.SetStyleSheet(comboStyle)
	for _, mode := range config.GifLoopModes {
		p.gifLoopCombo.AddItem(config.GifLoopModeLabels[mode])
	}
	for i, mode := range config.GifLoopModes {
		if mode == p.cfg.LastUsedLogos.GifLoopMode {
			p.gifLoopCombo.SetCurrentIndex(i)
			break
		}
	}
	p.gifLoopCombo.OnCurrentIndexChanged(func(index int) { p.saveConfig() })
	gifRow.AddWidget(p.gifLoopCombo.QWidget)
	gifRow.AddStretch()
	layout.AddLayout(gifRow.QLayout)

	// === Topics ===
	topicSection := qt.NewQLabel3("Topics")
	topicSection.SetStyleSheet(sectionStyle)
	layout.AddWidget(topicSection.QFrame.QWidget)

	p.topicsList = qt.NewQListWidget2()
	p.topicsList.SetMaximumHeight(120)
	p.topicsList.SetStyleSheet(`
		QListWidget {
			background: #313244;
			color: #cdd6f4;
			border: 1px solid #45475a;
			border-radius: 4px;
			font-size: 13px;
		}
		QListWidget::item { padding: 4px 8px; }
		QListWidget::item:selected { background: #45475a; }
	`)
	// Sort topics alphabetically
	sort.Slice(p.cfg.Topics, func(i, j int) bool {
		return strings.ToLower(p.cfg.Topics[i].Name) < strings.ToLower(p.cfg.Topics[j].Name)
	})
	for _, topic := range p.cfg.Topics {
		p.topicsList.AddItem(topic.Name)
	}
	layout.AddWidget(p.topicsList.QListView.QAbstractItemView.QAbstractScrollArea.QFrame.QWidget)

	topicRow := qt.NewQHBoxLayout2()
	p.topicInput = qt.NewQLineEdit2()
	p.topicInput.SetPlaceholderText("New topic name...")
	p.topicInput.SetStyleSheet(inputStyle)
	topicRow.AddWidget(p.topicInput.QWidget)

	addTopicBtn := qt.NewQPushButton3("Add")
	addTopicBtn.SetStyleSheet("QPushButton { background: #a6e3a1; color: #1e1e2e; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold; } QPushButton:hover { background: #94e2d5; }")
	addTopicBtn.OnClicked(func() {
		name := strings.TrimSpace(p.topicInput.Text())
		if name == "" {
			return
		}
		p.topicInput.Clear()
		p.cfg.Topics = append(p.cfg.Topics, models.Topic{
			ID:   fmt.Sprintf("topic_%d", len(p.cfg.Topics)+1),
			Name: name,
		})
		// Re-sort and rebuild list
		sort.Slice(p.cfg.Topics, func(i, j int) bool {
			return strings.ToLower(p.cfg.Topics[i].Name) < strings.ToLower(p.cfg.Topics[j].Name)
		})
		p.topicsList.Clear()
		for _, t := range p.cfg.Topics {
			p.topicsList.AddItem(t.Name)
		}
		p.saveConfig()
	})
	topicRow.AddWidget(addTopicBtn.QAbstractButton.QWidget)

	removeTopicBtn := qt.NewQPushButton3("Remove")
	removeTopicBtn.SetStyleSheet("QPushButton { background: #f38ba8; color: #1e1e2e; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold; } QPushButton:hover { background: #eba0ac; }")
	removeTopicBtn.OnClicked(func() {
		row := int(p.topicsList.CurrentRow())
		if row < 0 || row >= len(p.cfg.Topics) {
			return
		}
		p.cfg.Topics = append(p.cfg.Topics[:row], p.cfg.Topics[row+1:]...)
		p.topicsList.TakeItem(row)
		p.saveConfig()
	})
	topicRow.AddWidget(removeTopicBtn.QAbstractButton.QWidget)
	layout.AddLayout(topicRow.QLayout)

	// === YouTube ===
	ytSection := qt.NewQLabel3("YouTube Integration")
	ytSection.SetStyleSheet(sectionStyle)
	layout.AddWidget(ytSection.QFrame.QWidget)

	ytConnected := p.cfg.YouTube.ClientID != ""
	ytStatusText := "Not connected"
	if ytConnected {
		ytStatusText = fmt.Sprintf("Connected (Channel: %s)", p.cfg.YouTube.ChannelName)
		if p.cfg.YouTube.ChannelName == "" {
			ytStatusText = "Connected"
		}
	}
	p.ytStatusLabel = qt.NewQLabel3("Status: " + ytStatusText)
	if ytConnected {
		p.ytStatusLabel.SetStyleSheet("QLabel { color: #a6e3a1; font-size: 13px; }")
	} else {
		p.ytStatusLabel.SetStyleSheet("QLabel { color: #6c7086; font-size: 13px; }")
	}
	layout.AddWidget(p.ytStatusLabel.QFrame.QWidget)

	ytBtnRow := qt.NewQHBoxLayout2()
	disconnectBtn := qt.NewQPushButton3("Disconnect")
	disconnectBtn.SetStyleSheet("QPushButton { background: #f38ba8; color: #1e1e2e; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold; } QPushButton:hover { background: #eba0ac; }")
	disconnectBtn.SetEnabled(ytConnected)

	setupBtn := qt.NewQPushButton3("Setup YouTube")
	setupBtn.SetStyleSheet("QPushButton { background: #a6e3a1; color: #1e1e2e; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold; } QPushButton:hover { background: #94e2d5; }")
	setupBtn.SetVisible(!ytConnected)
	setupBtn.OnClicked(func() {
		// Open YouTube setup dialog — prompt for Client ID and Secret
		clientID := p.promptInput("YouTube Setup", "Enter your Google OAuth Client ID:")
		if clientID == "" {
			return
		}
		clientSecret := p.promptInput("YouTube Setup", "Enter your Google OAuth Client Secret:")
		if clientSecret == "" {
			return
		}
		p.cfg.YouTube.ClientID = clientID
		p.cfg.YouTube.ClientSecret = clientSecret
		_ = config.Save(p.cfg)
		p.ytStatusLabel.SetText("Status: Credentials saved (authorize on next upload)")
		p.ytStatusLabel.SetStyleSheet("QLabel { color: #fab387; font-size: 13px; }")
		setupBtn.SetVisible(false)
		disconnectBtn.SetEnabled(true)
	})
	ytBtnRow.AddWidget(setupBtn.QAbstractButton.QWidget)

	disconnectBtn.OnClicked(func() {
		result := qt.QMessageBox_Question(p.widget, "Disconnect YouTube",
			"Are you sure you want to disconnect your YouTube account?\nYou will need to re-authorize to upload videos.")
		if result == qt.QMessageBox__Yes {
			p.cfg.YouTube.ClientID = ""
			p.cfg.YouTube.ClientSecret = ""
			p.cfg.YouTube.ChannelName = ""
			p.cfg.YouTube.ChannelID = ""
			p.cfg.YouTube.Accounts = nil
			_ = config.Save(p.cfg)
			p.ytStatusLabel.SetText("Status: Not connected")
			p.ytStatusLabel.SetStyleSheet("QLabel { color: #6c7086; font-size: 13px; }")
			disconnectBtn.SetEnabled(false)
			setupBtn.SetVisible(true)
		}
	})
	ytBtnRow.AddWidget(disconnectBtn.QAbstractButton.QWidget)

	ytBtnRow.AddStretch()
	layout.AddLayout(ytBtnRow.QLayout)

	layout.AddStretch()

	scrollArea.SetWidget(scrollContent)

	// Wrap in page widget
	pageLayout := qt.NewQVBoxLayout(p.widget)
	pageLayout.SetContentsMargins(0, 0, 0, 0)
	pageLayout.AddWidget(scrollArea.QAbstractScrollArea.QFrame.QWidget)
}

func (p *SettingsPage) saveConfig() {
	p.cfg.OutputDir = p.outputDirInput.Text()
	p.cfg.DefaultPresenter = p.presenterInput.Text()
	p.cfg.AudioProcessing.NormalizeEnabled = p.normalizeCheck.IsChecked()
	p.cfg.LogoDirectory = p.logoDirInput.Text()

	// Colors
	p.cfg.LastUsedLogos.TitleColor = p.titleColor
	p.cfg.BgColor = p.bgColor

	// GIF loop mode
	gifIdx := p.gifLoopCombo.CurrentIndex()
	if gifIdx >= 0 && gifIdx < len(config.GifLoopModes) {
		p.cfg.LastUsedLogos.GifLoopMode = config.GifLoopModes[gifIdx]
	}

	_ = config.Save(p.cfg)
}

// promptInput shows a styled text input dialog and returns the entered text (empty if cancelled)
func (p *SettingsPage) promptInput(title, label string) string {
	dlg := qt.NewQInputDialog(p.widget)
	dlg.SetWindowTitle(title)
	dlg.SetLabelText(label)
	dlg.SetInputMode(qt.QInputDialog__TextInput)
	dlg.SetStyleSheet(`
		QInputDialog, QDialog, QWidget {
			background-color: #1e1e2e;
			color: #cdd6f4;
		}
		QLabel { color: #cdd6f4; }
		QLineEdit {
			background: #313244;
			color: #cdd6f4;
			border: 1px solid #45475a;
			padding: 4px;
		}
		QPushButton {
			background: #45475a;
			color: #cdd6f4;
			border: 1px solid #585b70;
			border-radius: 3px;
			padding: 4px 12px;
		}
		QPushButton:hover { background: #585b70; }
	`)

	if dlg.QDialog.Exec() == 1 {
		return dlg.TextValue()
	}
	return ""
}

// updateSwatchStyle sets the button background to show the color swatch
func (p *SettingsPage) updateSwatchStyle(btn *qt.QPushButton, color string) {
	btn.SetStyleSheet(fmt.Sprintf(`
		QPushButton {
			background-color: %s;
			border: 2px solid #45475a;
			border-radius: 4px;
		}
		QPushButton:hover { border-color: #89b4fa; }
	`, color))
}

// Widget returns the underlying QWidget
func (p *SettingsPage) Widget() *qt.QWidget {
	return p.widget
}
