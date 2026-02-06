package tui

import (
	"fmt"
	"os"
	"os/exec"
	"runtime"
	"strings"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
	"github.com/kartoza/kartoza-screencaster/internal/models"
)

// ProcessingStep represents a single processing step
type ProcessingStep struct {
	Name      string
	Status    StepStatus
	StartTime time.Time
	EndTime   time.Time
	Progress  float64 // Progress percentage (0-100), -1 means indeterminate
}

// StepStatus represents the status of a processing step
type StepStatus int

const (
	StepPending StepStatus = iota
	StepRunning
	StepComplete
	StepFailed
	StepSkipped
)

// ProcessingState holds the state of all processing steps
type ProcessingState struct {
	Steps        []ProcessingStep
	CurrentStep  int
	IsProcessing bool
	StartTime    time.Time
	EndTime      time.Time
	Error        error
}

// Processing step indices (must match order in NewProcessingState)
const (
	ProcessStepStopping = iota
	ProcessStepAnalyzing
	ProcessStepNormalizing
	ProcessStepMerging
	ProcessStepVertical
)

// NewProcessingState creates a new processing state with default steps
func NewProcessingState() *ProcessingState {
	return &ProcessingState{
		Steps: []ProcessingStep{
			{Name: "Stopping recorders", Status: StepPending},
			{Name: "Analyzing audio levels", Status: StepPending},
			{Name: "Normalizing audio", Status: StepPending},
			{Name: "Merging video & audio", Status: StepPending},
			{Name: "Creating vertical video", Status: StepPending},
		},
		CurrentStep:  -1,
		IsProcessing: false,
	}
}

// ConfigureSteps marks steps as skipped based on recording settings
func (p *ProcessingState) ConfigureSteps(hasAudio, hasScreen, hasWebcam, createVertical bool) {
	// Audio steps skipped if no audio
	if !hasAudio {
		p.Steps[ProcessStepAnalyzing].Status = StepSkipped
		p.Steps[ProcessStepNormalizing].Status = StepSkipped
	}

	// Merging step skipped if only one source or no video sources
	if !hasScreen && !hasWebcam {
		p.Steps[ProcessStepMerging].Status = StepSkipped
	}

	// Vertical video step skipped if not creating vertical video
	if !createVertical {
		p.Steps[ProcessStepVertical].Status = StepSkipped
	}
}

// SetStepByIndex directly sets a step's status by index
func (p *ProcessingState) SetStepByIndex(index int, status StepStatus) {
	if index >= 0 && index < len(p.Steps) {
		switch status {
		case StepRunning:
			p.Steps[index].StartTime = time.Now()
			p.Steps[index].Progress = -1 // Indeterminate by default
			p.CurrentStep = index
		case StepComplete, StepSkipped, StepFailed:
			p.Steps[index].EndTime = time.Now()
			p.Steps[index].Progress = 100 // Mark as complete
		}
		p.Steps[index].Status = status
	}
}

// SetStepProgress updates the progress percentage for a step
func (p *ProcessingState) SetStepProgress(index int, progress float64) {
	if index >= 0 && index < len(p.Steps) {
		p.Steps[index].Progress = progress
	}
}

// Start begins the processing
func (p *ProcessingState) Start() {
	p.IsProcessing = true
	p.StartTime = time.Now()
	p.CurrentStep = 0

	// Find first non-skipped step
	for p.CurrentStep < len(p.Steps) && p.Steps[p.CurrentStep].Status == StepSkipped {
		p.CurrentStep++
	}

	if p.CurrentStep < len(p.Steps) {
		p.Steps[p.CurrentStep].Status = StepRunning
		p.Steps[p.CurrentStep].StartTime = time.Now()
	}
}

// NextStep advances to the next step
func (p *ProcessingState) NextStep() {
	if p.CurrentStep >= 0 && p.CurrentStep < len(p.Steps) {
		p.Steps[p.CurrentStep].Status = StepComplete
		p.Steps[p.CurrentStep].EndTime = time.Now()
	}
	p.CurrentStep++

	// Skip any already-skipped steps
	for p.CurrentStep < len(p.Steps) && p.Steps[p.CurrentStep].Status == StepSkipped {
		p.CurrentStep++
	}

	if p.CurrentStep < len(p.Steps) {
		p.Steps[p.CurrentStep].Status = StepRunning
		p.Steps[p.CurrentStep].StartTime = time.Now()
	}
}

