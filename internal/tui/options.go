package tui

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
	"github.com/kartoza/kartoza-screencaster/internal/config"
	"github.com/kartoza/kartoza-screencaster/internal/models"
)

// fileEntry represents a file or directory in the browser
type fileEntry struct {
	name  string
	path  string
	isDir bool
}

// OptionsField represents which field is focused in options
type OptionsField int

const (
	OptionsFieldOutputDirectory OptionsField = iota
	OptionsFieldTopicList
	OptionsFieldAddTopic
	OptionsFieldRemoveTopic
	OptionsFieldDefaultPresenter
	OptionsFieldLogoDirectory
	OptionsFieldBgColor
	OptionsFieldYouTubeSetup
	OptionsFieldSyndicationSetup
	OptionsFieldPresetRecordAudio
	OptionsFieldPresetRecordWebcam
	OptionsFieldPresetRecordScreen
	OptionsFieldPresetVerticalVideo
	OptionsFieldPresetLeftSplit
	OptionsFieldPresetWebcamBubble
	OptionsFieldPresetAddLogos
	OptionsFieldSave
)

// FileBrowserField represents which part of the file browser is focused
type FileBrowserField int

const (
	FileBrowserFieldList FileBrowserField = iota
	FileBrowserFieldPathInput
)

// BrowserTarget indicates what the file browser is selecting
type BrowserTarget int

const (
	BrowserTargetLogo BrowserTarget = iota
	BrowserTargetOutput
)

// OptionsModel handles the options screen
type OptionsModel struct {
	width  int
	height int

	// Configuration
	config *config.Config

	// Topics management
	topics        []models.Topic
	selectedTopic int

	// Focus state
	focusedField OptionsField

	// Inputs
	newTopicInput  textinput.Model
	presenterInput textinput.Model

	// Output directory path (media folder)
	outputDirectory string

	// Logo directory path
	logoDirectory string

	// Background color for vertical video lower third
	bgColorIdx int

	// Custom file browser (for selecting logo directory or output directory)
	showFileBrowser      bool
	selectingDirectory   bool // true when selecting directory, not file
	browserTarget        BrowserTarget
	browserCurrentDir    string
	browserEntries       []fileEntry
	browserSelected      int
	browserScrollTop     int
	browserPathInput     textinput.Model
	browserField         FileBrowserField

	// Recording presets (for systray quick-record)
	presetRecordAudio   bool
	presetRecordWebcam  bool
	presetRecordScreen  bool
	presetVerticalVideo bool
	presetLeftSplit     bool
	presetWebcamBubble  bool
	presetAddLogos      bool

	// State
	savedSuccess bool // set on successful save (for presets-mode auto-close)
	message      string
	err          error
}

// NewOptionsModel creates a new options model
func NewOptionsModel() *OptionsModel {
	cfg, _ := config.Load()

	// Get topics from config or use defaults
	topics := cfg.Topics
	if len(topics) == 0 {
		topics = models.DefaultTopics()
	}

	// New topic input
	newTopicInput := textinput.New()
	newTopicInput.Placeholder = "New topic name"
	newTopicInput.CharLimit = 50
	newTopicInput.Width = 30

	// Presenter input
	presenterInput := textinput.New()
	presenterInput.Placeholder = "Default presenter name"
	presenterInput.CharLimit = 100
	presenterInput.Width = 40
	if cfg.DefaultPresenter != "" {
		presenterInput.SetValue(cfg.DefaultPresenter)
	}

	// Path input for file browser
	pathInput := textinput.New()
	pathInput.Placeholder = "Enter or paste path..."
	pathInput.CharLimit = 500
	pathInput.Width = 50

	// Start in home directory or output directory if set
	home, _ := os.UserHomeDir()
	browserDir := home
	if cfg.OutputDir != "" {
		browserDir = cfg.OutputDir
	}

	// Get output directory - use config value or default
	outputDir := cfg.OutputDir
	if outputDir == "" {
		outputDir = config.GetDefaultVideosDir()
	}

	// Load recording presets
	presets := cfg.RecordingPresets
	if !cfg.PresetsConfigured {
		presets = config.DefaultRecordingPresets()
	}

	// Find background color index
	bgColorIdx := 0
	if cfg.BgColor != "" {
		for i, c := range config.BgColors {
			if c == cfg.BgColor {
				bgColorIdx = i
				break
			}
		}
	}

	return &OptionsModel{
		config:              cfg,
		topics:              topics,
		selectedTopic:       0,
		focusedField:        OptionsFieldOutputDirectory,
		newTopicInput:       newTopicInput,
		presenterInput:      presenterInput,
		outputDirectory:     outputDir,
		logoDirectory:       cfg.LogoDirectory,
		bgColorIdx:          bgColorIdx,
		showFileBrowser:     false,
		selectingDirectory:  false,
		browserCurrentDir:   browserDir,
		browserPathInput:    pathInput,
		browserField:        FileBrowserFieldList,
		presetRecordAudio:   presets.RecordAudio,
		presetRecordWebcam:  presets.RecordWebcam,
		presetRecordScreen:  presets.RecordScreen,
		presetVerticalVideo: presets.VerticalVideo,
		presetLeftSplit:     presets.LeftSplit,
		presetWebcamBubble:  presets.WebcamBubble,
		presetAddLogos:      presets.AddLogos,
	}
}

