package tui

import (
	"fmt"
	"strings"

	"github.com/charmbracelet/bubbles/textarea"
	"github.com/charmbracelet/bubbles/textinput"
	"github.com/charmbracelet/bubbles/viewport"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
	"github.com/kartoza/kartoza-screencaster/internal/config"
	"github.com/kartoza/kartoza-screencaster/internal/models"
	"github.com/kartoza/kartoza-screencaster/internal/spellcheck"
)

// RecordingFormMode indicates whether the form is for new recording or editing existing
type RecordingFormMode int

const (
	FormModeNewRecording RecordingFormMode = iota
	FormModeEditExisting
)

// RecordingFormField represents which field is focused
type RecordingFormField int

const (
	FormFieldTitle RecordingFormField = iota
	FormFieldNumber
	FormFieldTopic
	FormFieldRecordAudio
	FormFieldRecordWebcam
	FormFieldRecordScreen
	FormFieldMonitor
	FormFieldVerticalVideo
	FormFieldLeftSplit
	FormFieldAddLogos
	FormFieldLeftLogo
	FormFieldRightLogo
	FormFieldBottomLogo
	FormFieldTitleColor
	FormFieldGifLoopMode
	FormFieldPresenter
	FormFieldDescription
	FormFieldConfirm
)

// RecordingFormConfig holds configuration for the form
type RecordingFormConfig struct {
	Mode RecordingFormMode

	// Read-only info (for edit mode)
	FolderName string
	Date       string
	Duration   string

	// Available options
	Topics   []models.Topic
	Monitors []models.Monitor
	Logos    []string

	// Callbacks
	OnConfirm func()
	OnCancel  func()
}

// RecordingFormState holds the current state/values of the form
type RecordingFormState struct {
	// Text inputs
	TitleInput     textinput.Model
	NumberInput    textinput.Model
	PresenterInput textinput.Model
	DescInput      textarea.Model

	// Selections
	SelectedTopic   int
	SelectedMonitor int

	// Toggles (new recording only)
	RecordAudio   bool
	RecordWebcam  bool
	RecordScreen  bool
	VerticalVideo bool
	LeftSplit     bool // Use left half of screen for vertical video
	AddLogos      bool

	// Logo selection
	SelectedLeftIdx    int
	SelectedRightIdx   int
	SelectedBottomIdx  int
	SelectedColorIdx   int
	SelectedGifLoopIdx int

	// Focus state
	FocusedField RecordingFormField
	InputMode    bool // When true, text input captures all keys

	// Confirm button state
	ConfirmSelected bool // true = confirm, false = cancel

	// Spell checking
	SpellChecker *spellcheck.SpellChecker
	TitleIssues  []spellcheck.Issue
	DescIssues   []spellcheck.Issue

	// Status messages
	ErrorMsg   string
	SuccessMsg string
	IsSaving   bool
}

// NewRecordingFormState creates a new form state with default values
func NewRecordingFormState(mode RecordingFormMode) *RecordingFormState {
	cfg, _ := config.Load()

	// Title input
	titleInput := textinput.New()
	titleInput.Placeholder = "Enter recording title..."
	titleInput.CharLimit = 100
	titleInput.Width = 40

	// Number input (for new recordings)
	numberInput := textinput.New()
	numberInput.Placeholder = "001"
	numberInput.CharLimit = 10
	numberInput.Width = 30
	if mode == FormModeNewRecording {
		recordingNumber := config.GetCurrentRecordingNumber()
		numberInput.SetValue(fmt.Sprintf("%03d", recordingNumber))
	}

	// Presenter input
	presenterInput := textinput.New()
	presenterInput.Placeholder = "Presenter name..."
	presenterInput.CharLimit = 100
	presenterInput.Width = 40
	if cfg.DefaultPresenter != "" {
		presenterInput.SetValue(cfg.DefaultPresenter)
	}

	// Description input
	descInput := textarea.New()
	descInput.Placeholder = "Enter description..."
	descInput.CharLimit = 2000
	descInput.SetWidth(58)
	descInput.SetHeight(4)
	descInput.ShowLineNumbers = false

	// Get recording presets
	presets := cfg.RecordingPresets
	presetsExist := presets.RecordAudio || presets.RecordWebcam ||
		presets.RecordScreen || presets.VerticalVideo || presets.AddLogos
	if !presetsExist {
		presets = config.DefaultRecordingPresets()
	}

	state := &RecordingFormState{
		TitleInput:      titleInput,
		NumberInput:     numberInput,
		PresenterInput:  presenterInput,
		DescInput:       descInput,
		FocusedField:    FormFieldTitle,
		ConfirmSelected: true,
		SpellChecker:    spellcheck.NewSpellChecker(),
	}

	if mode == FormModeNewRecording {
		state.RecordAudio = presets.RecordAudio
		state.RecordWebcam = presets.RecordWebcam
		state.RecordScreen = presets.RecordScreen
		state.VerticalVideo = presets.VerticalVideo
		state.LeftSplit = presets.LeftSplit
		state.AddLogos = presets.AddLogos
	}

	return state
}

// RecordingForm is the shared form component
type RecordingForm struct {
	Config   *RecordingFormConfig
	State    *RecordingFormState
	viewport viewport.Model
	width    int
	height   int
	ready    bool // viewport initialized

	// Track line positions for auto-scroll
	fieldLinePositions map[RecordingFormField]int
}

// NewRecordingForm creates a new recording form
func NewRecordingForm(cfg *RecordingFormConfig) *RecordingForm {
	vp := viewport.New(70, 20) // Default size, will be updated by SetSize
	vp.Style = lipgloss.NewStyle()

	return &RecordingForm{
		Config:             cfg,
		State:              NewRecordingFormState(cfg.Mode),
		viewport:           vp,
		fieldLinePositions: make(map[RecordingFormField]int),
	}
}

// SetSize updates the form dimensions and viewport
func (f *RecordingForm) SetSize(width, height int) {
	f.width = width
	f.height = height

	// Calculate viewport height (leave room for scroll indicators)
	viewportHeight := height
	if viewportHeight < 10 {
		viewportHeight = 10
	}

	f.viewport.Width = f.containerWidth()
	f.viewport.Height = viewportHeight
	f.ready = true

	// Update text input widths to be responsive
	inputWidth := f.internalWidth() - 20 // Leave room for labels
	if inputWidth < 30 {
		inputWidth = 30
	}
	if inputWidth > 80 {
		inputWidth = 80
	}
	f.State.TitleInput.Width = inputWidth
	f.State.NumberInput.Width = inputWidth / 2
	f.State.PresenterInput.Width = inputWidth
	f.State.DescInput.SetWidth(inputWidth)
}

