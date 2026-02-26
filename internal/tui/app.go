package tui

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"github.com/charmbracelet/bubbles/key"
	"github.com/charmbracelet/bubbles/spinner"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
	"github.com/kartoza/kartoza-screencaster/internal/audio"
	"github.com/kartoza/kartoza-screencaster/internal/beep"
	"github.com/kartoza/kartoza-screencaster/internal/config"
	"github.com/kartoza/kartoza-screencaster/internal/models"
	"github.com/kartoza/kartoza-screencaster/internal/monitor"
	"github.com/kartoza/kartoza-screencaster/internal/recorder"
)

// Screen represents the current screen being displayed
type Screen int

const (
	ScreenMenu Screen = iota
	ScreenRecordingSetup
	ScreenRecording
	ScreenHistory
	ScreenOptions
	ScreenYouTubeSetup
	ScreenYouTubeUpload
	ScreenSyndicationSetup
	ScreenSyndicationPost
)

// RecordingButton represents a button on the recording screen
type RecordingButton int

const (
	ButtonPause RecordingButton = iota
	ButtonStop
)

// AppModel is the main application model that coordinates screens
type AppModel struct {
	screen            Screen
	menu              *MenuModel
	recordingSetup    *RecordingSetupModel
	options           *OptionsModel
	history           *HistoryModel
	youtubeSetup      *YouTubeSetupModel
	youtubeUpload     *YouTubeUploadModel
	syndicationSetup  *SyndicationSetupModel
	syndicationPost   *SyndicationPostModel
	recorder          *recorder.Recorder
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
	processingBtn   ProcessingButton // Selected button on processing complete screen
	processingDone  bool             // Whether processing is complete and showing buttons
	metadata        models.RecordingMetadata
	recordingInfo   *models.RecordingInfo
	outputDir       string

	// Recording screen state
	isPaused         bool
	isPausing        bool
	isResuming       bool
	selectedButton   RecordingButton

	// Progress channel for processing updates
	progressChan chan recorder.ProgressUpdate

	// External recording detection
	externalRecordingActive bool
	externalRecordingPIDs   []string

	// Presets mode - opens directly to recording presets, auto-closes on save
	presetsMode bool

	// Edit recording mode - opens directly to history with latest needs_metadata recording
	editRecordingMode bool

	// Room noise capture state
	roomNoiseCountdown  int           // Countdown seconds remaining
	roomNoiseRecorder   *audio.Recorder
	roomNoiseFile       string        // Path to room noise file
}

// countRecordings counts the number of valid recordings in the screencasts folder
func countRecordings() int {
	videosDir := config.GetDefaultVideosDir()

	if _, err := os.Stat(videosDir); os.IsNotExist(err) {
		return 0
	}

	entries, err := os.ReadDir(videosDir)
	if err != nil {
		return 0
	}

	count := 0
	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}

		// Check if folder has recording.json
		infoPath := filepath.Join(videosDir, entry.Name(), "recording.json")
		if _, err := os.Stat(infoPath); err == nil {
			count++
		}
	}

	return count
}

// updateGlobalAppState updates the global app state for header display
func updateGlobalAppState(isRecording bool, blinkOn bool, status string) {
	GlobalAppState.IsRecording = isRecording
	GlobalAppState.BlinkOn = blinkOn
	GlobalAppState.Status = status
	GlobalAppState.TotalRecordings = countRecordings()
	GlobalAppState.YouTubeConnected = checkYouTubeConnected()
}

// checkYouTubeConnected checks if YouTube is connected
func checkYouTubeConnected() bool {
	cfg, err := config.Load()
	if err != nil {
		return false
	}
	return cfg.IsYouTubeConnected()
}

// checkExternalRecording checks if wl-screenrec processes are running externally
func checkExternalRecording() (bool, []string) {
	// Use pgrep to find wl-screenrec processes
	cmd := exec.Command("pgrep", "-a", "wl-screenrec")
	output, err := cmd.Output()
	if err != nil {
		// pgrep returns exit code 1 if no processes found
		return false, nil
	}

	lines := strings.Split(strings.TrimSpace(string(output)), "\n")
	var pids []string
	for _, line := range lines {
		if line != "" {
			parts := strings.Fields(line)
			if len(parts) > 0 {
				pids = append(pids, parts[0])
			}
		}
	}

	return len(pids) > 0, pids
}

// NewAppModel creates a new application model
func NewAppModel() AppModel {
	s := spinner.New()
	s.Spinner = spinner.Dot
	s.Style = lipgloss.NewStyle().Foreground(ColorRed)

	rec := recorder.New()

	// Check if a recording is already in progress
	status := rec.GetStatus()
	initialScreen := ScreenMenu
	initialState := stateReady

	if status.IsRecording {
		initialScreen = ScreenRecording
		initialState = stateRecording
	}

	// Check for external wl-screenrec processes
	externalActive, externalPIDs := checkExternalRecording()

	// Create menu and set external recording state
	menu := NewMenuModel()
	menu.SetExternalRecording(externalActive, externalPIDs)

	// Initialize global app state
	GlobalAppState.TotalRecordings = countRecordings()
	GlobalAppState.YouTubeConnected = checkYouTubeConnected()
	if status.IsRecording {
		GlobalAppState.IsRecording = true
		GlobalAppState.Status = "Recording"
	} else {
		GlobalAppState.IsRecording = false
		GlobalAppState.Status = "Ready"
	}

	return AppModel{
		screen:                  initialScreen,
		menu:                    menu,
		recordingSetup:          NewRecordingSetupModel(),
		options:                 NewOptionsModel(),
		history:                 NewHistoryModel(),
		youtubeSetup:            NewYouTubeSetupModel(),
		recorder:                rec,
		spinner:                 s,
		blinkOn:                 true,
		state:                   initialState,
		status:                  status,
		countdownNum:            5,
		processing:              NewProcessingState(),
		processingFrame:         0,
		externalRecordingActive: externalActive,
		externalRecordingPIDs:   externalPIDs,
	}
}

// NewAppModelWithPresets creates an app model that opens directly to the presets section
// of the Options screen. Used when launched with --presets flag (e.g. from systray first-run).
func NewAppModelWithPresets() AppModel {
	m := NewAppModel()
	m.presetsMode = true
	m.screen = ScreenOptions
	m.options = NewOptionsModel()
	m.options.focusedField = OptionsFieldPresetRecordAudio
	return m
}

// NewAppModelWithEditRecording creates an app model that opens directly to the history
// screen with the latest needs_metadata recording in edit mode.
// Used when launched with --edit-recording flag (e.g. from systray after stopping a recording).
func NewAppModelWithEditRecording() AppModel {
	m := NewAppModel()
	m.editRecordingMode = true
	m.screen = ScreenHistory
	m.history = NewHistoryModel()
	m.history.editRecordingOnLoad = true
	return m
}

// Init initializes the application
func (m AppModel) Init() tea.Cmd {
	cmds := []tea.Cmd{
		m.spinner.Tick,
		tickCmd(),
		blinkCmd(),
		updateStatus(m.recorder),
		updateMonitors(),
	}

	// Initialize the active screen's sub-model if needed
	if m.screen == ScreenHistory && m.history != nil {
		cmds = append(cmds, m.history.Init())
	}
	if m.screen == ScreenOptions && m.options != nil {
		cmds = append(cmds, m.options.Init())
	}

	return tea.Batch(cmds...)
}