// loadBrowserEntries loads the directory contents for the file browser
func (m *OptionsModel) loadBrowserEntries() {
	m.browserEntries = nil
	m.browserSelected = 0
	m.browserScrollTop = 0

	entries, err := os.ReadDir(m.browserCurrentDir)
	if err != nil {
		return
	}

	// Add parent directory entry if not at root
	if m.browserCurrentDir != "/" {
		m.browserEntries = append(m.browserEntries, fileEntry{
			name:  "..",
			path:  filepath.Dir(m.browserCurrentDir),
			isDir: true,
		})
	}

	// Collect directories (and files if not selecting directory)
	var dirs []fileEntry
	for _, entry := range entries {
		// Skip hidden files
		if strings.HasPrefix(entry.Name(), ".") {
			continue
		}

		fullPath := filepath.Join(m.browserCurrentDir, entry.Name())
		fe := fileEntry{
			name:  entry.Name(),
			path:  fullPath,
			isDir: entry.IsDir(),
		}

		if entry.IsDir() {
			dirs = append(dirs, fe)
		}
		// When selecting directory, we only show directories
	}

	// Sort alphabetically
	sort.Slice(dirs, func(i, j int) bool {
		return strings.ToLower(dirs[i].name) < strings.ToLower(dirs[j].name)
	})

	m.browserEntries = append(m.browserEntries, dirs...)
}

// openDirectoryBrowser opens the file browser for selecting a directory
func (m *OptionsModel) openDirectoryBrowser(target BrowserTarget) {
	m.showFileBrowser = true
	m.selectingDirectory = true
	m.browserTarget = target
	m.browserField = FileBrowserFieldList

	// Start in the appropriate directory based on target
	switch target {
	case BrowserTargetOutput:
		if m.outputDirectory != "" {
			m.browserCurrentDir = m.outputDirectory
		}
	case BrowserTargetLogo:
		if m.logoDirectory != "" {
			m.browserCurrentDir = m.logoDirectory
		}
	}

	m.browserPathInput.SetValue(m.browserCurrentDir)
	m.browserPathInput.Blur()
	m.loadBrowserEntries()
}

// closeFileBrowser closes the file browser
func (m *OptionsModel) closeFileBrowser() {
	m.showFileBrowser = false
	m.selectingDirectory = false
}

// IsFileBrowserActive returns true if the file browser is currently shown
func (m *OptionsModel) IsFileBrowserActive() bool {
	return m.showFileBrowser
}

// RenderFileBrowser renders the file browser with full screen layout
func (m *OptionsModel) RenderFileBrowser(width, height int) string {
	m.width = width
	m.height = height
	return m.renderFileBrowser()
}

// Init initializes the model
func (m *OptionsModel) Init() tea.Cmd {
	return textinput.Blink
}