// containerWidth returns the calculated container width based on terminal width
func (f *RecordingForm) containerWidth() int {
	containerWidth := f.width - 4
	// Allow wider forms on larger terminals (max 120 for readability)
	if containerWidth > 120 {
		containerWidth = 120
	}
	if containerWidth < 60 {
		containerWidth = 60
	}
	return containerWidth
}

// internalWidth returns the internal content width (container minus padding/borders)
func (f *RecordingForm) internalWidth() int {
	iw := f.containerWidth() - 8
	if iw < 50 {
		iw = 50
	}
	return iw
}

// Focus focuses the title input
func (f *RecordingForm) Focus() {
	f.State.TitleInput.Focus()
	f.State.FocusedField = FormFieldTitle
}

// Blur removes focus from all inputs
func (f *RecordingForm) Blur() {
	f.State.TitleInput.Blur()
	f.State.NumberInput.Blur()
	f.State.PresenterInput.Blur()
	f.State.DescInput.Blur()
	f.State.InputMode = false
}

// Update handles input for the form
func (f *RecordingForm) Update(msg tea.Msg) (*RecordingForm, tea.Cmd) {
	var cmd tea.Cmd
	var cmds []tea.Cmd

	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		// Handle window resize - update form dimensions
		contentHeight := msg.Height - 8
		if contentHeight < 10 {
			contentHeight = 10
		}
		f.SetSize(msg.Width, contentHeight)
		return f, nil

	case tea.KeyMsg:
		// Handle input mode (when typing in a text field)
		if f.State.InputMode {
			switch msg.String() {
			case "tab", "enter":
				if f.State.FocusedField == FormFieldDescription {
					// Allow enter in description for newlines
					if msg.String() == "tab" {
						f.State.InputMode = false
						f.State.DescInput.Blur()
						f.nextField()
						f.scrollToFocusedField()
					} else {
						f.State.DescInput, cmd = f.State.DescInput.Update(msg)
						f.State.DescIssues = f.State.SpellChecker.Check(f.State.DescInput.Value())
					}
				} else {
					f.State.InputMode = false
					f.blurCurrentInput()
					f.nextField()
					f.scrollToFocusedField()
				}
				return f, cmd
			case "esc":
				f.State.InputMode = false
				f.blurCurrentInput()
				return f, nil
			default:
				// Pass to focused input
				cmd = f.updateFocusedInput(msg)
				return f, cmd
			}
		}

		// Normal mode navigation
		switch msg.String() {
		case "tab", "down", "j":
			f.nextField()
			f.scrollToFocusedField()
		case "shift+tab", "up", "k":
			f.prevField()
			f.scrollToFocusedField()
		case "left", "h":
			f.handleLeftRight(-1)
		case "right", "l":
			f.handleLeftRight(1)
		case "enter", " ":
			return f.handleEnter()
		case "esc":
			if f.Config.OnCancel != nil {
				f.Config.OnCancel()
			}
		case "ctrl+s":
			if f.Config.Mode == FormModeEditExisting && f.Config.OnConfirm != nil {
				f.Config.OnConfirm()
			}
		case "pgup", "ctrl+u":
			f.viewport.ViewUp()
		case "pgdown", "ctrl+d":
			f.viewport.ViewDown()
		}

	case tea.MouseMsg:
		// Handle mouse wheel scrolling
		f.viewport, cmd = f.viewport.Update(msg)
		cmds = append(cmds, cmd)
	}

	if len(cmds) > 0 {
		return f, tea.Batch(cmds...)
	}
	return f, cmd
}

// scrollToFocusedField scrolls the viewport to ensure the focused field is visible
func (f *RecordingForm) scrollToFocusedField() {
	if !f.ready {
		return
	}

	linePos, ok := f.fieldLinePositions[f.State.FocusedField]
	if !ok {
		return
	}

	// Add some padding so the field isn't right at the edge
	padding := 2
	viewTop := f.viewport.YOffset
	viewBottom := viewTop + f.viewport.Height

	// If field is above visible area, scroll up
	if linePos < viewTop+padding {
		f.viewport.SetYOffset(linePos - padding)
	}
	// If field is below visible area, scroll down
	if linePos > viewBottom-padding-3 { // -3 for field height
		f.viewport.SetYOffset(linePos - f.viewport.Height + padding + 5)
	}
}

func (f *RecordingForm) updateFocusedInput(msg tea.Msg) tea.Cmd {
	var cmd tea.Cmd
	switch f.State.FocusedField {
	case FormFieldTitle:
		f.State.TitleInput, cmd = f.State.TitleInput.Update(msg)
		f.State.TitleIssues = f.State.SpellChecker.Check(f.State.TitleInput.Value())
	case FormFieldNumber:
		f.State.NumberInput, cmd = f.State.NumberInput.Update(msg)
	case FormFieldPresenter:
		f.State.PresenterInput, cmd = f.State.PresenterInput.Update(msg)
	case FormFieldDescription:
		f.State.DescInput, cmd = f.State.DescInput.Update(msg)
		f.State.DescIssues = f.State.SpellChecker.Check(f.State.DescInput.Value())
	}
	return cmd
}

func (f *RecordingForm) blurCurrentInput() {
	switch f.State.FocusedField {
	case FormFieldTitle:
		f.State.TitleInput.Blur()
	case FormFieldNumber:
		f.State.NumberInput.Blur()
	case FormFieldPresenter:
		f.State.PresenterInput.Blur()
	case FormFieldDescription:
		f.State.DescInput.Blur()
	}
}

func (f *RecordingForm) nextField() {
	if f.Config.Mode == FormModeEditExisting {
		f.nextFieldEditMode()
	} else {
		f.nextFieldNewMode()
	}
}