// Update handles messages
func (m AppModel) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	// Handle recording setup completion messages first (from any screen)
	switch msg.(type) {
	case recordingSetupCompleteMsg:
		// Recording setup is complete, save presets for next time and start countdown
		_ = m.recordingSetup.SaveAllPresets()
		m.metadata = m.recordingSetup.GetMetadata()
		m.screen = ScreenRecording
		m.state = stateCountdown
		m.countdownNum = 5
		go beep.Play(5)
		return m, tea.Tick(time.Second, func(t time.Time) tea.Msg {
			return countdownTickMsg{}
		})
	case backToMenuMsg:
		m.screen = ScreenMenu
		// Don't recreate recordingSetup — preserve form state so logos/toggles
		// persist when the user returns to the recording form
		// Refresh YouTube status when returning to menu
		updateGlobalAppState(m.status.IsRecording, m.blinkOn, GlobalAppState.Status)
		return m, nil
	case goToYouTubeSetupMsg:
		m.screen = ScreenYouTubeSetup
		m.youtubeSetup = NewYouTubeSetupModel()
		m.youtubeSetup.width = m.width
		m.youtubeSetup.height = m.height
		return m, m.youtubeSetup.Init()
	case goToSyndicationSetupMsg:
		m.screen = ScreenSyndicationSetup
		m.syndicationSetup = NewSyndicationSetupModel()
		m.syndicationSetup.width = m.width
		m.syndicationSetup.height = m.height
		return m, m.syndicationSetup.Init()
	case syndicationAuthStartedMsg, syndicationAuthCompleteMsg:
		// Forward syndication auth messages
		if m.screen == ScreenSyndicationSetup && m.syndicationSetup != nil {
			newSetup, cmd := m.syndicationSetup.Update(msg)
			m.syndicationSetup = newSetup
			return m, cmd
		}
		return m, nil
	case syndicationPostProgressMsg, syndicationPostCompleteMsg:
		// Forward syndication post messages
		if m.screen == ScreenSyndicationPost && m.syndicationPost != nil {
			newPost, cmd := m.syndicationPost.Update(msg)
			m.syndicationPost = newPost
			return m, cmd
		}
		return m, nil
	case backToHistoryMsg:
		// Return to history from syndication post
		m.screen = ScreenHistory
		m.history = NewHistoryModel()
		m.history.width = m.width
		m.history.height = m.height
		return m, m.history.Init()
	case youtubeAuthStartedMsg, youtubeAuthCompleteMsg, youtubeDisconnectMsg, youtubeVerifyCompleteMsg, youtubePlaylistsLoadedMsg, youtubePlaylistCreatedMsg:
		// Forward YouTube auth messages to the YouTube setup model
		if m.screen == ScreenYouTubeSetup && m.youtubeSetup != nil {
			newSetup, cmd := m.youtubeSetup.Update(msg)
			m.youtubeSetup = newSetup
			// Refresh global state to update status bar
			updateGlobalAppState(m.status.IsRecording, m.blinkOn, GlobalAppState.Status)
			return m, cmd
		}
		return m, nil
	case uploadProgressMsg, uploadCompleteMsg, playlistsLoadedMsg:
		// Forward upload messages to the YouTube upload model
		if m.screen == ScreenYouTubeUpload && m.youtubeUpload != nil {
			newUpload, cmd := m.youtubeUpload.Update(msg)
			m.youtubeUpload = newUpload
			return m, cmd
		}
		return m, nil
	case youtubeUploadSkippedMsg, youtubeUploadDoneMsg:
		// YouTube upload done or skipped
		// If we have a youtubeUpload with recordingInfo, return to history and refresh
		if m.youtubeUpload != nil && m.youtubeUpload.recordingInfo != nil {
			m.screen = ScreenHistory
			m.history = NewHistoryModel()
			m.history.width = m.width
			m.history.height = m.height
			return m, m.history.Init()
		}
		// Otherwise return to menu
		m.screen = ScreenMenu
		return m, nil
	}

	// If on recording setup screen, pass messages to the form
	if m.screen == ScreenRecordingSetup {
		// Handle escape to go back (before passing to form)
		if keyMsg, ok := msg.(tea.KeyMsg); ok {
			if key.Matches(keyMsg, key.NewBinding(key.WithKeys("esc"))) {
				m.screen = ScreenMenu
				return m, nil
			}
			if key.Matches(keyMsg, key.NewBinding(key.WithKeys("ctrl+c"))) {
				return m, tea.Quit
			}
		}

		// Pass message to the form
		newSetup, cmd := m.recordingSetup.Update(msg)
		m.recordingSetup = newSetup

		return m, cmd
	}

	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		m.menu.width = msg.Width
		m.menu.height = msg.Height
		// Also update sub-model dimensions
		if m.recordingSetup != nil {
			m.recordingSetup.width = msg.Width
			m.recordingSetup.height = msg.Height
		}
		if m.history != nil {
			m.history.width = msg.Width
			m.history.height = msg.Height
		}
		if m.options != nil {
			m.options.width = msg.Width
			m.options.height = msg.Height
		}
		if m.youtubeSetup != nil {
			m.youtubeSetup.width = msg.Width
			m.youtubeSetup.height = msg.Height
		}
		if m.youtubeUpload != nil {
			m.youtubeUpload.width = msg.Width
			m.youtubeUpload.height = msg.Height
		}
		if m.syndicationSetup != nil {
			m.syndicationSetup.width = msg.Width
			m.syndicationSetup.height = msg.Height
		}
		if m.syndicationPost != nil {
			m.syndicationPost.width = msg.Width
			m.syndicationPost.height = msg.Height
		}
		return m, nil

	case tea.KeyMsg:
		// Handle based on current screen and state
		return m.handleKeyMsg(msg)

	case tickMsg:
		if m.state != stateCountdown {
			// Re-check for external recordings
			externalActive, externalPIDs := checkExternalRecording()
			if m.externalRecordingActive != externalActive {
				m.externalRecordingActive = externalActive
				m.externalRecordingPIDs = externalPIDs
				m.menu.SetExternalRecording(externalActive, externalPIDs)
			}

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
		return m.handleCountdownTick()

	case roomNoiseTickMsg:
		return m.handleRoomNoiseTick()

	case roomNoiseCompleteMsg:
		// Room noise recording finished, transition to processing
		if msg.err != nil {
			// Log error but continue with processing
			m.roomNoiseFile = "" // Clear the file path since it failed
		}
		// If this was a systray-initiated recording (editRecordingMode), use the systray processing flow
		if m.editRecordingMode && m.recordingInfo != nil {
			return m.transitionToProcessingForSystray(m.recordingInfo)
		}
		return m.transitionToProcessing()

	case statusUpdateMsg:
		// Don't update status during processing - it can cause race conditions
		// where the state gets reset from stateProcessing back to stateRecording
		if m.state == stateProcessing {
			return m, nil
		}
		m.status = models.RecordingStatus(msg)
		if m.status.IsRecording {
			m.state = stateRecording
			m.screen = ScreenRecording
			m.isPaused = false
		} else if m.status.IsPaused {
			// Recording is paused - stay on recording screen
			m.state = stateRecording
			m.screen = ScreenRecording
			m.isPaused = true
		} else if m.state == stateRecording && !m.isPaused {
			// Only transition to ready if we weren't paused
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
			if !msg.Completed {
				// Step is starting
				m.processing.SetStepByIndex(msg.Step, StepRunning)
			} else if msg.Skipped {
				// Step was skipped
				m.processing.SetStepByIndex(msg.Step, StepSkipped)
			} else if msg.Error != nil {
				// Step failed
				m.processing.SetStepByIndex(msg.Step, StepFailed)
				m.processing.Error = msg.Error
			} else {
				// Step completed successfully
				m.processing.SetStepByIndex(msg.Step, StepComplete)

				// If step 0 (stopping recorders) completed, start the processing pipeline
				if msg.Step == 0 {
					m.progressChan = make(chan recorder.ProgressUpdate, 100)
					go m.recorder.ProcessWithProgress(m.progressChan)
					return m, waitForProgressUpdate(m.progressChan)
				}
			}
		}
		return m, waitForProgressUpdate(m.progressChan)

	case processingPercentMsg:
		if m.state == stateProcessing && m.processing != nil {
			m.processing.SetStepProgress(msg.Step, msg.Percent)
		}
		return m, waitForProgressUpdate(m.progressChan)

	case processingCompleteMsg:
		if m.state == stateProcessing && m.processing != nil {
			m.processing.Complete()
			m.processingDone = true
			// Default to Upload button if YouTube is connected, else Menu button
			cfg, _ := config.Load()
			if cfg.IsYouTubeConnected() {
				m.processingBtn = ProcessingButtonUpload
			} else {
				m.processingBtn = ProcessingButtonMenu
			}
		}
		return m, nil

	case processingDoneMsg:
		m.state = stateReady
		m.processing.Reset()
		// Reset recording setup so next recording starts with a fresh form
		m.recordingSetup = nil
		// Update global state - recording complete, refresh count
		updateGlobalAppState(false, true, "Ready")

		// Check if YouTube upload should be prompted
		cfg, _ := config.Load()
		if cfg.YouTube.AutoPromptUpload && cfg.IsYouTubeConnected() && m.recordingInfo != nil {
			// Find the processed video file - check for merged file first
			videoPath := m.recordingInfo.Files.MergedFile
			if videoPath == "" {
				videoPath = filepath.Join(m.outputDir, "final.mp4")
			}
			if _, err := os.Stat(videoPath); err == nil {
				// Create YouTube upload model with recording info for metadata storage
				m.youtubeUpload = NewYouTubeUploadModelWithRecording(videoPath, m.recordingInfo)
				m.youtubeUpload.width = m.width
				m.youtubeUpload.height = m.height
				m.screen = ScreenYouTubeUpload
				return m, m.youtubeUpload.Init()
			}
		}

		// Otherwise go back to menu
		m.screen = ScreenMenu
		return m, updateStatus(m.recorder)

	case processingErrorMsg:
		if m.state == stateProcessing && m.processing != nil {
			m.processing.FailStep(msg.Error)
			m.err = msg.Error
		}
		return m, nil

	case menuActionMsg:
		return m.handleMenuAction(msg.action)

	case recordingSetupCompleteMsg:
		// Recording setup is complete, save presets for next time and start countdown
		_ = m.recordingSetup.SaveAllPresets()
		m.metadata = m.recordingSetup.GetMetadata()
		m.screen = ScreenRecording
		m.state = stateCountdown
		m.countdownNum = 5
		go beep.Play(5)
		return m, tea.Tick(time.Second, func(t time.Time) tea.Msg {
			return countdownTickMsg{}
		})

	case backToMenuMsg:
		// Return to main menu from history
		m.screen = ScreenMenu
		return m, nil

	case recordingsLoadedMsg:
		// Forward to history model
		if m.screen == ScreenHistory && m.history != nil {
			newHistory, cmd := m.history.Update(msg)
			m.history = newHistory
			return m, cmd
		}
		return m, nil

	case recordingSavedMsg:
		// Forward to history model
		if m.screen == ScreenHistory && m.history != nil {
			newHistory, cmd := m.history.Update(msg)
			m.history = newHistory
			return m, cmd
		}
		return m, nil

	case startYouTubeUploadMsg:
		// YouTube upload requested from history view
		m.youtubeUpload = NewYouTubeUploadModelWithRecording(msg.videoPath, msg.recording)
		m.youtubeUpload.width = m.width
		m.youtubeUpload.height = m.height
		m.screen = ScreenYouTubeUpload
		return m, m.youtubeUpload.Init()

	case startReprocessMsg:
		// Reprocess recording requested from history view
		if msg.recording == nil {
			return m, nil
		}

		// Clear previous processing status and errors before reprocessing
		msg.recording.SetStatus(models.StatusProcessing)
		msg.recording.Processing.Errors = nil
		msg.recording.Processing.ErrorDetail = ""
		msg.recording.Processing.Traceback = ""
		msg.recording.Processing.ProcessedAt = time.Time{}
		msg.recording.Processing.NormalizeApplied = false
		msg.recording.Processing.VerticalCreated = false
		_ = msg.recording.Save()

		// Set up for reprocessing
		m.screen = ScreenRecording
		m.state = stateProcessing
		m.outputDir = msg.recording.Files.FolderPath
		m.recordingInfo = msg.recording
		m.processing.Reset()
		m.processing.ConfigureSteps(
			msg.recording.Settings.AudioEnabled,
			msg.recording.Settings.ScreenEnabled,
			msg.recording.Settings.WebcamEnabled,
			msg.recording.Settings.VerticalEnabled,
		)
		// Skip the "Stopping recorders" step since we're reprocessing existing files
		m.processing.SetStepByIndex(ProcessStepStopping, StepSkipped)
		m.processing.Start()
		m.processingFrame = 0

		// Configure recorder with the recording info
		m.recorder.SetRecordingInfo(msg.recording)

		// Start processing pipeline directly (no need to stop recorders)
		m.progressChan = make(chan recorder.ProgressUpdate, 100)
		go m.recorder.ProcessWithProgress(m.progressChan)

		return m, tea.Batch(
			processingTickCmd(),
			waitForProgressUpdate(m.progressChan),
		)

	case recordingSavedNeedsProcessingMsg:
		// Recording from systray was saved with metadata, now process it
		// Note: Room noise was already captured by systray before opening TUI
		if msg.recording == nil {
			return m, nil
		}

		// Store recording info for later processing
		m.outputDir = msg.recording.Files.FolderPath
		m.recordingInfo = msg.recording

		// Check if room noise file exists (captured by systray)
		roomNoiseFile := filepath.Join(m.outputDir, "room_noise.wav")
		if _, err := os.Stat(roomNoiseFile); err == nil {
			m.roomNoiseFile = roomNoiseFile
		}

		// Go directly to processing (room noise already captured by systray)
		return m.transitionToProcessingForSystray(msg.recording)

	case youtubePrivacyChangedMsg, youtubeVideoDeletedMsg:
		// Forward YouTube action messages to history model
		if m.screen == ScreenHistory && m.history != nil {
			newHistory, cmd := m.history.Update(msg)
			m.history = newHistory
			return m, cmd
		}
		return m, nil

	case pauseCompleteMsg:
		m.isPausing = false
		if msg.err != nil {
			m.err = msg.err
			updateGlobalAppState(m.status.IsRecording, m.blinkOn, "Recording")
		} else {
			m.isPaused = true
			m.status.IsRecording = false
			m.status.IsPaused = true
			updateGlobalAppState(false, m.blinkOn, "Paused")
		}
		return m, updateStatus(m.recorder)

	case resumeCompleteMsg:
		m.isResuming = false
		if msg.err != nil {
			m.err = msg.err
			updateGlobalAppState(false, m.blinkOn, "Paused")
		} else {
			m.isPaused = false
			m.status.IsRecording = true
			m.status.IsPaused = false
			m.state = stateRecording
			updateGlobalAppState(true, m.blinkOn, "Recording")
		}
		return m, updateStatus(m.recorder)
	}

	return m, nil
}

