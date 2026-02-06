package tui

import (
	"fmt"
	"strings"
	"time"

	"github.com/charmbracelet/bubbles/key"
	"github.com/charmbracelet/bubbles/spinner"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
	"github.com/kartoza/kartoza-screencaster/internal/beep"
	"github.com/kartoza/kartoza-screencaster/internal/deps"
	"github.com/kartoza/kartoza-screencaster/internal/models"
	"github.com/kartoza/kartoza-screencaster/internal/monitor"
	"github.com/kartoza/kartoza-screencaster/internal/recorder"
)

// Application states
type appState int

const (
	stateReady appState = iota
	stateCountdown
	stateRecording
	stateRoomNoise  // Capturing room noise after recording stops
	stateProcessing
)

// Key bindings
type keyMap struct {
	Toggle key.Binding
	Quit   key.Binding
	Help   key.Binding
	Cancel key.Binding
}

var keys = keyMap{
	Toggle: key.NewBinding(
		key.WithKeys(" ", "enter"),
		key.WithHelp("space/enter", "toggle recording"),
	),
	Quit: key.NewBinding(
		key.WithKeys("q", "ctrl+c"),
		key.WithHelp("q", "quit"),
	),
	Help: key.NewBinding(
		key.WithKeys("?"),
		key.WithHelp("?", "help"),
	),
	Cancel: key.NewBinding(
		key.WithKeys("esc"),
		key.WithHelp("esc", "cancel"),
	),
}

// Messages
type tickMsg time.Time
type statusUpdateMsg models.RecordingStatus
type monitorsUpdateMsg []models.Monitor
type blinkMsg struct{}
type countdownTickMsg struct{}
type pauseCompleteMsg struct{ err error }
type resumeCompleteMsg struct{ err error }
type roomNoiseTickMsg struct{}
type roomNoiseCompleteMsg struct{ err error }

// Model is the main TUI model
type Model struct {
	recorder        *recorder.Recorder
	status          models.RecordingStatus
	monitors        []models.Monitor
	spinner         spinner.Model
	width           int
	height          int
	showHelp        bool
	blinkOn         bool
	err             error
	state           appState
	countdownNum    int
	processing      *ProcessingState
	processingFrame int
	processingBtn   ProcessingButton
	processingDone  bool
}

// NewModel creates a new TUI model
func NewModel() Model {
	s := spinner.New()
	s.Spinner = spinner.Dot
	s.Style = lipgloss.NewStyle().Foreground(ColorRed)

	rec := recorder.New()

	// Check if a recording is already in progress (restore state)
	initialState := stateReady
	status := rec.GetStatus()
	if status.IsRecording {
		initialState = stateRecording
	}

	return Model{
		recorder:        rec,
		spinner:         s,
		blinkOn:         true,
		state:           initialState,
		status:          status,
		countdownNum:    5,
		processing:      NewProcessingState(),
		processingFrame: 0,
	}
}

// Init initializes the model
func (m Model) Init() tea.Cmd {
	return tea.Batch(
		m.spinner.Tick,
		tickCmd(),
		blinkCmd(),
		updateStatus(m.recorder),
		updateMonitors(),
	)
}