func (f *RecordingForm) nextFieldEditMode() {
	// Edit mode uses same field order as new mode, just without Number and Confirm
	for {
		switch f.State.FocusedField {
		case FormFieldTitle:
			f.State.FocusedField = FormFieldTopic
		case FormFieldTopic:
			f.State.FocusedField = FormFieldPresenter
		case FormFieldPresenter:
			f.State.FocusedField = FormFieldRecordAudio
		case FormFieldRecordAudio:
			f.State.FocusedField = FormFieldRecordWebcam
		case FormFieldRecordWebcam:
			f.State.FocusedField = FormFieldRecordScreen
		case FormFieldRecordScreen:
			if f.State.RecordScreen && len(f.Config.Monitors) > 0 {
				f.State.FocusedField = FormFieldMonitor
			} else {
				f.State.FocusedField = FormFieldVerticalVideo
			}
		case FormFieldMonitor:
			f.State.FocusedField = FormFieldVerticalVideo
		case FormFieldVerticalVideo:
			if f.State.VerticalVideo {
				f.State.FocusedField = FormFieldLeftSplit
			} else {
				f.State.FocusedField = FormFieldAddLogos
			}
		case FormFieldLeftSplit:
			f.State.FocusedField = FormFieldAddLogos
		case FormFieldAddLogos:
			if f.State.AddLogos {
				f.State.FocusedField = FormFieldLeftLogo
			} else {
				f.State.FocusedField = FormFieldDescription
			}
		case FormFieldLeftLogo:
			f.State.FocusedField = FormFieldRightLogo
		case FormFieldRightLogo:
			f.State.FocusedField = FormFieldBottomLogo
		case FormFieldBottomLogo:
			f.State.FocusedField = FormFieldTitleColor
		case FormFieldTitleColor:
			if f.isBottomLogoGif() {
				f.State.FocusedField = FormFieldGifLoopMode
			} else {
				f.State.FocusedField = FormFieldDescription
			}
		case FormFieldGifLoopMode:
			f.State.FocusedField = FormFieldDescription
		case FormFieldDescription:
			f.State.FocusedField = FormFieldTitle
		default:
			f.State.FocusedField = FormFieldTitle
		}

		if !f.shouldSkipField(f.State.FocusedField) {
			break
		}
	}
}

func (f *RecordingForm) nextFieldNewMode() {
	// New recording mode has more fields
	for {
		switch f.State.FocusedField {
		case FormFieldTitle:
			f.State.FocusedField = FormFieldNumber
		case FormFieldNumber:
			f.State.FocusedField = FormFieldTopic
		case FormFieldTopic:
			f.State.FocusedField = FormFieldPresenter
		case FormFieldPresenter:
			f.State.FocusedField = FormFieldRecordAudio
		case FormFieldRecordAudio:
			f.State.FocusedField = FormFieldRecordWebcam
		case FormFieldRecordWebcam:
			f.State.FocusedField = FormFieldRecordScreen
		case FormFieldRecordScreen:
			if f.State.RecordScreen && len(f.Config.Monitors) > 0 {
				f.State.FocusedField = FormFieldMonitor
			} else {
				f.State.FocusedField = FormFieldVerticalVideo
			}
		case FormFieldMonitor:
			f.State.FocusedField = FormFieldVerticalVideo
		case FormFieldVerticalVideo:
			if f.State.VerticalVideo {
				f.State.FocusedField = FormFieldLeftSplit
			} else {
				f.State.FocusedField = FormFieldAddLogos
			}
		case FormFieldLeftSplit:
			f.State.FocusedField = FormFieldAddLogos
		case FormFieldAddLogos:
			if f.State.AddLogos {
				f.State.FocusedField = FormFieldLeftLogo
			} else {
				f.State.FocusedField = FormFieldDescription
			}
		case FormFieldLeftLogo:
			f.State.FocusedField = FormFieldRightLogo
		case FormFieldRightLogo:
			f.State.FocusedField = FormFieldBottomLogo
		case FormFieldBottomLogo:
			f.State.FocusedField = FormFieldTitleColor
		case FormFieldTitleColor:
			// Check if bottom logo is GIF
			if f.isBottomLogoGif() {
				f.State.FocusedField = FormFieldGifLoopMode
			} else {
				f.State.FocusedField = FormFieldDescription
			}
		case FormFieldGifLoopMode:
			f.State.FocusedField = FormFieldDescription
		case FormFieldDescription:
			f.State.FocusedField = FormFieldConfirm
		case FormFieldConfirm:
			f.State.FocusedField = FormFieldTitle
		default:
			f.State.FocusedField = FormFieldTitle
		}

		// Check if we should skip this field
		if !f.shouldSkipField(f.State.FocusedField) {
			break
		}
	}
}

func (f *RecordingForm) prevField() {
	if f.Config.Mode == FormModeEditExisting {
		f.prevFieldEditMode()
	} else {
		f.prevFieldNewMode()
	}
}

func (f *RecordingForm) prevFieldEditMode() {
	// Edit mode uses same field order as new mode, just without Number and Confirm
	for {
		switch f.State.FocusedField {
		case FormFieldTitle:
			f.State.FocusedField = FormFieldDescription
		case FormFieldTopic:
			f.State.FocusedField = FormFieldTitle
		case FormFieldPresenter:
			f.State.FocusedField = FormFieldTopic
		case FormFieldRecordAudio:
			f.State.FocusedField = FormFieldPresenter
		case FormFieldRecordWebcam:
			f.State.FocusedField = FormFieldRecordAudio
		case FormFieldRecordScreen:
			f.State.FocusedField = FormFieldRecordWebcam
		case FormFieldMonitor:
			f.State.FocusedField = FormFieldRecordScreen
		case FormFieldVerticalVideo:
			if f.State.RecordScreen && len(f.Config.Monitors) > 0 {
				f.State.FocusedField = FormFieldMonitor
			} else {
				f.State.FocusedField = FormFieldRecordScreen
			}
		case FormFieldLeftSplit:
			f.State.FocusedField = FormFieldVerticalVideo
		case FormFieldAddLogos:
			if f.State.VerticalVideo {
				f.State.FocusedField = FormFieldLeftSplit
			} else {
				f.State.FocusedField = FormFieldVerticalVideo
			}
		case FormFieldLeftLogo:
			f.State.FocusedField = FormFieldAddLogos
		case FormFieldRightLogo:
			f.State.FocusedField = FormFieldLeftLogo
		case FormFieldBottomLogo:
			f.State.FocusedField = FormFieldRightLogo
		case FormFieldTitleColor:
			f.State.FocusedField = FormFieldBottomLogo
		case FormFieldGifLoopMode:
			f.State.FocusedField = FormFieldTitleColor
		case FormFieldDescription:
			if f.State.AddLogos {
				if f.isBottomLogoGif() {
					f.State.FocusedField = FormFieldGifLoopMode
				} else {
					f.State.FocusedField = FormFieldTitleColor
				}
			} else {
				f.State.FocusedField = FormFieldAddLogos
			}
		default:
			f.State.FocusedField = FormFieldTitle
		}

		if !f.shouldSkipField(f.State.FocusedField) {
			break
		}
	}
}