// handleKeyMsg handles keyboard input based on current state
func (m AppModel) handleKeyMsg(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	// Handle processing state
	if m.state == stateProcessing {
		if key.Matches(msg, key.NewBinding(key.WithKeys("q", "ctrl+c"))) {
			return m, tea.Quit
		}
		// When processing is done, allow button navigation
		if m.processingDone {
			cfg, _ := config.Load()
			youtubeConnected := cfg.IsYouTubeConnected()

			switch msg.String() {
			case "left", "right", "tab":
				if youtubeConnected {
					// Toggle between upload and menu buttons
					if m.processingBtn == ProcessingButtonUpload {
						m.processingBtn = ProcessingButtonMenu
					} else {
						m.processingBtn = ProcessingButtonUpload
					}
				}
				return m, nil
			case "enter":
				if m.processingBtn == ProcessingButtonUpload && youtubeConnected {
					// Go to YouTube upload
					m.processingDone = false
					m.state = stateReady
					m.processing.Reset()
					m.screen = ScreenYouTubeUpload
					if m.recordingInfo != nil {
						videoPath := m.recordingInfo.Files.MergedFile
						if m.recordingInfo.Files.VerticalFile != "" {
							videoPath = m.recordingInfo.Files.VerticalFile
						}
						m.youtubeUpload = NewYouTubeUploadModelWithRecording(videoPath, m.recordingInfo)
					}
					return m, nil
				} else {
					// Return to menu
					m.processingDone = false
					m.state = stateReady
					m.processing.Reset()
					m.screen = ScreenMenu
					updateGlobalAppState(false, true, "Ready")
					return m, nil
				}
			case "v", "m", "a", "o":
				// Media preview shortcuts
				if cmd := HandleProcessingMediaKey(msg.String(), m.recordingInfo); cmd != nil {
					return m, cmd
				}
				return m, nil
			}
		}
		return m, nil
	}

	// Handle countdown state
	if m.state == stateCountdown {
		if key.Matches(msg, key.NewBinding(key.WithKeys("esc", "q"))) {
			m.state = stateReady
			m.countdownNum = 5
			m.screen = ScreenMenu
			return m, nil
		}
		return m, nil
	}

	// Handle based on screen
	switch m.screen {
	case ScreenMenu:
		return m.handleMenuKeys(msg)
	case ScreenRecordingSetup:
		return m.handleRecordingSetupKeys(msg)
	case ScreenRecording:
		return m.handleRecordingKeys(msg)
	case ScreenHistory:
		return m.handleHistoryKeys(msg)
	case ScreenOptions:
		return m.handleOptionsKeys(msg)
	case ScreenYouTubeSetup:
		return m.handleYouTubeSetupKeys(msg)
	case ScreenYouTubeUpload:
		return m.handleYouTubeUploadKeys(msg)
	case ScreenSyndicationSetup:
		return m.handleSyndicationSetupKeys(msg)
	case ScreenSyndicationPost:
		return m.handleSyndicationPostKeys(msg)
	}

	return m, nil
}