// Update handles messages
func (m Model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		// Handle processing state - only allow quit
		if m.state == stateProcessing {
			// During processing, only allow quit (but warn user)
			if key.Matches(msg, keys.Quit) {
				// Allow quitting but processing continues in background
				return m, tea.Quit
			}
			// Ignore all other keys during processing
			return m, nil
		}

		// Handle cancel during countdown
		if m.state == stateCountdown {
			if key.Matches(msg, keys.Cancel) || msg.String() == "q" {
				m.state = stateReady
				m.countdownNum = 5
				return m, nil
			}
			// Ignore other keys during countdown
			return m, nil
		}

		switch {
		case key.Matches(msg, keys.Quit):
			return m, tea.Quit

		case key.Matches(msg, keys.Toggle):
			if m.status.IsRecording {
				// Stop recording - transition to processing state
				m.state = stateProcessing
				m.processing.Reset()
				m.processing.Start()
				m.processingFrame = 0

				// Start stop and processing in background
				return m, tea.Batch(
					processingTickCmd(),
					m.stopAndProcess(),
				)
			} else {
				// Start countdown
				m.state = stateCountdown
				m.countdownNum = 5
				// Play first beep
				go beep.Play(5)
				return m, tea.Tick(time.Second, func(t time.Time) tea.Msg {
					return countdownTickMsg{}
				})
			}

		case key.Matches(msg, keys.Help):
			m.showHelp = !m.showHelp
			return m, nil
		}

	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		return m, nil

	case tickMsg:
		if m.state != stateCountdown {
			return m, tea.Batch(
				tickCmd(),
				updateStatus(m.recorder),
				updateMonitors(),
			)
		}
		return m, tickCmd()

	case blinkMsg:
		m.blinkOn = !m.blinkOn
		return m, blinkCmd()

	case countdownTickMsg:
		if m.state != stateCountdown {
			return m, nil
		}

		m.countdownNum--

		if m.countdownNum < 0 {
			// Countdown finished, start recording
			m.state = stateRecording
			if err := m.recorder.Start(); err != nil {
				m.err = err
				m.state = stateReady
			}
			return m, updateStatus(m.recorder)
		}

		// Play beep for counts 5-1 (not for 0/GO)
		if m.countdownNum > 0 {
			go beep.Play(m.countdownNum)
		}

		return m, tea.Tick(time.Second, func(t time.Time) tea.Msg {
			return countdownTickMsg{}
		})

	case statusUpdateMsg:
		m.status = models.RecordingStatus(msg)
		if m.status.IsRecording {
			m.state = stateRecording
		} else if m.state == stateRecording {
			m.state = stateReady
		}
		return m, nil

	case monitorsUpdateMsg:
		m.monitors = []models.Monitor(msg)
		return m, nil

	case spinner.TickMsg:
		var cmd tea.Cmd
		m.spinner, cmd = m.spinner.Update(msg)
		return m, cmd

	case processingTickMsg:
		if m.state == stateProcessing {
			m.processingFrame++
			return m, processingTickCmd()
		}
		return m, nil

	case processingStepMsg:
		if m.state == stateProcessing && m.processing != nil {
			if msg.Step < 0 {
				// Skip step
				m.processing.SkipStep()
			} else {
				m.processing.NextStep()
			}
		}
		return m, nil

	case processingCompleteMsg:
		if m.state == stateProcessing && m.processing != nil {
			m.processing.Complete()
			// Brief delay to show completion, then return to ready
			return m, tea.Tick(1500*time.Millisecond, func(t time.Time) tea.Msg {
				return processingDoneMsg{}
			})
		}
		return m, nil

	case processingDoneMsg:
		m.state = stateReady
		m.processing.Reset()
		return m, updateStatus(m.recorder)

	case processingErrorMsg:
		if m.state == stateProcessing && m.processing != nil {
			m.processing.FailStep(msg.Error)
			m.err = msg.Error
		}
		return m, nil
	}

	return m, nil
}

// processingDoneMsg signals processing display is complete
type processingDoneMsg struct{}

// View renders the UI using the standard layout
func (m Model) View() string {
	if m.width == 0 || m.height == 0 {
		return ""
	}

	// Show countdown screen if in countdown state
	if m.state == stateCountdown {
		return m.renderCountdownView()
	}

	// Show processing screen if in processing state
	if m.state == stateProcessing {
		return RenderProcessingView(m.processing, m.width, m.height, m.processingFrame, m.processingBtn, false, nil)
	}

	// Update global app state for header
	status := "Ready"
	if m.status.IsRecording {
		status = "Recording"
	}
	GlobalAppState.IsRecording = m.status.IsRecording
	GlobalAppState.BlinkOn = m.blinkOn
	GlobalAppState.Status = status

	// Get current monitor with cursor
	cursorMonitor, _ := monitor.GetMouseMonitor()

	// Render header
	screenTitle := "Recording"
	header := RenderHeader(screenTitle)

	// Render main content
	content := m.renderContent(cursorMonitor)

	// Render footer
	helpText := "space: toggle recording • q: quit • ?: help"
	footer := RenderHelpFooter(helpText, m.width)

	// Use standard layout
	return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
}