// Update handles messages
func (m *OptionsModel) Update(msg tea.Msg) (*OptionsModel, tea.Cmd) {
	var cmds []tea.Cmd

	// Handle file browser if active
	if m.showFileBrowser {
		return m.updateFileBrowser(msg)
	}

	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height

	case tea.KeyMsg:
		// Clear messages on any key
		m.message = ""
		m.err = nil

		switch msg.String() {
		case "tab", "down":
			m.nextField()
			return m, nil

		case "shift+tab", "up":
			m.prevField()
			return m, nil

		case "j":
			if m.focusedField == OptionsFieldTopicList {
				m.selectedTopic++
				if m.selectedTopic >= len(m.topics) {
					m.selectedTopic = 0
				}
				return m, nil
			}

		case "k":
			if m.focusedField == OptionsFieldTopicList {
				m.selectedTopic--
				if m.selectedTopic < 0 {
					m.selectedTopic = len(m.topics) - 1
				}
				return m, nil
			}

		case "left":
			if m.focusedField == OptionsFieldBgColor {
				m.bgColorIdx--
				if m.bgColorIdx < 0 {
					m.bgColorIdx = len(config.BgColors) - 1
				}
				return m, nil
			}

		case "right":
			if m.focusedField == OptionsFieldBgColor {
				m.bgColorIdx++
				if m.bgColorIdx >= len(config.BgColors) {
					m.bgColorIdx = 0
				}
				return m, nil
			}

		case "enter", " ":
			switch m.focusedField {
			case OptionsFieldOutputDirectory:
				m.openDirectoryBrowser(BrowserTargetOutput)
				return m, nil
			case OptionsFieldAddTopic:
				m.addTopic()
				return m, nil
			case OptionsFieldRemoveTopic:
				m.removeTopic()
				return m, nil
			case OptionsFieldLogoDirectory:
				m.openDirectoryBrowser(BrowserTargetLogo)
				return m, nil
			case OptionsFieldBgColor:
				// Cycle to next color on enter/space
				m.bgColorIdx++
				if m.bgColorIdx >= len(config.BgColors) {
					m.bgColorIdx = 0
				}
				return m, nil
			case OptionsFieldYouTubeSetup:
				return m, func() tea.Msg { return goToYouTubeSetupMsg{} }
			case OptionsFieldSyndicationSetup:
				return m, func() tea.Msg { return goToSyndicationSetupMsg{} }
			case OptionsFieldPresetRecordAudio:
				m.presetRecordAudio = !m.presetRecordAudio
				return m, nil
			case OptionsFieldPresetRecordWebcam:
				m.presetRecordWebcam = !m.presetRecordWebcam
				// Auto-disable vertical video if both webcam and screen are off
				if !m.presetRecordWebcam && !m.presetRecordScreen {
					m.presetVerticalVideo = false
				}
				return m, nil
			case OptionsFieldPresetRecordScreen:
				m.presetRecordScreen = !m.presetRecordScreen
				// Auto-disable vertical video if both webcam and screen are off
				if !m.presetRecordWebcam && !m.presetRecordScreen {
					m.presetVerticalVideo = false
				}
				return m, nil
			case OptionsFieldPresetVerticalVideo:
				// Only allow if webcam or screen is enabled
				if m.presetRecordWebcam || m.presetRecordScreen {
					m.presetVerticalVideo = !m.presetVerticalVideo
				}
				return m, nil
			case OptionsFieldPresetLeftSplit:
				// Only allow if vertical video is enabled
				if m.presetVerticalVideo {
					m.presetLeftSplit = !m.presetLeftSplit
				}
				return m, nil
			case OptionsFieldPresetWebcamBubble:
				// Only allow if vertical video and left split are enabled
				if m.presetVerticalVideo && m.presetLeftSplit {
					m.presetWebcamBubble = !m.presetWebcamBubble
				}
				return m, nil
			case OptionsFieldPresetAddLogos:
				m.presetAddLogos = !m.presetAddLogos
				return m, nil
			case OptionsFieldSave:
				m.save()
				return m, nil
			default:
				m.nextField()
				return m, nil
			}

		case "c":
			// Clear directory if on appropriate field and save immediately
			if m.focusedField == OptionsFieldOutputDirectory {
				m.outputDirectory = config.GetDefaultVideosDir()
				m.config.OutputDir = m.outputDirectory
				if err := config.Save(m.config); err != nil {
					m.message = "Error saving: " + err.Error()
				} else {
					m.message = "Output directory reset to default and saved"
				}
				return m, nil
			}
			if m.focusedField == OptionsFieldLogoDirectory {
				m.logoDirectory = ""
				m.config.LogoDirectory = ""
				if err := config.Save(m.config); err != nil {
					m.message = "Error saving: " + err.Error()
				} else {
					m.message = "Logo directory cleared and saved"
				}
				return m, nil
			}

		case "d", "delete", "backspace":
			if m.focusedField == OptionsFieldTopicList && len(m.topics) > 1 {
				m.removeTopic()
				return m, nil
			}
		}
	}

	// Update focused input
	switch m.focusedField {
	case OptionsFieldAddTopic:
		var cmd tea.Cmd
		m.newTopicInput, cmd = m.newTopicInput.Update(msg)
		cmds = append(cmds, cmd)

	case OptionsFieldDefaultPresenter:
		var cmd tea.Cmd
		m.presenterInput, cmd = m.presenterInput.Update(msg)
		cmds = append(cmds, cmd)
	}

	return m, tea.Batch(cmds...)
}

// nextField moves to the next field
func (m *OptionsModel) nextField() {
	m.unfocusAll()
	m.focusedField++
	if m.focusedField > OptionsFieldSave {
		m.focusedField = OptionsFieldTopicList
	}
	m.focusCurrent()
}

// prevField moves to the previous field
func (m *OptionsModel) prevField() {
	m.unfocusAll()
	m.focusedField--
	if m.focusedField < OptionsFieldTopicList {
		m.focusedField = OptionsFieldSave
	}
	m.focusCurrent()
}

// unfocusAll removes focus from all inputs
func (m *OptionsModel) unfocusAll() {
	m.newTopicInput.Blur()
	m.presenterInput.Blur()
}

// focusCurrent focuses the current field
func (m *OptionsModel) focusCurrent() {
	switch m.focusedField {
	case OptionsFieldAddTopic:
		m.newTopicInput.Focus()
	case OptionsFieldDefaultPresenter:
		m.presenterInput.Focus()
	}
}

// addTopic adds a new topic
func (m *OptionsModel) addTopic() {
	name := strings.TrimSpace(m.newTopicInput.Value())
	if name == "" {
		m.err = nil
		return
	}

	// Check for duplicates
	for _, t := range m.topics {
		if strings.EqualFold(t.Name, name) {
			m.message = "Topic already exists"
			return
		}
	}

	// Generate ID from name
	id := strings.ToLower(strings.ReplaceAll(name, " ", "-"))

	m.topics = append(m.topics, models.Topic{
		ID:   id,
		Name: name,
	})

	m.newTopicInput.SetValue("")
	m.message = "Topic added: " + name
}