// handleMenuKeys handles keys on the menu screen
func (m AppModel) handleMenuKeys(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	newMenu, cmd := m.menu.Update(msg)
	m.menu = newMenu
	return m, cmd
}

// handleRecordingSetupKeys handles keys on the recording setup screen
func (m AppModel) handleRecordingSetupKeys(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	// Handle escape to go back
	if key.Matches(msg, key.NewBinding(key.WithKeys("esc"))) {
		m.screen = ScreenMenu
		updateGlobalAppState(false, m.blinkOn, "Ready")
		return m, nil
	}

	// Handle quit
	if key.Matches(msg, key.NewBinding(key.WithKeys("ctrl+c"))) {
		return m, tea.Quit
	}

	// Update the setup form
	newSetup, cmd := m.recordingSetup.Update(msg)
	m.recordingSetup = newSetup
	return m, cmd
}

// handleRecordingKeys handles keys on the recording screen
func (m AppModel) handleRecordingKeys(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	switch {
	case key.Matches(msg, key.NewBinding(key.WithKeys("q", "ctrl+c"))):
		return m, tea.Quit

	case key.Matches(msg, key.NewBinding(key.WithKeys("left", "h"))):
		// Move to Pause button
		if m.status.IsRecording || m.isPaused {
			m.selectedButton = ButtonPause
		}
		return m, nil

	case key.Matches(msg, key.NewBinding(key.WithKeys("right", "l"))):
		// Move to Stop button
		if m.status.IsRecording || m.isPaused {
			m.selectedButton = ButtonStop
		}
		return m, nil

	case key.Matches(msg, key.NewBinding(key.WithKeys("p"))):
		// Direct pause/resume toggle
		if m.status.IsRecording && !m.isPaused {
			return m.handlePause()
		} else if m.isPaused {
			return m.handleResume()
		}
		return m, nil

	case key.Matches(msg, key.NewBinding(key.WithKeys(" ", "enter"))):
		if m.status.IsRecording || m.isPaused {
			switch m.selectedButton {
			case ButtonPause:
				if m.isPaused {
					return m.handleResume()
				}
				return m.handlePause()
			case ButtonStop:
				return m.handleStop()
			}
		}
		return m, nil

	case key.Matches(msg, key.NewBinding(key.WithKeys("s"))):
		// Direct stop
		if m.status.IsRecording || m.isPaused {
			return m.handleStop()
		}
		return m, nil

	case key.Matches(msg, key.NewBinding(key.WithKeys("esc"))):
		// Go back to menu (only if not recording and not paused)
		if !m.status.IsRecording && !m.isPaused {
			m.screen = ScreenMenu
			updateGlobalAppState(false, m.blinkOn, "Ready")
		}
		return m, nil

	case key.Matches(msg, key.NewBinding(key.WithKeys("?"))):
		m.showHelp = !m.showHelp
		return m, nil
	}

	return m, nil
}

// handlePause handles pausing the recording
func (m AppModel) handlePause() (tea.Model, tea.Cmd) {
	// Don't allow pause if already pausing/resuming
	if m.isPausing || m.isResuming {
		return m, nil
	}

	m.isPausing = true
	updateGlobalAppState(false, m.blinkOn, "Pausing...")

	// Run pause asynchronously
	rec := m.recorder
	return m, func() tea.Msg {
		err := rec.Pause()
		return pauseCompleteMsg{err: err}
	}
}