// renderCountdownView renders the countdown screen
func (m Model) renderCountdownView() string {
	var bigText []string
	var color lipgloss.Color

	if m.countdownNum > 0 {
		// Show digit
		bigText = getBigDigit(m.countdownNum)
		// Color changes as countdown progresses (orange -> red)
		switch m.countdownNum {
		case 5, 4:
			color = ColorOrange
		case 3, 2:
			color = lipgloss.Color("#FF8C00") // Dark orange
		case 1:
			color = ColorRed
		}
	} else {
		// Show GO!
		bigText = bigGO
		color = ColorGreen
	}

	// Style the big text
	digitStyle := lipgloss.NewStyle().
		Foreground(color).
		Bold(true)

	// Build the display
	var lines string
	for i, line := range bigText {
		lines += digitStyle.Render(line)
		if i < len(bigText)-1 {
			lines += "\n"
		}
	}

	// Add subtitle
	subtitleStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	var subtitle string
	if m.countdownNum > 0 {
		subtitle = subtitleStyle.Render("Get ready... Recording starts soon!")
	} else {
		subtitle = subtitleStyle.Render("Recording!")
	}

	// Add cancel hint
	hintStyle := lipgloss.NewStyle().
		Foreground(ColorGray)
	hint := hintStyle.Render("esc: cancel")

	// Combine content
	content := lipgloss.JoinVertical(
		lipgloss.Center,
		"",
		lines,
		"",
		subtitle,
		"",
		hint,
	)

	// Center on screen
	return lipgloss.Place(
		m.width,
		m.height,
		lipgloss.Center,
		lipgloss.Center,
		content,
	)
}

// renderContent renders the main content area
func (m Model) renderContent(cursorMonitor string) string {
	// Monitor list
	monitorsContent := m.renderMonitors(cursorMonitor)

	// Recording info (if recording)
	var recordingInfo string
	if m.status.IsRecording {
		recordingInfo = m.renderRecordingInfo()
	}

	// Help content (if shown)
	var helpContent string
	if m.showHelp {
		helpContent = m.renderHelp()
	}

	// Combine content
	contentStyle := lipgloss.NewStyle().
		Width(HeaderWidth).
		Align(lipgloss.Center)

	var sections []string
	sections = append(sections, monitorsContent)

	if recordingInfo != "" {
		sections = append(sections, "", recordingInfo)
	}

	if helpContent != "" {
		sections = append(sections, "", helpContent)
	}

	content := lipgloss.JoinVertical(lipgloss.Center, sections...)
	return contentStyle.Render(content)
}

// renderMonitors renders the monitor list
func (m Model) renderMonitors(cursorMonitor string) string {
	titleStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(ColorBlue).
		MarginBottom(1)

	markerStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	monitorStyle := lipgloss.NewStyle().
		Foreground(ColorWhite)

	dimStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	var content string
	content += titleStyle.Render("Available Monitors") + "\n"

	if len(m.monitors) == 0 {
		content += dimStyle.Render("No monitors detected")
	} else {
		for _, mon := range m.monitors {
			marker := "  "
			style := monitorStyle
			if mon.Name == cursorMonitor {
				marker = markerStyle.Render("→ ")
				style = ActiveStyle
			}
			line := fmt.Sprintf("%s%s (%dx%d)",
				marker,
				mon.Name,
				mon.Width,
				mon.Height,
			)
			content += style.Render(line) + "\n"
		}
	}

	return content
}

// renderRecordingInfo renders info about the current recording
func (m Model) renderRecordingInfo() string {
	titleStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(ColorRed).
		MarginBottom(1)

	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	valueStyle := lipgloss.NewStyle().
		Foreground(ColorWhite)

	var content string
	content += titleStyle.Render("Recording In Progress") + "\n"
	content += labelStyle.Render("Video: ") + valueStyle.Render(m.status.VideoFile) + "\n"
	content += labelStyle.Render("Audio: ") + valueStyle.Render(m.status.AudioFile) + "\n"

	if m.status.WebcamFile != "" {
		content += labelStyle.Render("Webcam: ") + valueStyle.Render(m.status.WebcamFile) + "\n"
	}

	return content
}

// renderHelp renders the help content
func (m Model) renderHelp() string {
	titleStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(ColorBlue).
		MarginBottom(1)

	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	helpText := `Keyboard Shortcuts:
  space/enter  Toggle recording on/off
  q            Quit application
  ?            Toggle this help

Recording Features:
  • Video captured with wl-screenrec
  • Audio from default microphone
  • Webcam recorded if available
  • Audio denoised & normalized
  • Vertical video with webcam overlay`

	return titleStyle.Render("Help") + "\n" + helpStyle.Render(helpText)
}

// Commands

func tickCmd() tea.Cmd {
	return tea.Tick(time.Second, func(t time.Time) tea.Msg {
		return tickMsg(t)
	})
}

func blinkCmd() tea.Cmd {
	return tea.Tick(500*time.Millisecond, func(t time.Time) tea.Msg {
		return blinkMsg{}
	})
}

func updateStatus(rec *recorder.Recorder) tea.Cmd {
	return func() tea.Msg {
		return statusUpdateMsg(rec.GetStatus())
	}
}