// removeTopic removes the selected topic
func (m *OptionsModel) removeTopic() {
	if len(m.topics) <= 1 {
		m.message = "Cannot remove last topic"
		return
	}

	if m.selectedTopic >= 0 && m.selectedTopic < len(m.topics) {
		name := m.topics[m.selectedTopic].Name
		m.topics = append(m.topics[:m.selectedTopic], m.topics[m.selectedTopic+1:]...)
		if m.selectedTopic >= len(m.topics) {
			m.selectedTopic = len(m.topics) - 1
		}
		m.message = "Topic removed: " + name
	}
}

// save saves the configuration
func (m *OptionsModel) save() {
	m.config.Topics = m.topics
	m.config.DefaultPresenter = strings.TrimSpace(m.presenterInput.Value())
	m.config.OutputDir = m.outputDirectory
	m.config.LogoDirectory = m.logoDirectory
	m.config.BgColor = config.BgColors[m.bgColorIdx]

	// Save recording presets
	m.config.RecordingPresets = config.RecordingPresets{
		RecordAudio:   m.presetRecordAudio,
		RecordWebcam:  m.presetRecordWebcam,
		RecordScreen:  m.presetRecordScreen,
		VerticalVideo: m.presetVerticalVideo,
		LeftSplit:     m.presetLeftSplit,
		WebcamBubble:  m.presetWebcamBubble,
		AddLogos:      m.presetAddLogos,
	}
	m.config.PresetsConfigured = true

	if err := config.Save(m.config); err != nil {
		m.err = err
		return
	}

	m.savedSuccess = true
	m.message = "Settings saved successfully"
}

