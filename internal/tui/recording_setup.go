package tui

import (
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/kartoza/kartoza-screencaster/internal/config"
	"github.com/kartoza/kartoza-screencaster/internal/models"
	"github.com/kartoza/kartoza-screencaster/internal/monitor"
)

// RecordingSetupModel handles the recording setup form
type RecordingSetupModel struct {
	width  int
	height int

	config *config.Config
	form   *RecordingForm

	// Logo directory and available logos (needed for logo path resolution)
	logoDirectory  string
	availableLogos []string

	// Monitors for screen recording
	monitors []models.Monitor
}

// NewRecordingSetupModel creates a new recording setup model
func NewRecordingSetupModel() *RecordingSetupModel {
	cfg, _ := config.Load()

	topics := cfg.Topics
	if len(topics) == 0 {
		topics = models.DefaultTopics()
	}

	// Get available monitors
	monitors, _ := monitor.ListMonitors()

	m := &RecordingSetupModel{
		config:        cfg,
		logoDirectory: cfg.LogoDirectory,
		monitors:      monitors,
	}

	// Load available logos from directory
	m.loadAvailableLogos()

	// Create the shared form
	m.form = NewRecordingForm(&RecordingFormConfig{
		Mode:     FormModeNewRecording,
		Topics:   topics,
		Monitors: monitors,
		Logos:    m.availableLogos[1:], // Skip the "(none)" entry, form handles that
		OnConfirm: func() {
			// Will be handled by the parent via message
		},
		OnCancel: func() {
			// Will be handled by the parent via message
		},
	})

	// Set logo indices from last used
	m.setLogoIndicesFromConfig()

	// Set topic from presets
	presets := cfg.RecordingPresets
	presetsExist := presets.RecordAudio || presets.RecordWebcam || presets.RecordScreen || presets.VerticalVideo || presets.AddLogos
	if !presetsExist {
		presets = config.DefaultRecordingPresets()
	}
	if presets.Topic != "" {
		m.form.SetSelectedTopic(presets.Topic)
	}

	// Focus the title field
	m.form.Focus()

	return m
}

// loadAvailableLogos scans the logo directory for image files
func (m *RecordingSetupModel) loadAvailableLogos() {
	m.availableLogos = []string{"(none)"} // First option is always "none"

	if m.logoDirectory == "" {
		return
	}

	entries, err := os.ReadDir(m.logoDirectory)
	if err != nil {
		return
	}

	var logos []string
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		ext := strings.ToLower(filepath.Ext(entry.Name()))
		if ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" {
			logos = append(logos, entry.Name())
		}
	}

	sort.Strings(logos)
	m.availableLogos = append(m.availableLogos, logos...)
}

// setLogoIndicesFromConfig sets the logo indices from the last used config
func (m *RecordingSetupModel) setLogoIndicesFromConfig() {
	if m.form == nil {
		return
	}

	// Find indices for current selections
	leftIdx := m.findLogoIndex(m.config.LastUsedLogos.LeftLogo)
	rightIdx := m.findLogoIndex(m.config.LastUsedLogos.RightLogo)
	bottomIdx := m.findLogoIndex(m.config.LastUsedLogos.BottomLogo)

	m.form.State.SelectedLeftIdx = leftIdx
	m.form.State.SelectedRightIdx = rightIdx
	m.form.State.SelectedBottomIdx = bottomIdx

	// Set color index
	titleColor := m.config.LastUsedLogos.TitleColor
	if titleColor == "" {
		titleColor = config.DefaultTitleColor
	}
	for i, c := range config.TitleColors {
		if c == titleColor {
			m.form.State.SelectedColorIdx = i
			break
		}
	}

	// Set GIF loop mode index
	gifLoopMode := m.config.LastUsedLogos.GifLoopMode
	if gifLoopMode == "" {
		gifLoopMode = config.GifLoopContinuous
	}
	for i, mode := range config.GifLoopModes {
		if mode == gifLoopMode {
			m.form.State.SelectedGifLoopIdx = i
			break
		}
	}
}

// findLogoIndex finds the index of a logo path in availableLogos
func (m *RecordingSetupModel) findLogoIndex(logoPath string) int {
	if logoPath == "" {
		return 0 // (none)
	}
	name := filepath.Base(logoPath)
	for i, logo := range m.availableLogos {
		if logo == name {
			return i
		}
	}
	return 0
}

// getLogoPath returns the full path for a selected logo index
func (m *RecordingSetupModel) getLogoPath(idx int) string {
	if idx == 0 || idx >= len(m.availableLogos) || m.logoDirectory == "" {
		return ""
	}
	return filepath.Join(m.logoDirectory, m.availableLogos[idx])
}

func (m *RecordingSetupModel) Init() tea.Cmd {
	return nil
}