func (f *RecordingForm) prevFieldNewMode() {
	for {
		switch f.State.FocusedField {
		case FormFieldTitle:
			f.State.FocusedField = FormFieldConfirm
		case FormFieldNumber:
			f.State.FocusedField = FormFieldTitle
		case FormFieldTopic:
			f.State.FocusedField = FormFieldNumber
		case FormFieldPresenter:
			f.State.FocusedField = FormFieldTopic
		case FormFieldRecordAudio:
			f.State.FocusedField = FormFieldPresenter
		case FormFieldRecordWebcam:
			f.State.FocusedField = FormFieldRecordAudio
		case FormFieldRecordScreen:
			f.State.FocusedField = FormFieldRecordWebcam
		case FormFieldMonitor:
			f.State.FocusedField = FormFieldRecordScreen
		case FormFieldVerticalVideo:
			if f.State.RecordScreen && len(f.Config.Monitors) > 0 {
				f.State.FocusedField = FormFieldMonitor
			} else {
				f.State.FocusedField = FormFieldRecordScreen
			}
		case FormFieldLeftSplit:
			f.State.FocusedField = FormFieldVerticalVideo
		case FormFieldAddLogos:
			if f.State.VerticalVideo {
				f.State.FocusedField = FormFieldLeftSplit
			} else {
				f.State.FocusedField = FormFieldVerticalVideo
			}
		case FormFieldLeftLogo:
			f.State.FocusedField = FormFieldAddLogos
		case FormFieldRightLogo:
			f.State.FocusedField = FormFieldLeftLogo
		case FormFieldBottomLogo:
			f.State.FocusedField = FormFieldRightLogo
		case FormFieldTitleColor:
			f.State.FocusedField = FormFieldBottomLogo
		case FormFieldGifLoopMode:
			f.State.FocusedField = FormFieldTitleColor
		case FormFieldDescription:
			if f.State.AddLogos {
				if f.isBottomLogoGif() {
					f.State.FocusedField = FormFieldGifLoopMode
				} else {
					f.State.FocusedField = FormFieldTitleColor
				}
			} else {
				f.State.FocusedField = FormFieldAddLogos
			}
		case FormFieldConfirm:
			f.State.FocusedField = FormFieldDescription
		default:
			f.State.FocusedField = FormFieldTitle
		}

		if !f.shouldSkipField(f.State.FocusedField) {
			break
		}
	}
}

func (f *RecordingForm) shouldSkipField(field RecordingFormField) bool {
	switch field {
	case FormFieldNumber:
		// Only show number field for new recordings
		return f.Config.Mode == FormModeEditExisting
	case FormFieldMonitor:
		// Only show monitor if recording screen and monitors available
		return !f.State.RecordScreen || len(f.Config.Monitors) == 0
	case FormFieldLeftSplit:
		// Only show left split if vertical video is enabled
		return !f.State.VerticalVideo
	case FormFieldLeftLogo, FormFieldRightLogo, FormFieldBottomLogo, FormFieldTitleColor:
		// Only show logo fields if logos enabled
		return !f.State.AddLogos
	case FormFieldGifLoopMode:
		// Only show GIF loop mode if logos enabled and bottom logo is GIF
		return !f.State.AddLogos || !f.isBottomLogoGif()
	case FormFieldConfirm:
		// Only show confirm button for new recordings
		return f.Config.Mode == FormModeEditExisting
	}
	return false
}

func (f *RecordingForm) handleLeftRight(dir int) {
	switch f.State.FocusedField {
	case FormFieldTopic:
		f.State.SelectedTopic += dir
		if f.State.SelectedTopic < 0 {
			f.State.SelectedTopic = len(f.Config.Topics) - 1
		}
		if f.State.SelectedTopic >= len(f.Config.Topics) {
			f.State.SelectedTopic = 0
		}
	case FormFieldMonitor:
		f.State.SelectedMonitor += dir
		if f.State.SelectedMonitor < 0 {
			f.State.SelectedMonitor = len(f.Config.Monitors) - 1
		}
		if f.State.SelectedMonitor >= len(f.Config.Monitors) {
			f.State.SelectedMonitor = 0
		}
	case FormFieldRecordAudio:
		f.State.RecordAudio = !f.State.RecordAudio
	case FormFieldRecordWebcam:
		f.State.RecordWebcam = !f.State.RecordWebcam
	case FormFieldRecordScreen:
		f.State.RecordScreen = !f.State.RecordScreen
	case FormFieldVerticalVideo:
		if f.canEnableVerticalVideo() {
			f.State.VerticalVideo = !f.State.VerticalVideo
		}
	case FormFieldLeftSplit:
		f.State.LeftSplit = !f.State.LeftSplit
	case FormFieldAddLogos:
		f.State.AddLogos = !f.State.AddLogos
	case FormFieldLeftLogo:
		f.State.SelectedLeftIdx += dir
		if f.State.SelectedLeftIdx < 0 {
			f.State.SelectedLeftIdx = len(f.Config.Logos)
		}
		if f.State.SelectedLeftIdx > len(f.Config.Logos) {
			f.State.SelectedLeftIdx = 0
		}
	case FormFieldRightLogo:
		f.State.SelectedRightIdx += dir
		if f.State.SelectedRightIdx < 0 {
			f.State.SelectedRightIdx = len(f.Config.Logos)
		}
		if f.State.SelectedRightIdx > len(f.Config.Logos) {
			f.State.SelectedRightIdx = 0
		}
	case FormFieldBottomLogo:
		f.State.SelectedBottomIdx += dir
		if f.State.SelectedBottomIdx < 0 {
			f.State.SelectedBottomIdx = len(f.Config.Logos)
		}
		if f.State.SelectedBottomIdx > len(f.Config.Logos) {
			f.State.SelectedBottomIdx = 0
		}
	case FormFieldTitleColor:
		f.State.SelectedColorIdx += dir
		if f.State.SelectedColorIdx < 0 {
			f.State.SelectedColorIdx = len(config.TitleColors) - 1
		}
		if f.State.SelectedColorIdx >= len(config.TitleColors) {
			f.State.SelectedColorIdx = 0
		}
	case FormFieldGifLoopMode:
		f.State.SelectedGifLoopIdx += dir
		if f.State.SelectedGifLoopIdx < 0 {
			f.State.SelectedGifLoopIdx = len(config.GifLoopModes) - 1
		}
		if f.State.SelectedGifLoopIdx >= len(config.GifLoopModes) {
			f.State.SelectedGifLoopIdx = 0
		}
	case FormFieldConfirm:
		f.State.ConfirmSelected = !f.State.ConfirmSelected
	}
}