// handleResume handles resuming the recording
func (m AppModel) handleResume() (tea.Model, tea.Cmd) {
	// Don't allow resume if already pausing/resuming
	if m.isPausing || m.isResuming {
		return m, nil
	}

	m.isResuming = true
	updateGlobalAppState(false, m.blinkOn, "Resuming...")

	// Run resume asynchronously
	rec := m.recorder
	return m, func() tea.Msg {
		err := rec.Resume()
		return resumeCompleteMsg{err: err}
	}
}

// handleStop handles stopping the recording
func (m AppModel) handleStop() (tea.Model, tea.Cmd) {
	m.isPaused = false

	// First stop the main recording
	if m.recorder != nil {
		_ = m.recorder.Stop()
	}

	// Check if audio was enabled - if so, capture room noise for calibration
	audioEnabled := m.recordingInfo != nil && m.recordingInfo.Settings.AudioEnabled
	if audioEnabled && m.outputDir != "" {
		// Transition to room noise capture state
		m.state = stateRoomNoise
		m.roomNoiseCountdown = 30 // 30 seconds of room noise
		m.roomNoiseFile = filepath.Join(m.outputDir, "room_noise.wav")

		// Start room noise recording
		m.roomNoiseRecorder = audio.NewRecorder("", m.roomNoiseFile)
		if err := m.roomNoiseRecorder.Start(); err != nil {
			// If room noise recording fails, skip it and go to processing
			return m.transitionToProcessing()
		}

		updateGlobalAppState(false, m.blinkOn, "Capturing Room Noise")
		return m, roomNoiseTickCmd()
	}

	// No audio, go directly to processing
	return m.transitionToProcessing()
}

// transitionToProcessing moves from room noise capture (or directly from stop) to processing
func (m AppModel) transitionToProcessing() (tea.Model, tea.Cmd) {
	m.state = stateProcessing
	m.processing.Reset()

	// Configure which steps are applicable based on recording settings
	if m.recordingInfo != nil {
		m.processing.ConfigureSteps(
			m.recordingInfo.Settings.AudioEnabled,
			m.recordingInfo.Settings.ScreenEnabled,
			m.recordingInfo.Settings.WebcamEnabled,
			m.recordingInfo.Settings.VerticalEnabled,
		)
	}

	m.processing.Start()
	m.processingFrame = 0

	// Skip the "Stopping recorders" step since we already stopped in handleStop
	m.processing.SetStepByIndex(ProcessStepStopping, StepSkipped)

	return m, tea.Batch(
		processingTickCmd(),
		m.runProcessingPipeline(),
	)
}

// transitionToProcessingForSystray moves to processing for systray-initiated recordings
// This is similar to transitionToProcessing but handles the case where recording was stopped via systray
func (m AppModel) transitionToProcessingForSystray(recording *models.RecordingInfo) (tea.Model, tea.Cmd) {
	// Set status to processing
	recording.SetStatus(models.StatusProcessing)
	_ = recording.Save()

	// Set up for processing
	m.screen = ScreenRecording
	m.state = stateProcessing
	m.processing.Reset()
	m.processing.ConfigureSteps(
		recording.Settings.AudioEnabled,
		recording.Settings.ScreenEnabled,
		recording.Settings.WebcamEnabled,
		recording.Settings.VerticalEnabled,
	)
	// Skip the "Stopping recorders" step since recording was already stopped via systray
	m.processing.SetStepByIndex(ProcessStepStopping, StepSkipped)
	m.processing.Start()
	m.processingFrame = 0

	// Configure recorder with the recording info
	m.recorder.SetRecordingInfo(recording)

	// Set the room noise file on the recorder for audio processing (if we captured it)
	if m.roomNoiseFile != "" {
		m.recorder.SetRoomNoiseFile(m.roomNoiseFile)
	}

	// Start processing pipeline directly
	m.progressChan = make(chan recorder.ProgressUpdate, 100)
	go m.recorder.ProcessWithProgress(m.progressChan)

	return m, tea.Batch(
		processingTickCmd(),
		waitForProgressUpdate(m.progressChan),
	)
}

// roomNoiseTickCmd returns a command that ticks every second for room noise countdown
func roomNoiseTickCmd() tea.Cmd {
	return tea.Tick(time.Second, func(t time.Time) tea.Msg {
		return roomNoiseTickMsg{}
	})
}

// handleHistoryKeys handles keys on the history screen
func (m AppModel) handleHistoryKeys(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	// Let history model handle keys
	newHistory, cmd := m.history.Update(msg)
	m.history = newHistory
	return m, cmd
}

// handleOptionsKeys handles keys on the options screen
func (m AppModel) handleOptionsKeys(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	// Handle escape - in presets mode, quit the app; otherwise go back to menu
	if key.Matches(msg, key.NewBinding(key.WithKeys("esc"))) {
		if m.presetsMode {
			return m, tea.Quit
		}
		m.screen = ScreenMenu
		return m, nil
	}

	// Handle quit
	if key.Matches(msg, key.NewBinding(key.WithKeys("ctrl+c"))) {
		return m, tea.Quit
	}

	// Update the options form
	newOptions, cmd := m.options.Update(msg)
	m.options = newOptions

	// In presets mode, auto-close after successful save
	if m.presetsMode && m.options.savedSuccess {
		return m, tea.Quit
	}

	return m, cmd
}

// handleMenuAction handles menu item selection
func (m AppModel) handleMenuAction(action MenuItem) (tea.Model, tea.Cmd) {
	switch action {
	case MenuNewRecording:
		// Go to recording setup screen - always create fresh form for new recording
		m.screen = ScreenRecordingSetup
		m.recordingSetup = NewRecordingSetupModel()
		m.recordingSetup.width = m.width
		m.recordingSetup.height = m.height
		// Reset global state when starting new recording setup
		updateGlobalAppState(false, m.blinkOn, "Ready")
		return m, m.recordingSetup.Init()

	case MenuRecordingHistory:
		m.screen = ScreenHistory
		m.history = NewHistoryModel()
		m.history.width = m.width
		m.history.height = m.height
		return m, m.history.Init()

	case MenuOptions:
		m.screen = ScreenOptions
		m.options = NewOptionsModel()
		m.options.width = m.width
		m.options.height = m.height
		return m, m.options.Init()

	case MenuQuit:
		return m, tea.Quit
	}

	return m, nil
}