// SkipStep marks current step as skipped and advances
func (p *ProcessingState) SkipStep() {
	if p.CurrentStep >= 0 && p.CurrentStep < len(p.Steps) {
		p.Steps[p.CurrentStep].Status = StepSkipped
		p.Steps[p.CurrentStep].EndTime = time.Now()
	}
	p.CurrentStep++
	if p.CurrentStep < len(p.Steps) {
		p.Steps[p.CurrentStep].Status = StepRunning
		p.Steps[p.CurrentStep].StartTime = time.Now()
	}
}

// FailStep marks current step as failed
func (p *ProcessingState) FailStep(err error) {
	if p.CurrentStep >= 0 && p.CurrentStep < len(p.Steps) {
		p.Steps[p.CurrentStep].Status = StepFailed
		p.Steps[p.CurrentStep].EndTime = time.Now()
	}
	p.EndTime = time.Now()
	p.Error = err
}

// Complete marks processing as complete
func (p *ProcessingState) Complete() {
	if p.CurrentStep >= 0 && p.CurrentStep < len(p.Steps) {
		p.Steps[p.CurrentStep].Status = StepComplete
		p.Steps[p.CurrentStep].EndTime = time.Now()
	}
	p.EndTime = time.Now()
	p.IsProcessing = false
}

// Reset resets the processing state
func (p *ProcessingState) Reset() {
	for i := range p.Steps {
		p.Steps[i].Status = StepPending
		p.Steps[i].StartTime = time.Time{}
		p.Steps[i].EndTime = time.Time{}
	}
	p.CurrentStep = -1
	p.IsProcessing = false
	p.StartTime = time.Time{}
	p.EndTime = time.Time{}
	p.Error = nil
}

// Messages for processing updates
type processingTickMsg struct{}
type processingStepMsg struct {
	Step      int
	Completed bool
	Skipped   bool
	Error     error
}
type processingPercentMsg struct {
	Step    int
	Percent float64
}
type processingCompleteMsg struct{}
type processingErrorMsg struct {
	Error error
}

// processingTickCmd returns a command that ticks the processing animation
func processingTickCmd() tea.Cmd {
	return tea.Tick(100*time.Millisecond, func(t time.Time) tea.Msg {
		return processingTickMsg{}
	})
}

// Donut animation frames (Unicode block characters for spinning effect)
var donutFrames = []string{
	"◐", "◓", "◑", "◒",
}

// ProcessingButton represents a button option on the processing complete screen
type ProcessingButton int

const (
	ProcessingButtonUpload ProcessingButton = iota
	ProcessingButtonMenu
)