// View renders the options screen
func (m *OptionsModel) View() string {
	// If file browser is shown, render it instead
	if m.showFileBrowser {
		return m.renderFileBrowser()
	}

	// Styles
	sectionStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(ColorBlue).
		MarginTop(1)

	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Width(18).
		Align(lipgloss.Right)

	labelActiveStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true).
		Width(18).
		Align(lipgloss.Right)

	valueStyle := lipgloss.NewStyle().
		Foreground(ColorWhite)

	valueActiveStyle := lipgloss.NewStyle().
		Foreground(ColorOrange)

	hintStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	buttonStyle := lipgloss.NewStyle().
		Padding(0, 2).
		Bold(true)

	activeButtonStyle := buttonStyle.
		Background(ColorOrange).
		Foreground(lipgloss.Color("#000000"))

	inactiveButtonStyle := buttonStyle.
		Background(ColorGray).
		Foreground(ColorWhite)

	// Media Folder Section (Output Directory)
	mediaSection := sectionStyle.Render("Media Folder")
	outputLabel := labelStyle.Render("Save to: ")
	if m.focusedField == OptionsFieldOutputDirectory {
		outputLabel = labelActiveStyle.Render("Save to: ")
	}
	var outputValue string
	if m.focusedField == OptionsFieldOutputDirectory {
		outputValue = valueActiveStyle.Render(m.outputDirectory)
	} else {
		outputValue = valueStyle.Render(m.outputDirectory)
	}
	outputRow := lipgloss.JoinHorizontal(lipgloss.Center, outputLabel, outputValue)
	outputHint := hintStyle.Render("                    press enter to browse, c to reset")

	// Topic Management Section
	topicSection := sectionStyle.Render("Topics")

	// Topic list - simple inline display
	var topicItems []string
	for i, topic := range m.topics {
		style := lipgloss.NewStyle().Foreground(ColorGray)
		if i == m.selectedTopic {
			if m.focusedField == OptionsFieldTopicList {
				style = lipgloss.NewStyle().Background(ColorOrange).Foreground(lipgloss.Color("#000000"))
			} else {
				style = lipgloss.NewStyle().Foreground(ColorWhite)
			}
		}
		topicItems = append(topicItems, style.Render(" "+topic.Name+" "))
	}
	topicListStr := lipgloss.JoinHorizontal(lipgloss.Center, topicItems...)

	topicLabel := labelStyle.Render("Topics: ")
	if m.focusedField == OptionsFieldTopicList {
		topicLabel = labelActiveStyle.Render("Topics: ")
	}
	topicRow := lipgloss.JoinHorizontal(lipgloss.Center, topicLabel, topicListStr)

	// Add topic input
	addLabel := labelStyle.Render("Add: ")
	if m.focusedField == OptionsFieldAddTopic {
		addLabel = labelActiveStyle.Render("Add: ")
	}
	addTopicRow := lipgloss.JoinHorizontal(lipgloss.Center, addLabel, m.newTopicInput.View())

	// Remove button
	removeLabel := labelStyle.Render("")
	removeBtn := inactiveButtonStyle.Render("Remove")
	if m.focusedField == OptionsFieldRemoveTopic {
		removeBtn = activeButtonStyle.Render("Remove")
	}
	removeRow := lipgloss.JoinHorizontal(lipgloss.Center, removeLabel, "  ", removeBtn)

	// Default Presenter Section
	presenterSection := sectionStyle.Render("Presenter")
	presenterLabel := labelStyle.Render("Default: ")
	if m.focusedField == OptionsFieldDefaultPresenter {
		presenterLabel = labelActiveStyle.Render("Default: ")
	}
	presenterRow := lipgloss.JoinHorizontal(lipgloss.Center, presenterLabel, m.presenterInput.View())

	// Logo Settings Section
	logoSection := sectionStyle.Render("Logos")

	// Logo directory
	logoDirLabel := labelStyle.Render("Directory: ")
	if m.focusedField == OptionsFieldLogoDirectory {
		logoDirLabel = labelActiveStyle.Render("Directory: ")
	}
	var logoDirValue string
	if m.logoDirectory == "" {
		if m.focusedField == OptionsFieldLogoDirectory {
			logoDirValue = valueActiveStyle.Render("(browse...)")
		} else {
			logoDirValue = hintStyle.Render("(not set)")
		}
	} else {
		if m.focusedField == OptionsFieldLogoDirectory {
			logoDirValue = valueActiveStyle.Render(m.logoDirectory)
		} else {
			logoDirValue = valueStyle.Render(m.logoDirectory)
		}
	}
	logoDirRow := lipgloss.JoinHorizontal(lipgloss.Center, logoDirLabel, logoDirValue)
	logoDirHint := hintStyle.Render("                    logos selected per-recording")

	// Background Color
	bgLabel := labelStyle.Render("Background: ")
	if m.focusedField == OptionsFieldBgColor {
		bgLabel = labelActiveStyle.Render("Background: ")
	}
	var bgColorPills []string
	for i, c := range config.BgColors {
		pillStyle := lipgloss.NewStyle().Padding(0, 1)
		if i == m.bgColorIdx {
			if m.focusedField == OptionsFieldBgColor {
				pillStyle = pillStyle.Background(ColorOrange).Foreground(lipgloss.Color("#000")).Bold(true)
			} else {
				pillStyle = pillStyle.Background(ColorGreen).Foreground(ColorWhite)
			}
		} else {
			pillStyle = pillStyle.Foreground(ColorGray)
		}
		bgColorPills = append(bgColorPills, pillStyle.Render(c))
	}
	bgColorRow := lipgloss.JoinHorizontal(lipgloss.Center, bgLabel, strings.Join(bgColorPills, " "))
	bgColorHint := hintStyle.Render("                    ←/→: change • lower third background")

	// YouTube Section
	youtubeSection := sectionStyle.Render("YouTube")
	youtubeLabel := labelStyle.Render("Status: ")
	if m.focusedField == OptionsFieldYouTubeSetup {
		youtubeLabel = labelActiveStyle.Render("Status: ")
	}

	// Get YouTube status
	cfg, _ := config.Load()
	youtubeStatus := cfg.GetYouTubeAuthStatus()
	var youtubeStatusText string
	var youtubeStatusColor lipgloss.Color
	switch youtubeStatus {
	case 3: // AuthStatusAuthenticated
		youtubeStatusText = "Connected"
		youtubeStatusColor = ColorGreen
		if cfg.YouTube.ChannelName != "" {
			youtubeStatusText = "Connected: " + cfg.YouTube.ChannelName
		}
	case 2: // AuthStatusConfigured
		youtubeStatusText = "Not Connected (press enter to connect)"
		youtubeStatusColor = ColorOrange
	default:
		youtubeStatusText = "Not Set Up (press enter to configure)"
		youtubeStatusColor = ColorGray
	}
	if m.focusedField == OptionsFieldYouTubeSetup {
		youtubeStatusText = "▶ " + youtubeStatusText
	}
	youtubeStatusStyled := lipgloss.NewStyle().Foreground(youtubeStatusColor).Render(youtubeStatusText)
	youtubeRow := lipgloss.JoinHorizontal(lipgloss.Center, youtubeLabel, youtubeStatusStyled)

	// Syndication Section
	syndicationSection := sectionStyle.Render("Syndication")
	syndicationLabel := labelStyle.Render("Accounts: ")
	if m.focusedField == OptionsFieldSyndicationSetup {
		syndicationLabel = labelActiveStyle.Render("Accounts: ")
	}

	// Count syndication accounts
	enabledAccounts := cfg.Syndication.GetEnabledAccounts()
	totalAccounts := len(cfg.Syndication.GetAccounts())
	var syndicationStatusText string
	var syndicationStatusColor lipgloss.Color
	if totalAccounts == 0 {
		syndicationStatusText = "No accounts (press enter to configure)"
		syndicationStatusColor = ColorGray
	} else {
		syndicationStatusText = fmt.Sprintf("%d enabled of %d (press enter to manage)", len(enabledAccounts), totalAccounts)
		if len(enabledAccounts) > 0 {
			syndicationStatusColor = ColorGreen
		} else {
			syndicationStatusColor = ColorOrange
		}
	}
	if m.focusedField == OptionsFieldSyndicationSetup {
		syndicationStatusText = "▶ " + syndicationStatusText
	}
	syndicationStatusStyled := lipgloss.NewStyle().Foreground(syndicationStatusColor).Render(syndicationStatusText)
	syndicationRow := lipgloss.JoinHorizontal(lipgloss.Center, syndicationLabel, syndicationStatusStyled)

	// Recording Presets Section
	presetSection := sectionStyle.Render("Recording Presets")
	presetHint := hintStyle.Render("                    defaults for systray quick-record")

	audioPresetLabel := labelStyle.Render("Audio: ")
	if m.focusedField == OptionsFieldPresetRecordAudio {
		audioPresetLabel = labelActiveStyle.Render("Audio: ")
	}
	audioPresetRow := lipgloss.JoinHorizontal(lipgloss.Center,
		audioPresetLabel, m.renderPresetToggle(m.presetRecordAudio, m.focusedField == OptionsFieldPresetRecordAudio))

	webcamPresetLabel := labelStyle.Render("Webcam: ")
	if m.focusedField == OptionsFieldPresetRecordWebcam {
		webcamPresetLabel = labelActiveStyle.Render("Webcam: ")
	}
	webcamPresetRow := lipgloss.JoinHorizontal(lipgloss.Center,
		webcamPresetLabel, m.renderPresetToggle(m.presetRecordWebcam, m.focusedField == OptionsFieldPresetRecordWebcam))

	screenPresetLabel := labelStyle.Render("Screen: ")
	if m.focusedField == OptionsFieldPresetRecordScreen {
		screenPresetLabel = labelActiveStyle.Render("Screen: ")
	}
	screenPresetRow := lipgloss.JoinHorizontal(lipgloss.Center,
		screenPresetLabel, m.renderPresetToggle(m.presetRecordScreen, m.focusedField == OptionsFieldPresetRecordScreen))

	verticalDisabled := !m.presetRecordWebcam && !m.presetRecordScreen
	verticalPresetLabel := labelStyle.Render("Vertical: ")
	if m.focusedField == OptionsFieldPresetVerticalVideo {
		verticalPresetLabel = labelActiveStyle.Render("Vertical: ")
	}
	verticalPresetRow := lipgloss.JoinHorizontal(lipgloss.Center,
		verticalPresetLabel, m.renderPresetToggleWithDisabled(m.presetVerticalVideo, m.focusedField == OptionsFieldPresetVerticalVideo, verticalDisabled))

	leftSplitDisabled := !m.presetVerticalVideo
	leftSplitPresetLabel := labelStyle.Render("Left Split: ")
	if m.focusedField == OptionsFieldPresetLeftSplit {
		leftSplitPresetLabel = labelActiveStyle.Render("Left Split: ")
	}
	leftSplitPresetRow := lipgloss.JoinHorizontal(lipgloss.Center,
		leftSplitPresetLabel, m.renderPresetToggleWithDisabled(m.presetLeftSplit, m.focusedField == OptionsFieldPresetLeftSplit, leftSplitDisabled))

	webcamBubbleDisabled := !m.presetVerticalVideo || !m.presetLeftSplit
	webcamBubblePresetLabel := labelStyle.Render("Webcam Bubble: ")
	if m.focusedField == OptionsFieldPresetWebcamBubble {
		webcamBubblePresetLabel = labelActiveStyle.Render("Webcam Bubble: ")
	}
	webcamBubblePresetRow := lipgloss.JoinHorizontal(lipgloss.Center,
		webcamBubblePresetLabel, m.renderPresetToggleWithDisabledMsg(m.presetWebcamBubble, m.focusedField == OptionsFieldPresetWebcamBubble, webcamBubbleDisabled, "(requires left split)"))

	logosPresetLabel := labelStyle.Render("Logos: ")
	if m.focusedField == OptionsFieldPresetAddLogos {
		logosPresetLabel = labelActiveStyle.Render("Logos: ")
	}
	logosPresetRow := lipgloss.JoinHorizontal(lipgloss.Center,
		logosPresetLabel, m.renderPresetToggle(m.presetAddLogos, m.focusedField == OptionsFieldPresetAddLogos))

	// Save button
	saveLabel := labelStyle.Render("")
	saveBtn := inactiveButtonStyle.Render("Save")
	if m.focusedField == OptionsFieldSave {
		saveBtn = activeButtonStyle.Render("Save")
	}
	saveRow := lipgloss.JoinHorizontal(lipgloss.Center, saveLabel, "  ", saveBtn)

	// Message/Error display
	var statusLine string
	if m.err != nil {
		statusLine = lipgloss.NewStyle().
			Foreground(ColorRed).
			Bold(true).
			Render("Error: " + m.err.Error())
	} else if m.message != "" {
		statusLine = lipgloss.NewStyle().
			Foreground(ColorGreen).
			Bold(true).
			Render(m.message)
	}

	// Build the view - return just the content (header/footer added by app.go)
	return lipgloss.JoinVertical(lipgloss.Left,
		mediaSection,
		outputRow,
		outputHint,
		topicSection,
		topicRow,
		addTopicRow,
		removeRow,
		presenterSection,
		presenterRow,
		logoSection,
		logoDirRow,
		logoDirHint,
		bgColorRow,
		bgColorHint,
		youtubeSection,
		youtubeRow,
		syndicationSection,
		syndicationRow,
		presetSection,
		presetHint,
		audioPresetRow,
		webcamPresetRow,
		screenPresetRow,
		verticalPresetRow,
		leftSplitPresetRow,
		webcamBubblePresetRow,
		logosPresetRow,
		"",
		saveRow,
		"",
		statusLine,
	)
}