// handleCountdownTick handles countdown timer ticks
func (m AppModel) handleCountdownTick() (tea.Model, tea.Cmd) {
	if m.state != stateCountdown {
		return m, nil
	}

	m.countdownNum--

	if m.countdownNum < 0 {
		// Countdown finished, start recording
		m.state = stateRecording

		// Generate folder name and create recording directory
		m.metadata.GenerateFolderName()
		baseDir := config.GetDefaultVideosDir()
		m.outputDir = filepath.Join(baseDir, m.metadata.FolderName)

		// Create the recording directory
		if err := os.MkdirAll(m.outputDir, 0755); err != nil {
			m.err = fmt.Errorf("failed to create recording directory: %w", err)
			m.state = stateReady
			m.screen = ScreenMenu
			return m, nil
		}

		// Get monitor info for recording
		monitorName, _ := monitor.GetMouseMonitor()
		if m.recordingSetup != nil && m.recordingSetup.form != nil && m.recordingSetup.form.State.SelectedMonitor >= 0 && m.recordingSetup.form.State.SelectedMonitor < len(m.recordingSetup.monitors) {
			monitorName = m.recordingSetup.monitors[m.recordingSetup.form.State.SelectedMonitor].Name
		}
		monitorResolution := ""
		for _, mon := range m.monitors {
			if mon.Name == monitorName {
				monitorResolution = fmt.Sprintf("%dx%d", mon.Width, mon.Height)
				break
			}
		}

		// Create RecordingInfo and save initial metadata
		m.recordingInfo = models.NewRecordingInfo(m.metadata, monitorName, monitorResolution)
		m.recordingInfo.Files.FolderPath = m.outputDir

		// Set recording settings from setup form
		if m.recordingSetup != nil && m.recordingSetup.form != nil {
			logoSelection := m.recordingSetup.GetLogoSelection()

			m.recordingInfo.Settings.ScreenEnabled = m.recordingSetup.form.State.RecordScreen
			m.recordingInfo.Settings.AudioEnabled = m.recordingSetup.form.State.RecordAudio
			m.recordingInfo.Settings.WebcamEnabled = m.recordingSetup.form.State.RecordWebcam
			m.recordingInfo.Settings.VerticalEnabled = m.recordingSetup.form.State.VerticalVideo && m.recordingSetup.form.State.RecordWebcam && m.recordingSetup.form.State.RecordScreen
			m.recordingInfo.Settings.LeftSplitEnabled = m.recordingSetup.form.State.LeftSplit
			m.recordingInfo.Settings.LogosEnabled = m.recordingSetup.form.State.AddLogos

			// Logo details
			m.recordingInfo.Settings.LeftLogo = logoSelection.LeftLogo
			m.recordingInfo.Settings.RightLogo = logoSelection.RightLogo
			m.recordingInfo.Settings.BottomLogo = logoSelection.BottomLogo
			m.recordingInfo.Settings.TitleColor = logoSelection.TitleColor
			m.recordingInfo.Settings.GifLoopMode = string(logoSelection.GifLoopMode)

			// Save background color from global config
			cfg, _ := config.Load()
			if cfg != nil && cfg.BgColor != "" {
				m.recordingInfo.Settings.BgColor = cfg.BgColor
			}
		}

		// Save initial recording.json
		if err := m.recordingInfo.Save(); err != nil {
			m.err = fmt.Errorf("failed to save recording metadata: %w", err)
			m.state = stateReady
			m.screen = ScreenMenu
			return m, nil
		}

		// Set up recorder options
		opts := recorder.Options{
			OutputDir:      m.outputDir,
			Monitor:        monitorName,
			Metadata:       &m.metadata,
			RecordingInfo:  m.recordingInfo,
			CreateVertical: m.recordingSetup != nil && m.recordingSetup.form != nil && m.recordingSetup.form.State.VerticalVideo,
		}

		// Set audio/webcam/screen options from setup
		if m.recordingSetup != nil && m.recordingSetup.form != nil {
			opts.NoAudio = !m.recordingSetup.form.State.RecordAudio
			opts.NoWebcam = !m.recordingSetup.form.State.RecordWebcam
			opts.NoScreen = !m.recordingSetup.form.State.RecordScreen
			// Set logo selection and save for future recordings
			opts.LogoSelection = m.recordingSetup.GetLogoSelection()
			_ = m.recordingSetup.SaveLogoSelection() // Save for next time
		}

		if err := m.recorder.StartWithOptions(opts); err != nil {
			m.err = err
			m.state = stateReady
			m.screen = ScreenMenu
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
}

// handleRoomNoiseTick handles the room noise capture countdown
func (m AppModel) handleRoomNoiseTick() (tea.Model, tea.Cmd) {
	if m.state != stateRoomNoise {
		return m, nil
	}

	m.roomNoiseCountdown--

	if m.roomNoiseCountdown <= 0 {
		// Room noise capture finished, stop the recorder
		if m.roomNoiseRecorder != nil {
			err := m.roomNoiseRecorder.Stop()
			m.roomNoiseRecorder = nil
			return m, func() tea.Msg {
				return roomNoiseCompleteMsg{err: err}
			}
		}
		return m.transitionToProcessing()
	}

	// Continue countdown
	return m, roomNoiseTickCmd()
}

// stopAndProcess stops recording and runs post-processing with progress updates
func (m AppModel) stopAndProcess() tea.Cmd {
	return func() tea.Msg {
		if err := m.recorder.Stop(); err != nil {
			return processingErrorMsg{Error: err}
		}
		// Step 0 (stopping recorders) is complete
		return processingStepMsg{Step: 0, Completed: true}
	}
}

// runProcessingPipeline starts the processing pipeline directly (used after room noise capture)
func (m *AppModel) runProcessingPipeline() tea.Cmd {
	return func() tea.Msg {
		// Set the room noise file on the recorder for audio processing
		if m.roomNoiseFile != "" && m.recorder != nil {
			m.recorder.SetRoomNoiseFile(m.roomNoiseFile)
		}

		// Create progress channel and start processing
		m.progressChan = make(chan recorder.ProgressUpdate, 100)
		go m.recorder.ProcessWithProgress(m.progressChan)

		// Wait for first update
		update, ok := <-m.progressChan
		if !ok {
			return processingCompleteMsg{}
		}

		if update.Percent >= 0 && !update.Completed && !update.Skipped && update.Error == nil {
			return processingPercentMsg{
				Step:    update.Step,
				Percent: update.Percent,
			}
		}

		return processingStepMsg{
			Step:      update.Step,
			Completed: update.Completed,
			Skipped:   update.Skipped,
			Error:     update.Error,
		}
	}
}

// waitForProgressUpdate waits for the next progress update from the channel
func waitForProgressUpdate(ch chan recorder.ProgressUpdate) tea.Cmd {
	if ch == nil {
		return nil
	}
	return func() tea.Msg {
		update, ok := <-ch
		if !ok {
			// Channel closed, processing complete
			return processingCompleteMsg{}
		}

		// Check if this is a percent update (no status change, just progress)
		if update.Percent >= 0 && !update.Completed && !update.Skipped && update.Error == nil {
			return processingPercentMsg{
				Step:    update.Step,
				Percent: update.Percent,
			}
		}

		// Return a step message
		return processingStepMsg{
			Step:      update.Step,
			Completed: update.Completed,
			Skipped:   update.Skipped,
			Error:     update.Error,
		}
	}
}

// View renders the current screen
func (m AppModel) View() string {
	if m.width == 0 || m.height == 0 {
		return ""
	}

	// Show countdown screen if in countdown state
	if m.state == stateCountdown {
		return m.renderCountdownView()
	}

	// Show room noise capture screen
	if m.state == stateRoomNoise {
		return m.renderRoomNoiseView()
	}

	// Show processing screen if in processing state
	if m.state == stateProcessing {
		cfg, _ := config.Load()
		youtubeConnected := cfg.IsYouTubeConnected()
		return RenderProcessingView(m.processing, m.width, m.height, m.processingFrame, m.processingBtn, youtubeConnected, m.recordingInfo)
	}

	// Render based on current screen
	switch m.screen {
	case ScreenMenu:
		return m.menu.View()
	case ScreenRecordingSetup:
		return m.renderRecordingSetupScreen()
	case ScreenRecording:
		return m.renderRecordingScreen()
	case ScreenHistory:
		return m.renderHistoryScreen()
	case ScreenOptions:
		return m.renderOptionsScreen()
	case ScreenYouTubeSetup:
		return m.renderYouTubeSetupScreen()
	case ScreenYouTubeUpload:
		return m.renderYouTubeUploadScreen()
	case ScreenSyndicationSetup:
		return m.renderSyndicationSetupScreen()
	case ScreenSyndicationPost:
		return m.renderSyndicationPostScreen()
	}

	return ""
}

// renderRecordingScreen renders the recording screen
func (m AppModel) renderRecordingScreen() string {
	// Update global app state for header
	status := "Ready"
	if m.status.IsRecording {
		status = "Recording"
	} else if m.isPaused {
		status = "Paused"
	}
	updateGlobalAppState(m.status.IsRecording, m.blinkOn, status)

	// Render header
	screenTitle := "Recording"
	if m.isPaused {
		screenTitle = "Paused"
	}
	header := RenderHeader(screenTitle)

	// Render main content with ASCII art
	content := m.renderRecordingContent("")

	// Render footer
	var helpText string
	if m.status.IsRecording || m.isPaused {
		helpText = "←/→: select • space/enter: activate • p: pause/resume • s: stop • q: quit"
	} else {
		helpText = "esc: back to menu • q: quit"
	}
	footer := RenderHelpFooter(helpText, m.width)

	return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
}

// renderRecordingContent renders the main content for the recording screen
func (m AppModel) renderRecordingContent(cursorMonitor string) string {
	var sections []string

	// Choose and render the appropriate ASCII art icon
	var iconLines []string
	var iconColor lipgloss.Color

	if m.isPausing {
		// Show pause icon in gray while pausing
		iconLines = bigPause
		iconColor = ColorGray
	} else if m.isResuming {
		// Show camera icon in gray while resuming
		iconLines = bigCamera
		iconColor = ColorGray
	} else if m.isPaused {
		// Show pause icon in amber
		iconLines = bigPause
		iconColor = ColorOrange
	} else if m.status.IsRecording {
		// Show camera icon in red (solid, no blinking)
		iconLines = bigCamera
		iconColor = ColorRed
	} else {
		// Not recording, not paused - show camera in gray
		iconLines = bigCamera
		iconColor = ColorGray
	}

	iconStyle := lipgloss.NewStyle().
		Foreground(iconColor).
		Bold(true)

	var iconDisplay string
	for i, line := range iconLines {
		iconDisplay += iconStyle.Render(line)
		if i < len(iconLines)-1 {
			iconDisplay += "\n"
		}
	}

	sections = append(sections, iconDisplay)

	// Show status text below icon
	if m.isPausing {
		// Show PAUSING text
		pausingStyle := lipgloss.NewStyle().
			Foreground(ColorGray).
			Bold(true)
		pausingText := pausingStyle.Render("⏳ PAUSING...")
		sections = append(sections, "", pausingText)
	} else if m.isResuming {
		// Show RESUMING text
		resumingStyle := lipgloss.NewStyle().
			Foreground(ColorGray).
			Bold(true)
		resumingText := resumingStyle.Render("⏳ RESUMING...")
		sections = append(sections, "", resumingText)
	} else if m.status.IsRecording {
		// Add REC text below camera when recording (solid, no blinking)
		recStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Bold(true)
		var recDisplay string
		for i, line := range bigREC {
			recDisplay += recStyle.Render(line)
			if i < len(bigREC)-1 {
				recDisplay += "\n"
			}
		}
		sections = append(sections, "", recDisplay)
	} else if m.isPaused {
		// Show PAUSED text in amber
		pausedStyle := lipgloss.NewStyle().
			Foreground(ColorOrange).
			Bold(true)
		pausedText := pausedStyle.Render("▶ PAUSED - press p to resume")
		sections = append(sections, "", pausedText)
	}

	// Add duration display
	if m.status.IsRecording || m.isPaused {
		duration := time.Since(m.status.StartTime).Round(time.Second)
		durationStyle := lipgloss.NewStyle().
			Foreground(ColorWhite).
			Bold(true)
		durationText := durationStyle.Render(fmt.Sprintf("Duration: %s", duration))
		if m.status.CurrentPart > 0 {
			durationText += lipgloss.NewStyle().
				Foreground(ColorGray).
				Render(fmt.Sprintf("  (Part %d)", m.status.CurrentPart+1))
		}
		sections = append(sections, "", durationText)
	}

	// Render Pause and Stop buttons
	sections = append(sections, "", m.renderRecordingButtons())

	// Show output directory path
	if m.outputDir != "" {
		pathStyle := lipgloss.NewStyle().
			Foreground(ColorGray).
			Italic(true)
		pathText := pathStyle.Render("Output: " + m.outputDir)
		sections = append(sections, "", pathText)
	}

	// Help content (if shown)
	if m.showHelp {
		sections = append(sections, "", m.renderHelp())
	}

	// Combine content
	contentStyle := lipgloss.NewStyle().
		Width(HeaderWidth).
		Align(lipgloss.Center)

	content := lipgloss.JoinVertical(lipgloss.Center, sections...)
	return contentStyle.Render(content)
}

// renderRecordingButtons renders the Pause and Stop buttons
func (m AppModel) renderRecordingButtons() string {
	// Button styles
	normalStyle := lipgloss.NewStyle().
		Padding(0, 3).
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorGray)

	selectedStyle := lipgloss.NewStyle().
		Padding(0, 3).
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorBlue).
		Bold(true)

	// Pause button
	pauseLabel := "⏸ Pause"
	if m.isPaused {
		pauseLabel = "▶ Resume"
	}
	pauseStyle := normalStyle
	if m.selectedButton == ButtonPause {
		pauseStyle = selectedStyle
		if m.isPaused {
			pauseStyle = pauseStyle.Foreground(ColorGreen)
		} else {
			pauseStyle = pauseStyle.Foreground(ColorOrange)
		}
	} else {
		if m.isPaused {
			pauseStyle = pauseStyle.Foreground(ColorGreen)
		} else {
			pauseStyle = pauseStyle.Foreground(ColorWhite)
		}
	}
	pauseBtn := pauseStyle.Render(pauseLabel)

	// Stop button
	stopStyle := normalStyle
	if m.selectedButton == ButtonStop {
		stopStyle = selectedStyle.Foreground(ColorRed)
	} else {
		stopStyle = stopStyle.Foreground(ColorWhite)
	}
	stopBtn := stopStyle.Render("⏹ Stop")

	// Join buttons horizontally with spacing
	return lipgloss.JoinHorizontal(lipgloss.Center, pauseBtn, "    ", stopBtn)
}