// RenderProcessingView renders the processing screen with donut indicators
// Uses standard header/footer layout. When complete, footer shows media preview shortcuts.
func RenderProcessingView(state *ProcessingState, width, height int, frame int, selectedButton ProcessingButton, youtubeConnected bool, recordingInfo *models.RecordingInfo) string {
	if state == nil {
		return ""
	}

	// Update global app state to show Processing status
	GlobalAppState.IsRecording = false
	GlobalAppState.Status = "Processing"

	// Standard header
	header := RenderHeader("Processing")

	// Title
	titleStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(ColorOrange).
		MarginBottom(1)

	title := titleStyle.Render("Processing Recording...")

	// Elapsed time — freeze when processing completes or fails
	var elapsed time.Duration
	if !state.IsProcessing && !state.EndTime.IsZero() {
		elapsed = state.EndTime.Sub(state.StartTime).Round(time.Second)
	} else {
		elapsed = time.Since(state.StartTime).Round(time.Second)
	}
	timeStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)
	elapsedStr := timeStyle.Render(fmt.Sprintf("Elapsed: %s", elapsed))

	// Build step list
	var steps []string
	for i, step := range state.Steps {
		line := renderStepLine(step, i == state.CurrentStep, frame)
		steps = append(steps, line)
	}
	stepsContent := strings.Join(steps, "\n")

	// Status message
	var statusMsg string
	statusStyle := lipgloss.NewStyle().
		MarginTop(1).
		Foreground(ColorGray)

	if state.Error != nil {
		statusStyle = statusStyle.Foreground(ColorRed)
		statusMsg = statusStyle.Render(fmt.Sprintf("Error: %v", state.Error))
	} else if !state.IsProcessing {
		statusStyle = statusStyle.Foreground(ColorGreen)
		statusMsg = statusStyle.Render("Processing complete!")
	} else {
		statusMsg = statusStyle.Render("Please wait...")
	}

	// Buttons (only shown when processing is complete and no error)
	var buttonsRow string
	if !state.IsProcessing && state.Error == nil {
		buttonStyle := lipgloss.NewStyle().
			Padding(0, 2).
			Bold(true)

		activeButtonStyle := buttonStyle.
			Background(ColorOrange).
			Foreground(lipgloss.Color("#000000"))

		inactiveButtonStyle := buttonStyle.
			Background(ColorGray).
			Foreground(ColorWhite)

		// Upload button (only if YouTube is connected)
		var uploadBtn string
		if youtubeConnected {
			if selectedButton == ProcessingButtonUpload {
				uploadBtn = activeButtonStyle.Render("Upload to YouTube")
			} else {
				uploadBtn = inactiveButtonStyle.Render("Upload to YouTube")
			}
		}

		// Menu button
		var menuBtn string
		if selectedButton == ProcessingButtonMenu {
			menuBtn = activeButtonStyle.Render("Return to Menu")
		} else {
			menuBtn = inactiveButtonStyle.Render("Return to Menu")
		}

		if youtubeConnected {
			buttonsRow = lipgloss.JoinHorizontal(lipgloss.Center, uploadBtn, "  ", menuBtn)
		} else {
			buttonsRow = menuBtn
		}
	}

	// Combine content elements
	content := lipgloss.JoinVertical(
		lipgloss.Center,
		title,
		elapsedStr,
		"",
		stepsContent,
		"",
		statusMsg,
		"",
		buttonsRow,
	)

	// Build footer help text
	var helpText string
	if !state.IsProcessing && state.Error == nil {
		// Processing complete - show media shortcuts and button navigation
		helpText = buildProcessingCompleteFooter(recordingInfo)
	} else if state.Error != nil {
		helpText = "q: quit"
	} else {
		helpText = "Please wait..."
	}
	footer := RenderHelpFooter(helpText, width)

	return LayoutWithHeaderFooter(header, content, footer, width, height)
}

// buildProcessingCompleteFooter builds the footer help text for the processing complete screen
func buildProcessingCompleteFooter(info *models.RecordingInfo) string {
	var parts []string

	if info != nil {
		hasVertical := info.Files.VerticalFile != ""
		hasMerged := info.Files.MergedFile != ""
		hasAudio := info.Files.AudioFile != ""
		hasFolder := info.Files.FolderPath != ""

		if hasVertical {
			parts = append(parts, "v: vertical")
		}
		if hasMerged {
			parts = append(parts, "m: merged")
		}
		if hasAudio {
			parts = append(parts, "a: audio")
		}
		if hasFolder {
			parts = append(parts, "o: folder")
		}
	}

	parts = append(parts, "←/→: select", "enter: confirm", "q: quit")
	return strings.Join(parts, " • ")
}

// Progress bar characters
const (
	progressBarWidth = 20
	progressFilled   = "█"
	progressEmpty    = "░"
)

// renderProgressBar renders a progress bar for a given percentage
func renderProgressBar(progress float64, width int) string {
	if progress < 0 {
		return "" // Indeterminate, don't show bar
	}
	if progress > 100 {
		progress = 100
	}

	filled := int(progress / 100 * float64(width))
	empty := width - filled

	filledStyle := lipgloss.NewStyle().Foreground(ColorOrange)
	emptyStyle := lipgloss.NewStyle().Foreground(ColorGray)

	bar := filledStyle.Render(strings.Repeat(progressFilled, filled)) +
		emptyStyle.Render(strings.Repeat(progressEmpty, empty))

	percentStyle := lipgloss.NewStyle().Foreground(ColorWhite).Width(4)
	return fmt.Sprintf(" %s %s", bar, percentStyle.Render(fmt.Sprintf("%3.0f%%", progress)))
}