func (m *RecordingSetupModel) Update(msg tea.Msg) (*RecordingSetupModel, tea.Cmd) {
	var cmd tea.Cmd

	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		// Account for header ~6 lines and footer ~2 lines
		contentHeight := msg.Height - 8
		if contentHeight < 10 {
			contentHeight = 10
		}
		m.form.SetSize(msg.Width, contentHeight)

	case tea.KeyMsg:
		// Check for confirm/cancel actions
		if m.form.State.FocusedField == FormFieldConfirm && !m.form.State.InputMode {
			switch msg.String() {
			case "enter", " ":
				if m.form.State.ConfirmSelected {
					// Go Live selected
					if m.Validate() {
						return m, func() tea.Msg { return recordingSetupCompleteMsg{} }
					}
				} else {
					// Cancel selected
					return m, func() tea.Msg { return backToMenuMsg{} }
				}
				return m, nil
			}
		}

		// Delegate to form
		m.form, cmd = m.form.Update(msg)
		return m, cmd
	}

	return m, cmd
}

func (m *RecordingSetupModel) Validate() bool {
	// Title is required
	if m.form.GetTitle() == "" {
		return false
	}
	// At least one recording source must be enabled
	if !m.form.State.RecordAudio && !m.form.State.RecordWebcam && !m.form.State.RecordScreen {
		return false
	}
	return true
}

func (m *RecordingSetupModel) GetMetadata() models.RecordingMetadata {
	topic := m.form.GetSelectedTopic().Name

	recordingNumber := 1
	if num, err := strconv.Atoi(m.form.GetNumber()); err == nil && num > 0 {
		recordingNumber = num
	}

	// Use presenter from form input (which defaults to last used presenter)
	presenter := m.form.GetPresenter()

	metadata := models.RecordingMetadata{
		Number:      recordingNumber,
		Title:       m.form.GetTitle(),
		Description: m.form.GetDescription(),
		Topic:       topic,
		Presenter:   presenter,
	}
	metadata.GenerateFolderName()

	return metadata
}

func (m *RecordingSetupModel) GetRecordingOptions() models.RecordingOptions {
	monitorName := ""
	if m.form.State.RecordScreen && m.form.State.SelectedMonitor >= 0 && m.form.State.SelectedMonitor < len(m.monitors) {
		monitorName = m.monitors[m.form.State.SelectedMonitor].Name
	}

	return models.RecordingOptions{
		Monitor:        monitorName,
		NoAudio:        !m.form.State.RecordAudio,
		NoWebcam:       !m.form.State.RecordWebcam,
		NoScreen:       !m.form.State.RecordScreen,
		CreateVertical: m.form.State.VerticalVideo && m.form.State.RecordWebcam && m.form.State.RecordScreen,
	}
}

// GetLogoSelection returns the selected logos
func (m *RecordingSetupModel) GetLogoSelection() config.LogoSelection {
	if !m.form.State.AddLogos {
		return config.LogoSelection{}
	}
	return config.LogoSelection{
		LeftLogo:    m.getLogoPath(m.form.State.SelectedLeftIdx),
		RightLogo:   m.getLogoPath(m.form.State.SelectedRightIdx),
		BottomLogo:  m.getLogoPath(m.form.State.SelectedBottomIdx),
		TitleColor:  config.TitleColors[m.form.State.SelectedColorIdx],
		GifLoopMode: config.GifLoopModes[m.form.State.SelectedGifLoopIdx],
	}
}

// SaveLogoSelection saves the current logo selection to config for next time
func (m *RecordingSetupModel) SaveLogoSelection() error {
	cfg, err := config.Load()
	if err != nil {
		return err
	}
	cfg.LastUsedLogos = m.GetLogoSelection()
	return config.Save(cfg)
}

// GetRecordingPresets returns the current recording presets
func (m *RecordingSetupModel) GetRecordingPresets() config.RecordingPresets {
	return config.RecordingPresets{
		RecordAudio:   m.form.State.RecordAudio,
		RecordWebcam:  m.form.State.RecordWebcam,
		RecordScreen:  m.form.State.RecordScreen,
		VerticalVideo: m.form.State.VerticalVideo,
		LeftSplit:     m.form.State.LeftSplit,
		AddLogos:      m.form.State.AddLogos,
		Topic:         m.form.GetSelectedTopic().Name,
	}
}

// SaveAllPresets saves all recording presets (toggles, topic, logos, presenter) to config
// This should be called when starting a recording to remember settings for next time
func (m *RecordingSetupModel) SaveAllPresets() error {
	cfg, err := config.Load()
	if err != nil {
		return err
	}

	// Save recording presets (toggles and topic)
	cfg.RecordingPresets = m.GetRecordingPresets()
	cfg.PresetsConfigured = true

	// Save logo selection
	cfg.LastUsedLogos = m.GetLogoSelection()

	// Save presenter name for next recording
	presenter := m.form.GetPresenter()
	if presenter != "" {
		cfg.DefaultPresenter = presenter
	}

	return config.Save(cfg)
}

// View renders the recording setup form content (layout is handled by app.go)
func (m *RecordingSetupModel) View() string {
	return m.form.View()
}

type recordingSetupCompleteMsg struct{}