func (f *RecordingForm) handleEnter() (*RecordingForm, tea.Cmd) {
	switch f.State.FocusedField {
	case FormFieldTitle, FormFieldNumber, FormFieldPresenter:
		f.State.InputMode = true
		f.focusCurrentInput()
		return f, textinput.Blink
	case FormFieldDescription:
		f.State.InputMode = true
		f.State.DescInput.Focus()
		return f, textarea.Blink
	case FormFieldConfirm:
		if f.State.ConfirmSelected {
			if f.Config.OnConfirm != nil {
				f.Config.OnConfirm()
			}
		} else {
			if f.Config.OnCancel != nil {
				f.Config.OnCancel()
			}
		}
	default:
		// For toggles, treat enter as toggle
		f.handleLeftRight(1)
	}
	return f, nil
}

func (f *RecordingForm) focusCurrentInput() {
	switch f.State.FocusedField {
	case FormFieldTitle:
		f.State.TitleInput.Focus()
	case FormFieldNumber:
		f.State.NumberInput.Focus()
	case FormFieldPresenter:
		f.State.PresenterInput.Focus()
	}
}

func (f *RecordingForm) canEnableVerticalVideo() bool {
	return f.State.RecordWebcam || f.State.RecordScreen
}

func (f *RecordingForm) isBottomLogoGif() bool {
	if f.State.SelectedBottomIdx <= 0 || f.State.SelectedBottomIdx > len(f.Config.Logos) {
		return false
	}
	logo := f.Config.Logos[f.State.SelectedBottomIdx-1]
	return strings.HasSuffix(strings.ToLower(logo), ".gif")
}