// renderStepLine renders a single processing step with appropriate indicator
func renderStepLine(step ProcessingStep, isCurrent bool, frame int) string {
	var indicator string
	var nameStyle lipgloss.Style

	switch step.Status {
	case StepPending:
		indicator = lipgloss.NewStyle().Foreground(ColorGray).Render("○")
		nameStyle = lipgloss.NewStyle().Foreground(ColorGray)

	case StepRunning:
		// Animated donut
		donutStyle := lipgloss.NewStyle().Foreground(ColorOrange).Bold(true)
		indicator = donutStyle.Render(donutFrames[frame%len(donutFrames)])
		nameStyle = lipgloss.NewStyle().Foreground(ColorWhite).Bold(true)

	case StepComplete:
		indicator = lipgloss.NewStyle().Foreground(ColorGreen).Render("●")
		nameStyle = lipgloss.NewStyle().Foreground(ColorGreen)

	case StepFailed:
		indicator = lipgloss.NewStyle().Foreground(ColorRed).Render("✗")
		nameStyle = lipgloss.NewStyle().Foreground(ColorRed)

	case StepSkipped:
		indicator = lipgloss.NewStyle().Foreground(ColorGray).Render("–")
		nameStyle = lipgloss.NewStyle().Foreground(ColorGray)
	}

	// Progress bar or duration or skipped indicator
	var suffix string
	if step.Status == StepRunning && step.Progress >= 0 {
		// Show progress bar for running steps with known progress
		suffix = renderProgressBar(step.Progress, progressBarWidth)
	} else if step.Status == StepComplete || step.Status == StepFailed {
		// Duration for completed steps (only show if StartTime was set)
		if !step.StartTime.IsZero() {
			d := step.EndTime.Sub(step.StartTime).Round(100 * time.Millisecond)
			durationStyle := lipgloss.NewStyle().Foreground(ColorGray).Italic(true)
			suffix = durationStyle.Render(fmt.Sprintf(" (%s)", d))
		}
	} else if step.Status == StepSkipped {
		// Show "skipped" for skipped steps
		skippedStyle := lipgloss.NewStyle().Foreground(ColorGray).Italic(true)
		suffix = skippedStyle.Render(" (skipped)")
	}

	return fmt.Sprintf("  %s %s%s", indicator, nameStyle.Render(step.Name), suffix)
}

// openFileCmd opens a file with the system default application
func openFileCmd(filePath string) tea.Cmd {
	return func() tea.Msg {
		cmd := exec.Command("xdg-open", filePath)
		_ = cmd.Start()
		return nil
	}
}

// openFolderCmd opens a folder in the system file manager
func openFolderCmd(folderPath string) tea.Cmd {
	return func() tea.Msg {
		var cmd *exec.Cmd
		switch runtime.GOOS {
		case "darwin":
			cmd = exec.Command("open", folderPath)
		case "windows":
			cmd = exec.Command("explorer", folderPath)
		default:
			cmd = exec.Command("xdg-open", folderPath)
		}
		_ = cmd.Start()
		return nil
	}
}

// HandleProcessingMediaKey handles v/m/a/o key presses on the processing complete screen.
// Returns a tea.Cmd if the key was handled, nil otherwise.
func HandleProcessingMediaKey(key string, info *models.RecordingInfo) tea.Cmd {
	if info == nil {
		return nil
	}
	switch key {
	case "v":
		if info.Files.VerticalFile != "" {
			return openFileCmd(info.Files.VerticalFile)
		}
		// Fall back to merged if no vertical
		if info.Files.MergedFile != "" {
			return openFileCmd(info.Files.MergedFile)
		}
	case "m":
		if info.Files.MergedFile != "" {
			return openFileCmd(info.Files.MergedFile)
		}
	case "a":
		if info.Files.AudioFile != "" {
			// Try normalized audio first
			normalizedPath := strings.TrimSuffix(info.Files.AudioFile, ".wav") + "-normalized.wav"
			if _, err := os.Stat(normalizedPath); err == nil {
				return openFileCmd(normalizedPath)
			}
			return openFileCmd(info.Files.AudioFile)
		}
	case "o":
		if info.Files.FolderPath != "" {
			return openFolderCmd(info.Files.FolderPath)
		}
	}
	return nil
}