// renderHelp renders the help content
func (m AppModel) renderHelp() string {
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

// renderCountdownView renders the countdown screen
func (m AppModel) renderCountdownView() string {
	var bigText []string
	var color lipgloss.Color

	if m.countdownNum > 0 {
		bigText = getBigDigit(m.countdownNum)
		switch m.countdownNum {
		case 5, 4:
			color = ColorOrange
		case 3, 2:
			color = lipgloss.Color("#FF8C00")
		case 1:
			color = ColorRed
		}
	} else {
		bigText = bigGO
		color = ColorGreen
	}

	digitStyle := lipgloss.NewStyle().
		Foreground(color).
		Bold(true)

	var lines string
	for i, line := range bigText {
		lines += digitStyle.Render(line)
		if i < len(bigText)-1 {
			lines += "\n"
		}
	}

	subtitleStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	var subtitle string
	if m.countdownNum > 0 {
		subtitle = subtitleStyle.Render("Get ready... Recording starts soon!")
	} else {
		subtitle = subtitleStyle.Render("Recording!")
	}

	hintStyle := lipgloss.NewStyle().
		Foreground(ColorGray)
	hint := hintStyle.Render("esc: cancel")

	content := lipgloss.JoinVertical(
		lipgloss.Center,
		"",
		lines,
		"",
		subtitle,
		"",
		hint,
	)

	return lipgloss.Place(
		m.width,
		m.height,
		lipgloss.Center,
		lipgloss.Center,
		content,
	)
}

// renderRoomNoiseView renders the room noise capture screen
func (m AppModel) renderRoomNoiseView() string {
	// Title
	titleStyle := lipgloss.NewStyle().
		Foreground(ColorBlue).
		Bold(true).
		MarginBottom(1)

	title := titleStyle.Render("Capturing Room Noise")

	// Large countdown number
	bigText := getBigDigit(m.roomNoiseCountdown)
	digitStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	var countdownDisplay string
	for i, line := range bigText {
		countdownDisplay += digitStyle.Render(line)
		if i < len(bigText)-1 {
			countdownDisplay += "\n"
		}
	}

	// Progress bar
	progressWidth := 40
	filled := (30 - m.roomNoiseCountdown) * progressWidth / 30
	if filled < 0 {
		filled = 0
	}
	if filled > progressWidth {
		filled = progressWidth
	}
	empty := progressWidth - filled

	progressStyle := lipgloss.NewStyle().Foreground(ColorGreen)
	emptyStyle := lipgloss.NewStyle().Foreground(ColorGray)
	progressBar := progressStyle.Render(strings.Repeat("█", filled)) + emptyStyle.Render(strings.Repeat("░", empty))

	// Instructions box
	boxStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorOrange).
		Padding(1, 2).
		Width(50)

	instructions := lipgloss.JoinVertical(
		lipgloss.Left,
		lipgloss.NewStyle().Bold(true).Foreground(ColorWhite).Render("Please remain quiet"),
		"",
		lipgloss.NewStyle().Foreground(ColorGray).Render("We're recording the ambient room noise to"),
		lipgloss.NewStyle().Foreground(ColorGray).Render("calibrate the audio processing. This helps"),
		lipgloss.NewStyle().Foreground(ColorGray).Render("remove background noise from your recording."),
		"",
		lipgloss.NewStyle().Foreground(ColorGray).Italic(true).Render("Do not speak or make any sounds."),
	)

	instructionBox := boxStyle.Render(instructions)

	// Status
	statusStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)
	status := statusStyle.Render(fmt.Sprintf("%d seconds remaining...", m.roomNoiseCountdown))

	// Combine all elements
	content := lipgloss.JoinVertical(
		lipgloss.Center,
		"",
		title,
		"",
		countdownDisplay,
		"",
		progressBar,
		"",
		instructionBox,
		"",
		status,
	)

	return lipgloss.Place(
		m.width,
		m.height,
		lipgloss.Center,
		lipgloss.Center,
		content,
	)
}