// View renders the form
func (f *RecordingForm) View() string {
	// Calculate available content height (subtract borders and padding)
	// Border adds 2 lines (top + bottom), padding adds 2 lines (top + bottom)
	borderPadding := 4
	contentHeight := f.height - borderPadding
	if contentHeight < 20 {
		contentHeight = 20 // Minimum height
	}

	// Get responsive widths
	containerWidth := f.containerWidth()
	internalWidth := f.internalWidth()

	// Container style - responsive to terminal size
	containerStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorOrange).
		Padding(1, 3).
		Width(containerWidth).
		Height(contentHeight)

	// Styles
	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Width(16).
		Align(lipgloss.Right)

	focusedLabelStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true).
		Width(16).
		Align(lipgloss.Right)

	dividerStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	// Centered row style for headers and dividers
	centeredRowStyle := lipgloss.NewStyle().Align(lipgloss.Center).Width(internalWidth)

	sectionStyle := lipgloss.NewStyle().
		Foreground(ColorBlue).
		Bold(true)

	infoStyle := lipgloss.NewStyle().
		Foreground(ColorBlue).
		Bold(true)

	warningStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		MarginLeft(18)

	var rows []string

	// For edit mode, show read-only recording info first
	if f.Config.Mode == FormModeEditExisting && (f.Config.FolderName != "" || f.Config.Date != "" || f.Config.Duration != "") {
		// Recording Info section header
		infoHeader := sectionStyle.Render("📋 Recording Info")
		infoRow := centeredRowStyle.Render(infoHeader)
		rows = append(rows, infoRow)
		rows = append(rows, "")

		// Show folder, date, and duration on a single line
		infoLine := fmt.Sprintf("%s  •  %s  •  %s", f.Config.FolderName, f.Config.Date, f.Config.Duration)
		infoLineRow := centeredRowStyle.Render(infoStyle.Render(infoLine))
		rows = append(rows, infoLineRow)

		rows = append(rows, "")
		rows = append(rows, dividerStyle.Render(strings.Repeat("─", internalWidth)))
		rows = append(rows, "")
	}

	// Metadata section header
	metadataHeader := sectionStyle.Render("📝 Metadata")
	metadataRow := centeredRowStyle.Render(metadataHeader)
	rows = append(rows, metadataRow)
	rows = append(rows, "")

	// Title field
	f.fieldLinePositions[FormFieldTitle] = len(rows)
	titleLabel := labelStyle.Render("Title:")
	if f.State.FocusedField == FormFieldTitle {
		titleLabel = focusedLabelStyle.Render("Title:")
		if f.State.InputMode {
			titleLabel = focusedLabelStyle.Render("» Title:")
		}
	}
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		titleLabel,
		"  ",
		f.State.TitleInput.View(),
	))

	// Title spell check warnings
	if len(f.State.TitleIssues) > 0 {
		titleWarning := spellcheck.FormatIssues(f.State.TitleIssues)
		if titleWarning != "" {
			rows = append(rows, warningStyle.Render("⚠ "+titleWarning))
		}
	}

	// Number field (new recording only)
	if f.Config.Mode == FormModeNewRecording {
		f.fieldLinePositions[FormFieldNumber] = len(rows)
		numberLabel := labelStyle.Render("Number:")
		if f.State.FocusedField == FormFieldNumber {
			numberLabel = focusedLabelStyle.Render("Number:")
			if f.State.InputMode {
				numberLabel = focusedLabelStyle.Render("» Number:")
			}
		}
		rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
			numberLabel,
			"  ",
			f.State.NumberInput.View(),
		))
	}

	// Topic selector
	f.fieldLinePositions[FormFieldTopic] = len(rows)
	topicLabel := labelStyle.Render("Topic:")
	if f.State.FocusedField == FormFieldTopic {
		topicLabel = focusedLabelStyle.Render("Topic:")
	}
	var topicOptions []string
	for i, topic := range f.Config.Topics {
		topicStyle := lipgloss.NewStyle().
			Padding(0, 1).
			Margin(0, 1)

		if i == f.State.SelectedTopic {
			if f.State.FocusedField == FormFieldTopic {
				topicStyle = topicStyle.
					Background(ColorOrange).
					Foreground(lipgloss.Color("#000000")).
					Bold(true)
			} else {
				topicStyle = topicStyle.
					Background(ColorGray).
					Foreground(ColorWhite)
			}
		} else {
			topicStyle = topicStyle.
				Foreground(ColorGray)
		}
		topicOptions = append(topicOptions, topicStyle.Render(topic.Name))
	}
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		topicLabel,
		"  ",
		lipgloss.JoinHorizontal(lipgloss.Center, topicOptions...),
	))

	// Presenter field
	f.fieldLinePositions[FormFieldPresenter] = len(rows)
	presenterLabel := labelStyle.Render("Presenter:")
	if f.State.FocusedField == FormFieldPresenter {
		presenterLabel = focusedLabelStyle.Render("Presenter:")
		if f.State.InputMode {
			presenterLabel = focusedLabelStyle.Render("» Presenter:")
		}
	}
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		presenterLabel,
		"  ",
		f.State.PresenterInput.View(),
	))

	// Recording Sources section
	rows = append(rows, "")
	rows = append(rows, dividerStyle.Render(strings.Repeat("─", internalWidth)))
	rows = append(rows, "")

	sourcesHeader := sectionStyle.Render("🎬 Recording Sources")
	sourcesRow := lipgloss.NewStyle().Align(lipgloss.Center).Width(internalWidth).Render(sourcesHeader)
	rows = append(rows, sourcesRow)
	rows = append(rows, "")

	// Audio toggle
	f.fieldLinePositions[FormFieldRecordAudio] = len(rows)
	audioLabel := labelStyle.Render("Record Audio:")
	if f.State.FocusedField == FormFieldRecordAudio {
		audioLabel = focusedLabelStyle.Render("Record Audio:")
	}
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		audioLabel,
		"  ",
		f.renderToggle(f.State.RecordAudio, f.State.FocusedField == FormFieldRecordAudio),
	))

	// Webcam toggle
	f.fieldLinePositions[FormFieldRecordWebcam] = len(rows)
	webcamLabel := labelStyle.Render("Record Webcam:")
	if f.State.FocusedField == FormFieldRecordWebcam {
		webcamLabel = focusedLabelStyle.Render("Record Webcam:")
	}
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		webcamLabel,
		"  ",
		f.renderToggle(f.State.RecordWebcam, f.State.FocusedField == FormFieldRecordWebcam),
	))

	// Screen toggle
	f.fieldLinePositions[FormFieldRecordScreen] = len(rows)
	screenLabel := labelStyle.Render("Record Screen:")
	if f.State.FocusedField == FormFieldRecordScreen {
		screenLabel = focusedLabelStyle.Render("Record Screen:")
	}
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		screenLabel,
		"  ",
		f.renderToggle(f.State.RecordScreen, f.State.FocusedField == FormFieldRecordScreen),
	))

	// Monitor selector
	if f.State.RecordScreen && len(f.Config.Monitors) > 0 {
		f.fieldLinePositions[FormFieldMonitor] = len(rows)
		monitorLabel := labelStyle.Render("Monitor:")
		if f.State.FocusedField == FormFieldMonitor {
			monitorLabel = focusedLabelStyle.Render("Monitor:")
		}
		rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
			monitorLabel,
			"  ",
			f.renderMonitorSelector(),
		))
	}

	// Output Options section
	rows = append(rows, "")
	rows = append(rows, dividerStyle.Render(strings.Repeat("─", internalWidth)))
	rows = append(rows, "")

	outputHeader := sectionStyle.Render("📤 Output Options")
	outputRow := lipgloss.NewStyle().Align(lipgloss.Center).Width(internalWidth).Render(outputHeader)
	rows = append(rows, outputRow)
	rows = append(rows, "")

	// Vertical Video toggle
	f.fieldLinePositions[FormFieldVerticalVideo] = len(rows)
	verticalLabel := labelStyle.Render("Vertical Video:")
	if f.State.FocusedField == FormFieldVerticalVideo {
		verticalLabel = focusedLabelStyle.Render("Vertical Video:")
	}
	verticalDisabled := !f.canEnableVerticalVideo()
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		verticalLabel,
		"  ",
		f.renderToggleWithDisabled(f.State.VerticalVideo, f.State.FocusedField == FormFieldVerticalVideo, verticalDisabled),
	))

	// Left Split toggle (only shown when vertical video is enabled)
	if f.State.VerticalVideo {
		f.fieldLinePositions[FormFieldLeftSplit] = len(rows)
		leftSplitLabel := labelStyle.Render("Left Split:")
		if f.State.FocusedField == FormFieldLeftSplit {
			leftSplitLabel = focusedLabelStyle.Render("Left Split:")
		}
		rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
			leftSplitLabel,
			"  ",
			f.renderToggle(f.State.LeftSplit, f.State.FocusedField == FormFieldLeftSplit),
		))
	}

	// Add Logos toggle
	f.fieldLinePositions[FormFieldAddLogos] = len(rows)
	logosLabel := labelStyle.Render("Add Logos:")
	if f.State.FocusedField == FormFieldAddLogos {
		logosLabel = focusedLabelStyle.Render("Add Logos:")
	}
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		logosLabel,
		"  ",
		f.renderToggle(f.State.AddLogos, f.State.FocusedField == FormFieldAddLogos),
	))

	// Logo selection fields
	if f.State.AddLogos {
		hintStyle := lipgloss.NewStyle().Foreground(ColorGray).Italic(true).MarginLeft(18)
		rows = append(rows, hintStyle.Render("Logos: 216x216px • Banner: 1080x200px"))

		f.fieldLinePositions[FormFieldLeftLogo] = len(rows)
		leftLabel := labelStyle.Render("Left Logo:")
		if f.State.FocusedField == FormFieldLeftLogo {
			leftLabel = focusedLabelStyle.Render("Left Logo:")
		}
		rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
			leftLabel,
			"  ",
			f.renderLogoSelector(f.State.SelectedLeftIdx, f.State.FocusedField == FormFieldLeftLogo),
		))

		f.fieldLinePositions[FormFieldRightLogo] = len(rows)
		rightLabel := labelStyle.Render("Right Logo:")
		if f.State.FocusedField == FormFieldRightLogo {
			rightLabel = focusedLabelStyle.Render("Right Logo:")
		}
		rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
			rightLabel,
			"  ",
			f.renderLogoSelector(f.State.SelectedRightIdx, f.State.FocusedField == FormFieldRightLogo),
		))

		f.fieldLinePositions[FormFieldBottomLogo] = len(rows)
		bottomLabel := labelStyle.Render("Bottom Banner:")
		if f.State.FocusedField == FormFieldBottomLogo {
			bottomLabel = focusedLabelStyle.Render("Bottom Banner:")
		}
		rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
			bottomLabel,
			"  ",
			f.renderLogoSelector(f.State.SelectedBottomIdx, f.State.FocusedField == FormFieldBottomLogo),
		))

		f.fieldLinePositions[FormFieldTitleColor] = len(rows)
		colorLabel := labelStyle.Render("Title Color:")
		if f.State.FocusedField == FormFieldTitleColor {
			colorLabel = focusedLabelStyle.Render("Title Color:")
		}
		rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
			colorLabel,
			"  ",
			f.renderColorSelector(f.State.FocusedField == FormFieldTitleColor),
		))

		if f.isBottomLogoGif() {
			f.fieldLinePositions[FormFieldGifLoopMode] = len(rows)
			gifLabel := labelStyle.Render("GIF Animation:")
			if f.State.FocusedField == FormFieldGifLoopMode {
				gifLabel = focusedLabelStyle.Render("GIF Animation:")
			}
			rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
				gifLabel,
				"  ",
				f.renderGifLoopSelector(f.State.FocusedField == FormFieldGifLoopMode),
			))
		}
	}

	// Description section
	rows = append(rows, "")
	rows = append(rows, dividerStyle.Render(strings.Repeat("─", internalWidth)))
	rows = append(rows, "")

	f.fieldLinePositions[FormFieldDescription] = len(rows)
	descHeaderText := "📄 Description"
	descHeaderStyle := sectionStyle
	if f.State.FocusedField == FormFieldDescription {
		descHeaderStyle = lipgloss.NewStyle().Foreground(ColorOrange).Bold(true)
		if f.State.InputMode {
			descHeaderText = "📄 » Description"
		}
	}
	descHeader := descHeaderStyle.Render(descHeaderText)
	descHeaderRow := lipgloss.NewStyle().Align(lipgloss.Center).Width(internalWidth).Render(descHeader)
	rows = append(rows, descHeaderRow)
	rows = append(rows, "")

	descRow := lipgloss.NewStyle().Width(internalWidth).Align(lipgloss.Center).Render(f.State.DescInput.View())
	rows = append(rows, descRow)

	// Description spell check warnings
	if len(f.State.DescIssues) > 0 {
		maxIssues := 3
		issuesToShow := f.State.DescIssues
		extraCount := 0
		if len(issuesToShow) > maxIssues {
			extraCount = len(issuesToShow) - maxIssues
			issuesToShow = issuesToShow[:maxIssues]
		}
		descWarning := spellcheck.FormatIssues(issuesToShow)
		if descWarning != "" {
			if extraCount > 0 {
				descWarning += fmt.Sprintf(" ... and %d more issues", extraCount)
			}
			descWarningStyle := lipgloss.NewStyle().
				Foreground(ColorOrange).
				Width(internalWidth).
				Align(lipgloss.Center)
			rows = append(rows, descWarningStyle.Render("⚠ "+descWarning))
		}
	}

	// Status messages
	if f.State.ErrorMsg != "" {
		errorStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Bold(true).
			Align(lipgloss.Center).
			Width(internalWidth)
		rows = append(rows, "")
		rows = append(rows, errorStyle.Render("Error: "+f.State.ErrorMsg))
	}

	if f.State.SuccessMsg != "" {
		successStyle := lipgloss.NewStyle().
			Foreground(ColorGreen).
			Bold(true).
			Align(lipgloss.Center).
			Width(internalWidth)
		rows = append(rows, "")
		rows = append(rows, successStyle.Render(f.State.SuccessMsg))
	}

	if f.State.IsSaving {
		savingStyle := lipgloss.NewStyle().
			Foreground(ColorOrange).
			Bold(true).
			Align(lipgloss.Center).
			Width(internalWidth)
		rows = append(rows, "")
		rows = append(rows, savingStyle.Render("Saving..."))
	}

	// Confirm buttons (new recording only)
	if f.Config.Mode == FormModeNewRecording {
		rows = append(rows, "")
		f.fieldLinePositions[FormFieldConfirm] = len(rows)
		rows = append(rows, f.renderConfirmButtons())
	}

	// Join all rows into form content
	formContent := lipgloss.JoinVertical(lipgloss.Left, rows...)

	// Check if scrolling is needed (based on form content, not bordered content)
	formContentLines := strings.Split(formContent, "\n")
	totalFormLines := len(formContentLines)

	// Calculate available lines inside the container (subtract border and padding)
	availableLines := contentHeight - 4 // 2 for border, 2 for padding
	if availableLines < 10 {
		availableLines = 10
	}

	// If content is taller than available space, use viewport for scrolling
	if f.ready && f.height > 0 && totalFormLines > availableLines {
		f.viewport.Width = internalWidth
		f.viewport.Height = availableLines
		f.viewport.SetContent(formContent)

		// Build scrollable content
		var scrollContent strings.Builder

		// Scroll up indicator
		if f.viewport.YOffset > 0 {
			scrollUpStyle := lipgloss.NewStyle().
				Foreground(ColorOrange).
				Bold(true).
				Width(internalWidth).
				Align(lipgloss.Center)
			scrollContent.WriteString(scrollUpStyle.Render("▲ more above (pgup/ctrl+u)"))
			scrollContent.WriteString("\n")
		}

		// Viewport content
		scrollContent.WriteString(f.viewport.View())

		// Scroll down indicator
		if f.viewport.YOffset < totalFormLines-availableLines {
			scrollDownStyle := lipgloss.NewStyle().
				Foreground(ColorOrange).
				Bold(true).
				Width(internalWidth).
				Align(lipgloss.Center)
			scrollContent.WriteString("\n")
			scrollContent.WriteString(scrollDownStyle.Render("▼ more below (pgdn/ctrl+d)"))
		}

		// Wrap scrollable content in container with border
		return containerStyle.Render(scrollContent.String())
	}

	// No scrolling needed - just wrap in container
	return containerStyle.Render(formContent)
}