// renderPresetToggle renders a Yes/No toggle pill for preset fields
func (m *OptionsModel) renderPresetToggle(value bool, focused bool) string {
	yesStyle := lipgloss.NewStyle().Padding(0, 1)
	noStyle := lipgloss.NewStyle().Padding(0, 1)

	if value {
		if focused {
			yesStyle = yesStyle.Background(ColorOrange).Foreground(lipgloss.Color("#000")).Bold(true)
		} else {
			yesStyle = yesStyle.Background(ColorGreen).Foreground(ColorWhite)
		}
		noStyle = noStyle.Foreground(ColorGray)
	} else {
		yesStyle = yesStyle.Foreground(ColorGray)
		if focused {
			noStyle = noStyle.Background(ColorOrange).Foreground(lipgloss.Color("#000")).Bold(true)
		} else {
			noStyle = noStyle.Background(ColorGray).Foreground(ColorWhite)
		}
	}

	return yesStyle.Render("Yes") + " " + noStyle.Render("No")
}

// renderPresetToggleWithDisabled renders a toggle or disabled hint
func (m *OptionsModel) renderPresetToggleWithDisabled(value bool, focused bool, disabled bool) string {
	return m.renderPresetToggleWithDisabledMsg(value, focused, disabled, "(requires webcam or screen)")
}