func updateMonitors() tea.Cmd {
	return func() tea.Msg {
		monitors, err := monitor.ListMonitors()
		if err != nil {
			return monitorsUpdateMsg{}
		}
		return monitorsUpdateMsg(monitors)
	}
}

// stopAndProcess stops recording and runs post-processing with step updates
func (m Model) stopAndProcess() tea.Cmd {
	return func() tea.Msg {
		// Get file paths before stopping
		status := m.recorder.GetStatus()
		videoFile := status.VideoFile
		audioFile := status.AudioFile
		webcamFile := status.WebcamFile

		// Step 0: Stop recorders
		if err := m.recorder.Stop(); err != nil {
			return processingErrorMsg{Error: err}
		}

		// The recorder.Stop() already handles merging in background via processRecordings()
		// We'll simulate the steps for now - in a full implementation, the recorder
		// would send progress updates via channels

		// For now, we'll just signal completion after a brief delay
		// The actual processing happens in recorder.processRecordings()
		_ = videoFile
		_ = audioFile
		_ = webcamFile

		return processingCompleteMsg{}
	}
}

// Run starts the TUI application with optional splash screens
func Run(noSplash bool, presetsMode bool, editRecordingMode bool) error {
	// Check for required dependencies before starting
	missing := deps.MissingRequired()
	if len(missing) > 0 {
		// Show dependency error screen instead of main app
		return showDependencyError(missing, noSplash)
	}

	// Skip splashes for special modes
	skipSplash := noSplash || presetsMode || editRecordingMode

	// Show entry splash screen (3 seconds, skippable with any key)
	if !skipSplash {
		if err := ShowSplashScreen(3 * time.Second); err != nil {
			// Ignore splash errors, continue to main app
			_ = err
		}
	}

	// Run main application with appropriate model
	var model tea.Model
	if presetsMode {
		model = NewAppModelWithPresets()
	} else if editRecordingMode {
		model = NewAppModelWithEditRecording()
	} else {
		model = NewAppModel()
	}
	p := tea.NewProgram(model, tea.WithAltScreen())
	_, err := p.Run()

	// Show exit splash screen (2 seconds, skippable with any key)
	if !skipSplash {
		if exitErr := ShowExitSplashScreen(2 * time.Second); exitErr != nil {
			// Ignore splash errors
			_ = exitErr
		}
	}

	return err
}

// showDependencyError displays missing dependencies and exits
func showDependencyError(missing []deps.CheckResult, noSplash bool) error {
	// Build error message
	var sb strings.Builder
	sb.WriteString("\n")
	sb.WriteString(lipgloss.NewStyle().
		Bold(true).
		Foreground(ColorRed).
		Render("Missing Required Dependencies"))
	sb.WriteString("\n\n")
	sb.WriteString("The following required programs are not installed:\n\n")

	for _, m := range missing {
		sb.WriteString(fmt.Sprintf("  %s %s\n",
			lipgloss.NewStyle().Foreground(ColorRed).Render("✗"),
			lipgloss.NewStyle().Bold(true).Render(m.Dependency.Name)))
		sb.WriteString(fmt.Sprintf("    %s\n\n",
			lipgloss.NewStyle().Foreground(ColorGray).Render(m.Dependency.Description)))
	}

	sb.WriteString(lipgloss.NewStyle().Foreground(ColorGray).Render("Please install the missing dependencies and try again."))
	sb.WriteString("\n\n")

	// Installation hints based on what's missing
	sb.WriteString(lipgloss.NewStyle().Bold(true).Render("Installation hints:"))
	sb.WriteString("\n")
	for _, m := range missing {
		hint := getInstallHint(m.Dependency.Name)
		if hint != "" {
			sb.WriteString(fmt.Sprintf("  %s: %s\n", m.Dependency.Name, hint))
		}
	}
	sb.WriteString("\n")

	fmt.Println(sb.String())
	return fmt.Errorf("missing required dependencies")
}

// getInstallHint returns installation hints for common package managers
func getInstallHint(name string) string {
	hints := map[string]string{
		"ffmpeg":       "apt install ffmpeg / pacman -S ffmpeg / nix-shell -p ffmpeg",
		"ffprobe":      "(included with ffmpeg)",
		"wl-screenrec": "cargo install wl-screenrec / nix-shell -p wl-screenrec",
		"pw-record":    "apt install pipewire / pacman -S pipewire / nix-shell -p pipewire",
	}
	if hint, ok := hints[name]; ok {
		return hint
	}
	return ""
}