// renderRecordingSetupScreen renders the recording setup screen
func (m AppModel) renderRecordingSetupScreen() string {
	header := RenderHeader("New Recording")

	// Render the setup form (already wrapped in container)
	content := m.recordingSetup.View()

	footer := RenderHelpFooter("tab/↓: next • shift+tab/↑: prev • ←/→: select • enter: confirm • esc: back", m.width)

	return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
}

// renderHistoryScreen renders the history screen
func (m AppModel) renderHistoryScreen() string {
	if m.history == nil {
		return "Loading..."
	}
	return m.history.View()
}

// renderOptionsScreen renders the options screen
func (m AppModel) renderOptionsScreen() string {
	// If file browser is active, it takes over the full screen
	if m.options.IsFileBrowserActive() {
		return m.options.RenderFileBrowser(m.width, m.height)
	}

	header := RenderHeader("Options")

	content := lipgloss.NewStyle().
		Width(HeaderWidth).
		Align(lipgloss.Center).
		Render(m.options.View())

	footer := RenderHelpFooter("tab/↓: next • shift+tab/↑: prev • enter: select • esc: back", m.width)

	return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
}

// handleYouTubeSetupKeys handles keys on the YouTube setup screen
func (m AppModel) handleYouTubeSetupKeys(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	// Update the YouTube setup model
	newYouTubeSetup, cmd := m.youtubeSetup.Update(msg)
	m.youtubeSetup = newYouTubeSetup
	return m, cmd
}

// renderYouTubeSetupScreen renders the YouTube setup screen
func (m AppModel) renderYouTubeSetupScreen() string {
	m.youtubeSetup.width = m.width
	m.youtubeSetup.height = m.height
	return m.youtubeSetup.View()
}

// handleYouTubeUploadKeys handles keys on the YouTube upload screen
func (m AppModel) handleYouTubeUploadKeys(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	// Update the YouTube upload model
	newYouTubeUpload, cmd := m.youtubeUpload.Update(msg)
	m.youtubeUpload = newYouTubeUpload
	return m, cmd
}

// renderYouTubeUploadScreen renders the YouTube upload screen
func (m AppModel) renderYouTubeUploadScreen() string {
	if m.youtubeUpload == nil {
		return ""
	}
	m.youtubeUpload.width = m.width
	m.youtubeUpload.height = m.height
	return m.youtubeUpload.View()
}

// handleSyndicationSetupKeys handles keys on the syndication setup screen
func (m AppModel) handleSyndicationSetupKeys(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	newSetup, cmd := m.syndicationSetup.Update(msg)
	m.syndicationSetup = newSetup

	// Check for back to menu message
	if msg.String() == "q" && m.syndicationSetup.step == SyndicationStepPlatformList {
		m.screen = ScreenOptions
		m.options = NewOptionsModel()
		m.options.width = m.width
		m.options.height = m.height
		return m, m.options.Init()
	}

	return m, cmd
}

// renderSyndicationSetupScreen renders the syndication setup screen
func (m AppModel) renderSyndicationSetupScreen() string {
	if m.syndicationSetup == nil {
		return ""
	}
	m.syndicationSetup.width = m.width
	m.syndicationSetup.height = m.height
	return m.syndicationSetup.View()
}

// handleSyndicationPostKeys handles keys on the syndication post screen
func (m AppModel) handleSyndicationPostKeys(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	newPost, cmd := m.syndicationPost.Update(msg)
	m.syndicationPost = newPost
	return m, cmd
}

// renderSyndicationPostScreen renders the syndication post screen
func (m AppModel) renderSyndicationPostScreen() string {
	if m.syndicationPost == nil {
		return ""
	}
	m.syndicationPost.width = m.width
	m.syndicationPost.height = m.height
	return m.syndicationPost.View()
}