// renderPresetToggleWithDisabledMsg renders a toggle or disabled hint with a custom message
func (m *OptionsModel) renderPresetToggleWithDisabledMsg(value bool, focused bool, disabled bool, msg string) string {
	if disabled {
		disabledStyle := lipgloss.NewStyle().Foreground(ColorGray).Italic(true)
		return disabledStyle.Render(msg)
	}
	return m.renderPresetToggle(value, focused)
}

// updateFileBrowser handles messages when the file browser is active
func (m *OptionsModel) updateFileBrowser(msg tea.Msg) (*OptionsModel, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		return m, nil

	case tea.KeyMsg:
		// Handle path input mode
		if m.browserField == FileBrowserFieldPathInput {
			switch msg.String() {
			case "esc":
				m.browserField = FileBrowserFieldList
				m.browserPathInput.Blur()
				return m, nil
			case "enter":
				// Try to navigate to the entered path
				path := m.browserPathInput.Value()
				if info, err := os.Stat(path); err == nil {
					if info.IsDir() {
						m.browserCurrentDir = path
						m.loadBrowserEntries()
					}
				}
				m.browserField = FileBrowserFieldList
				m.browserPathInput.Blur()
				return m, nil
			default:
				var cmd tea.Cmd
				m.browserPathInput, cmd = m.browserPathInput.Update(msg)
				return m, cmd
			}
		}

		// Handle file list mode
		switch msg.String() {
		case "esc", "q":
			m.closeFileBrowser()
			return m, nil

		case "up", "k":
			if m.browserSelected > 0 {
				m.browserSelected--
				// Scroll up if needed
				if m.browserSelected < m.browserScrollTop {
					m.browserScrollTop = m.browserSelected
				}
			}
			return m, nil

		case "down", "j":
			if m.browserSelected < len(m.browserEntries)-1 {
				m.browserSelected++
				// Scroll down if needed
				visibleHeight := m.height - 12 // Account for header, footer, path input
				if m.browserSelected >= m.browserScrollTop+visibleHeight {
					m.browserScrollTop = m.browserSelected - visibleHeight + 1
				}
			}
			return m, nil

		case "enter", " ":
			if len(m.browserEntries) > 0 && m.browserSelected < len(m.browserEntries) {
				entry := m.browserEntries[m.browserSelected]
				if entry.isDir {
					// Navigate into directory
					m.browserCurrentDir = entry.path
					m.browserPathInput.SetValue(entry.path)
					m.loadBrowserEntries()
				}
			}
			return m, nil

		case "s":
			// Select current directory (when selecting directory)
			if m.selectingDirectory {
				switch m.browserTarget {
				case BrowserTargetOutput:
					m.outputDirectory = m.browserCurrentDir
					m.config.OutputDir = m.browserCurrentDir
					if err := config.Save(m.config); err != nil {
						m.message = "Error saving: " + err.Error()
					} else {
						m.message = "Output directory saved: " + m.browserCurrentDir
					}
				case BrowserTargetLogo:
					m.logoDirectory = m.browserCurrentDir
					m.config.LogoDirectory = m.browserCurrentDir
					if err := config.Save(m.config); err != nil {
						m.message = "Error saving: " + err.Error()
					} else {
						m.message = "Logo directory saved: " + m.browserCurrentDir
					}
				}
				m.closeFileBrowser()
			}
			return m, nil

		case "backspace":
			// Go to parent directory
			if m.browserCurrentDir != "/" {
				m.browserCurrentDir = filepath.Dir(m.browserCurrentDir)
				m.browserPathInput.SetValue(m.browserCurrentDir)
				m.loadBrowserEntries()
			}
			return m, nil

		case "tab", "/":
			// Switch to path input
			m.browserField = FileBrowserFieldPathInput
			m.browserPathInput.Focus()
			return m, textinput.Blink

		case "~":
			// Go to home directory
			if home, err := os.UserHomeDir(); err == nil {
				m.browserCurrentDir = home
				m.browserPathInput.SetValue(home)
				m.loadBrowserEntries()
			}
			return m, nil
		}
	}

	return m, nil
}