func (f *RecordingForm) renderToggle(value bool, focused bool) string {
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

func (f *RecordingForm) renderToggleWithDisabled(value bool, focused bool, disabled bool) string {
	if disabled {
		disabledStyle := lipgloss.NewStyle().Foreground(ColorGray).Italic(true)
		return disabledStyle.Render("(requires webcam or screen)")
	}
	return f.renderToggle(value, focused)
}

func (f *RecordingForm) renderMonitorSelector() string {
	if len(f.Config.Monitors) == 0 {
		return lipgloss.NewStyle().Foreground(ColorGray).Italic(true).Render("(no monitors detected)")
	}

	var options []string
	for i, mon := range f.Config.Monitors {
		style := lipgloss.NewStyle().Padding(0, 1)
		label := fmt.Sprintf("%s (%dx%d)", mon.Name, mon.Width, mon.Height)

		if i == f.State.SelectedMonitor {
			if f.State.FocusedField == FormFieldMonitor {
				style = style.Background(ColorOrange).Foreground(lipgloss.Color("#000")).Bold(true)
			} else {
				style = style.Background(ColorGray).Foreground(ColorWhite)
			}
		} else {
			style = style.Foreground(ColorGray)
		}
		options = append(options, style.Render(label))
	}

	return lipgloss.JoinHorizontal(lipgloss.Center, options...)
}

func (f *RecordingForm) renderLogoSelector(selectedIdx int, focused bool) string {
	style := lipgloss.NewStyle()
	if focused {
		style = style.Foreground(ColorOrange).Bold(true)
	} else {
		style = style.Foreground(ColorWhite)
	}

	var label string
	if selectedIdx == 0 {
		label = "(none)"
	} else if selectedIdx > 0 && selectedIdx <= len(f.Config.Logos) {
		label = f.Config.Logos[selectedIdx-1]
	} else {
		label = "(none)"
	}

	arrows := ""
	if focused {
		arrows = "◀ "
	}
	suffix := ""
	if focused {
		suffix = " ▶"
	}

	return style.Render(arrows + label + suffix)
}

func (f *RecordingForm) renderColorSelector(focused bool) string {
	style := lipgloss.NewStyle()
	if focused {
		style = style.Bold(true)
	}

	color := config.TitleColors[f.State.SelectedColorIdx]
	colorStyle := lipgloss.NewStyle().Foreground(lipgloss.Color(color))

	arrows := ""
	if focused {
		arrows = "◀ "
	}
	suffix := ""
	if focused {
		suffix = " ▶"
	}

	return style.Render(arrows) + colorStyle.Render("■ "+color) + style.Render(suffix)
}

func (f *RecordingForm) renderGifLoopSelector(focused bool) string {
	style := lipgloss.NewStyle()
	if focused {
		style = style.Foreground(ColorOrange).Bold(true)
	} else {
		style = style.Foreground(ColorWhite)
	}

	mode := config.GifLoopModes[f.State.SelectedGifLoopIdx]

	arrows := ""
	if focused {
		arrows = "◀ "
	}
	suffix := ""
	if focused {
		suffix = " ▶"
	}

	return style.Render(arrows + string(mode) + suffix)
}

func (f *RecordingForm) renderConfirmButtons() string {
	hasSource := f.State.RecordAudio || f.State.RecordWebcam || f.State.RecordScreen
	hasTitle := strings.TrimSpace(f.State.TitleInput.Value()) != ""
	canGoLive := hasSource && hasTitle

	var goLive, cancel string

	if f.State.FocusedField == FormFieldConfirm {
		if f.State.ConfirmSelected {
			if canGoLive {
				goLive = lipgloss.NewStyle().
					Background(ColorOrange).
					Foreground(lipgloss.Color("#000")).
					Bold(true).
					Padding(0, 3).
					Render("Go Live!")
			} else {
				goLive = lipgloss.NewStyle().
					Background(ColorGray).
					Foreground(lipgloss.Color("#666")).
					Padding(0, 3).
					Render("Go Live!")
			}
			cancel = lipgloss.NewStyle().
				Foreground(ColorGray).
				Padding(0, 3).
				Render("Cancel")
		} else {
			if canGoLive {
				goLive = lipgloss.NewStyle().
					Foreground(ColorGray).
					Padding(0, 3).
					Render("Go Live!")
			} else {
				goLive = lipgloss.NewStyle().
					Foreground(lipgloss.Color("#666")).
					Padding(0, 3).
					Render("Go Live!")
			}
			cancel = lipgloss.NewStyle().
				Background(ColorGray).
				Foreground(ColorWhite).
				Bold(true).
				Padding(0, 3).
				Render("Cancel")
		}
	} else {
		if canGoLive {
			goLive = lipgloss.NewStyle().
				Foreground(ColorGray).
				Padding(0, 3).
				Render("Go Live!")
		} else {
			goLive = lipgloss.NewStyle().
				Foreground(lipgloss.Color("#666")).
				Padding(0, 3).
				Render("Go Live!")
		}
		cancel = lipgloss.NewStyle().
			Foreground(ColorGray).
			Padding(0, 3).
			Render("Cancel")
	}

	buttons := fmt.Sprintf("%s    %s", goLive, cancel)
	buttonRow := lipgloss.NewStyle().Width(f.internalWidth()).Align(lipgloss.Center).Render(buttons)

	// Show validation warnings
	var warnings []string
	if !hasTitle {
		warnings = append(warnings, "Title is required")
	}
	if !hasSource {
		warnings = append(warnings, "Enable at least one recording source")
	}

	if len(warnings) > 0 {
		warningStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Italic(true).
			Align(lipgloss.Center).
			Width(f.internalWidth())
		warningText := warningStyle.Render(strings.Join(warnings, " • "))
		return lipgloss.JoinVertical(lipgloss.Center, buttonRow, warningText)
	}

	return buttonRow
}

// GetTitle returns the current title value
func (f *RecordingForm) GetTitle() string {
	return strings.TrimSpace(f.State.TitleInput.Value())
}

// GetNumber returns the current number value
func (f *RecordingForm) GetNumber() string {
	return strings.TrimSpace(f.State.NumberInput.Value())
}

// GetDescription returns the current description value
func (f *RecordingForm) GetDescription() string {
	return strings.TrimSpace(f.State.DescInput.Value())
}

// GetPresenter returns the current presenter value
func (f *RecordingForm) GetPresenter() string {
	return strings.TrimSpace(f.State.PresenterInput.Value())
}

// GetSelectedTopic returns the selected topic
func (f *RecordingForm) GetSelectedTopic() models.Topic {
	if f.State.SelectedTopic >= 0 && f.State.SelectedTopic < len(f.Config.Topics) {
		return f.Config.Topics[f.State.SelectedTopic]
	}
	return models.Topic{}
}

// SetTitle sets the title value
func (f *RecordingForm) SetTitle(title string) {
	f.State.TitleInput.SetValue(title)
	f.State.TitleIssues = f.State.SpellChecker.Check(title)
}

// SetDescription sets the description value
func (f *RecordingForm) SetDescription(desc string) {
	f.State.DescInput.SetValue(desc)
	f.State.DescIssues = f.State.SpellChecker.Check(desc)
}

// SetPresenter sets the presenter value
func (f *RecordingForm) SetPresenter(presenter string) {
	f.State.PresenterInput.SetValue(presenter)
}

// SetSelectedTopic sets the selected topic by name
func (f *RecordingForm) SetSelectedTopic(topicName string) {
	for i, t := range f.Config.Topics {
		if t.Name == topicName {
			f.State.SelectedTopic = i
			return
		}
	}
}