// renderFileBrowser renders the custom file browser
func (m *OptionsModel) renderFileBrowser() string {
	// Page title based on target
	var pageTitle string
	switch m.browserTarget {
	case BrowserTargetOutput:
		pageTitle = "Select Media Folder"
	case BrowserTargetLogo:
		pageTitle = "Select Logo Directory"
	default:
		pageTitle = "Select Directory"
	}

	header := RenderHeader(pageTitle)

	// Styles
	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Width(10).
		Align(lipgloss.Right)

	labelActiveStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true).
		Width(10).
		Align(lipgloss.Right)

	dirStyle := lipgloss.NewStyle().
		Foreground(ColorBlue)

	selectedStyle := lipgloss.NewStyle().
		Background(ColorOrange).
		Foreground(lipgloss.Color("#000000"))

	// Path input row
	pathLabel := labelStyle.Render("Path: ")
	if m.browserField == FileBrowserFieldPathInput {
		pathLabel = labelActiveStyle.Render("Path: ")
	}
	pathRow := lipgloss.JoinHorizontal(lipgloss.Center, pathLabel, m.browserPathInput.View())

	// Current directory display
	dirLabel := labelStyle.Render("In: ")
	dirRow := lipgloss.JoinHorizontal(lipgloss.Center, dirLabel, lipgloss.NewStyle().Foreground(ColorGray).Render(m.browserCurrentDir))

	// File list - calculate available height more precisely
	// Header: ~6 lines, path row: 1, dir row: 1, empty line: 1, footer: 2, margins: 3
	visibleHeight := m.height - 14
	if visibleHeight < 3 {
		visibleHeight = 3
	}

	// Ensure scroll position keeps selected item visible
	if m.browserSelected < m.browserScrollTop {
		m.browserScrollTop = m.browserSelected
	} else if m.browserSelected >= m.browserScrollTop+visibleHeight {
		m.browserScrollTop = m.browserSelected - visibleHeight + 1
	}

	// Clamp scroll position
	if m.browserScrollTop < 0 {
		m.browserScrollTop = 0
	}
	maxScroll := len(m.browserEntries) - visibleHeight
	if maxScroll < 0 {
		maxScroll = 0
	}
	if m.browserScrollTop > maxScroll {
		m.browserScrollTop = maxScroll
	}

	var fileLines []string
	endIdx := m.browserScrollTop + visibleHeight
	if endIdx > len(m.browserEntries) {
		endIdx = len(m.browserEntries)
	}

	for i := m.browserScrollTop; i < endIdx; i++ {
		entry := m.browserEntries[i]
		var line string
		if entry.isDir {
			if i == m.browserSelected && m.browserField == FileBrowserFieldList {
				line = selectedStyle.Render("▶ 📁 " + entry.name)
			} else {
				line = dirStyle.Render("  📁 " + entry.name)
			}
		}
		if line != "" {
			fileLines = append(fileLines, line)
		}
	}

	// Add scroll indicators if needed
	scrollInfo := ""
	if len(m.browserEntries) > visibleHeight {
		scrollInfo = lipgloss.NewStyle().Foreground(ColorGray).Render(
			fmt.Sprintf(" [%d-%d of %d]", m.browserScrollTop+1, endIdx, len(m.browserEntries)))
	}

	fileList := lipgloss.JoinVertical(lipgloss.Left, fileLines...)
	if len(m.browserEntries) == 0 {
		fileList = lipgloss.NewStyle().Foreground(ColorGray).Italic(true).Render("(no subdirectories)")
	}

	// Content - use a fixed height box for the file list to prevent overflow
	content := lipgloss.JoinVertical(lipgloss.Left,
		pathRow,
		dirRow+scrollInfo,
		"",
		fileList,
	)

	// Help footer
	helpText := "↑/k ↓/j: navigate • enter: open dir • s: select this dir • backspace: parent • ~: home • esc: cancel"
	footer := RenderHelpFooter(helpText, m.width)

	return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
}

// goToYouTubeSetupMsg signals navigation to YouTube setup screen
type goToYouTubeSetupMsg struct{}

// goToSyndicationSetupMsg signals navigation to syndication setup screen
type goToSyndicationSetupMsg struct{}
