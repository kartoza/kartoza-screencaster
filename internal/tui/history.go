package tui

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
	"github.com/kartoza/kartoza-screencaster/internal/config"
	"github.com/kartoza/kartoza-screencaster/internal/models"
	"github.com/kartoza/kartoza-screencaster/internal/monitor"
	"github.com/kartoza/kartoza-screencaster/internal/youtube"
)

// HistoryViewMode represents the current mode of the history view
type HistoryViewMode int

const (
	HistoryListMode HistoryViewMode = iota
	HistoryDetailMode
	HistoryEditMode
	HistoryDeleteConfirmMode
	HistoryYouTubePrivacyMode
	HistoryYouTubeDeleteConfirmMode
	HistoryYouTubeUploadMode
	HistoryReprocessConfirmMode
	HistoryErrorDetailMode
)

// HistoryModel displays recording history with navigation
type HistoryModel struct {
	width  int
	height int

	// Data
	recordings []models.RecordingInfo

	// Scrolling - cursor is absolute position in recordings
	cursor int

	// View mode
	mode HistoryViewMode

	// Detail/Edit view state
	selectedRecording *models.RecordingInfo
	editForm          *RecordingForm
	topics            []models.Topic
	isSaving          bool

	// State
	err     error
	loading bool

	// Delete confirmation state
	deleteConfirmRecording *models.RecordingInfo
	deleteError            string

	// YouTube action state
	youtubePrivacyOptions  []string
	youtubeSelectedPrivacy int
	youtubeActionError     string
	youtubeActionSuccess   string
	youtubeActionLoading   bool

	// Error detail view scroll position
	errorViewScrollOffset int

	// When true, automatically navigate to edit the latest needs_metadata recording on load
	editRecordingOnLoad bool
}

// NewHistoryModel creates a new history model
func NewHistoryModel() *HistoryModel {
	// Load topics
	cfg, _ := config.Load()
	topics := cfg.Topics
	if len(topics) == 0 {
		topics = models.DefaultTopics()
	}

	h := &HistoryModel{
		cursor:                0,
		loading:               true,
		mode:                  HistoryListMode,
		topics:                topics,
		youtubePrivacyOptions: []string{"unlisted", "private", "public"},
	}

	return h
}

// Init initializes the history view
func (h *HistoryModel) Init() tea.Cmd {
	return h.loadRecordings()
}

// getVisibleCount returns how many entries can fit on screen
func (h *HistoryModel) getVisibleCount() int {
	availableHeight := h.height - 12
	if availableHeight < 3 {
		return 1
	}
	count := availableHeight / 3
	if count < 1 {
		count = 1
	}
	if count > 12 {
		count = 12
	}
	return count
}

// detailContainerWidth returns the responsive width for detail view containers
func (h *HistoryModel) detailContainerWidth() int {
	// Container width is terminal width minus padding, capped at reasonable size
	containerWidth := h.width - 10
	if containerWidth > 80 {
		containerWidth = 80
	}
	if containerWidth < 50 {
		containerWidth = 50
	}
	return containerWidth
}

// detailInternalWidth returns the width for content inside detail containers
func (h *HistoryModel) detailInternalWidth() int {
	// Internal width is container width minus border/padding (8px for borders + padding)
	iw := h.detailContainerWidth() - 8
	if iw < 40 {
		iw = 40
	}
	return iw
}

// Update handles messages
func (h *HistoryModel) Update(msg tea.Msg) (*HistoryModel, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		h.width = msg.Width
		h.height = msg.Height
		// Update edit form size if it exists
		if h.editForm != nil {
			contentHeight := h.height - 8
			if contentHeight < 10 {
				contentHeight = 10
			}
			h.editForm.SetSize(h.width, contentHeight)
		}

	case tea.KeyMsg:
		switch h.mode {
		case HistoryListMode:
			return h.updateListMode(msg)
		case HistoryDetailMode:
			return h.updateDetailMode(msg)
		case HistoryEditMode:
			return h.updateEditMode(msg)
		case HistoryDeleteConfirmMode:
			return h.updateDeleteConfirmMode(msg)
		case HistoryYouTubePrivacyMode:
			return h.updateYouTubePrivacyMode(msg)
		case HistoryYouTubeDeleteConfirmMode:
			return h.updateYouTubeDeleteConfirmMode(msg)
		case HistoryReprocessConfirmMode:
			return h.updateReprocessConfirmMode(msg)
		case HistoryErrorDetailMode:
			return h.updateErrorDetailMode(msg)
		}

	case recordingsLoadedMsg:
		h.loading = false
		h.recordings = msg.recordings
		h.err = msg.err

		// If edit-recording mode, find and open the latest needs_metadata recording
		if h.editRecordingOnLoad && msg.err == nil && len(msg.recordings) > 0 {
			h.editRecordingOnLoad = false
			for i, rec := range h.recordings {
				if rec.Status == models.StatusNeedsMetadata {
					h.cursor = i
					h.selectedRecording = &h.recordings[i]
					h.mode = HistoryEditMode
					h.initEditForm()
					return h, textinput.Blink
				}
			}
		}

	case recordingSavedMsg:
		h.isSaving = false
		if h.editForm != nil {
			h.editForm.State.IsSaving = false
		}
		if msg.err != nil {
			if h.editForm != nil {
				h.editForm.State.ErrorMsg = msg.err.Error()
			}
		} else {
			if h.editForm != nil {
				h.editForm.State.SuccessMsg = "Recording saved successfully!"
			}
			// Update local copy
			if h.selectedRecording != nil {
				for i := range h.recordings {
					if h.recordings[i].Files.FolderPath == h.selectedRecording.Files.FolderPath {
						h.recordings[i] = *h.selectedRecording
						break
					}
				}
			}
		}

	case youtubePrivacyChangedMsg:
		h.youtubeActionLoading = false
		if msg.err != nil {
			h.youtubeActionError = msg.err.Error()
		} else {
			h.youtubeActionSuccess = "Privacy updated to " + msg.newPrivacy
			// Update local metadata
			if h.selectedRecording != nil && h.selectedRecording.Metadata.YouTube != nil {
				h.selectedRecording.Metadata.YouTube.Privacy = msg.newPrivacy
				_ = h.selectedRecording.Save()
				// Update in list
				for i := range h.recordings {
					if h.recordings[i].Files.FolderPath == h.selectedRecording.Files.FolderPath {
						h.recordings[i] = *h.selectedRecording
						break
					}
				}
			}
			h.mode = HistoryDetailMode
		}

	case youtubeVideoDeletedMsg:
		h.youtubeActionLoading = false
		if msg.err != nil {
			h.youtubeActionError = msg.err.Error()
		} else {
			h.youtubeActionSuccess = "Video deleted from YouTube"
			// Clear YouTube metadata
			if h.selectedRecording != nil {
				h.selectedRecording.Metadata.YouTube = nil
				_ = h.selectedRecording.Save()
				// Update in list
				for i := range h.recordings {
					if h.recordings[i].Files.FolderPath == h.selectedRecording.Files.FolderPath {
						h.recordings[i] = *h.selectedRecording
						break
					}
				}
			}
			h.mode = HistoryDetailMode
		}

	case startYouTubeUploadMsg:
		// This is handled by the parent app model
		return h, func() tea.Msg { return msg }

	default:
		// Forward other messages (like cursor blink) to edit form when in edit mode
		if h.mode == HistoryEditMode && h.editForm != nil {
			var cmd tea.Cmd
			h.editForm, cmd = h.editForm.Update(msg)
			return h, cmd
		}
	}

	return h, nil
}

// updateListMode handles input in list mode
func (h *HistoryModel) updateListMode(msg tea.KeyMsg) (*HistoryModel, tea.Cmd) {
	switch msg.String() {
	case "ctrl+c":
		return h, tea.Quit

	case "esc", "q":
		return h, func() tea.Msg { return backToMenuMsg{} }

	case "up", "k":
		if h.cursor > 0 {
			h.cursor--
		}

	case "down", "j":
		if h.cursor < len(h.recordings)-1 {
			h.cursor++
		}

	case "home", "g":
		h.cursor = 0

	case "end", "G":
		if len(h.recordings) > 0 {
			h.cursor = len(h.recordings) - 1
		}

	case "pgup":
		h.cursor -= h.getVisibleCount()
		if h.cursor < 0 {
			h.cursor = 0
		}

	case "pgdown":
		h.cursor += h.getVisibleCount()
		if h.cursor >= len(h.recordings) {
			h.cursor = len(h.recordings) - 1
		}
		if h.cursor < 0 {
			h.cursor = 0
		}

	case "enter", " ":
		// Open detail view (or edit mode if recording needs metadata)
		if len(h.recordings) > 0 && h.cursor < len(h.recordings) {
			rec := h.recordings[h.cursor]
			h.selectedRecording = &rec

			// If recording needs metadata, go directly to edit mode
			if rec.Status == models.StatusNeedsMetadata {
				h.mode = HistoryEditMode
				h.initEditForm()
				return h, textinput.Blink
			}

			h.mode = HistoryDetailMode
		}

	case "r":
		h.loading = true
		h.cursor = 0
		return h, h.loadRecordings()

	case "d":
		// Delete selected recording (with confirmation)
		if len(h.recordings) > 0 && h.cursor < len(h.recordings) {
			rec := h.recordings[h.cursor]
			h.deleteConfirmRecording = &rec
			h.deleteError = ""
			h.mode = HistoryDeleteConfirmMode
		}
	}

	return h, nil
}

// updateDeleteConfirmMode handles input in delete confirmation mode
func (h *HistoryModel) updateDeleteConfirmMode(msg tea.KeyMsg) (*HistoryModel, tea.Cmd) {
	switch msg.String() {
	case "ctrl+c":
		return h, tea.Quit

	case "esc", "n", "N":
		// Cancel deletion
		h.mode = HistoryListMode
		h.deleteConfirmRecording = nil
		h.deleteError = ""

	case "y", "Y":
		// Confirm deletion
		if h.deleteConfirmRecording != nil {
			folderPath := h.deleteConfirmRecording.Files.FolderPath
			err := os.RemoveAll(folderPath)
			if err != nil {
				h.deleteError = fmt.Sprintf("Failed to delete: %v", err)
				return h, nil
			}

			// Remove from list
			for i := range h.recordings {
				if h.recordings[i].Files.FolderPath == folderPath {
					h.recordings = append(h.recordings[:i], h.recordings[i+1:]...)
					break
				}
			}

			// Adjust cursor if needed
			if h.cursor >= len(h.recordings) && h.cursor > 0 {
				h.cursor--
			}

			// Return to list mode
			h.mode = HistoryListMode
			h.deleteConfirmRecording = nil
			h.deleteError = ""

			// Update global recording count
			updateGlobalAppState(GlobalAppState.IsRecording, GlobalAppState.BlinkOn, GlobalAppState.Status)
		}
	}

	return h, nil
}

// updateDetailMode handles input in detail view mode
func (h *HistoryModel) updateDetailMode(msg tea.KeyMsg) (*HistoryModel, tea.Cmd) {
	switch msg.String() {
	case "ctrl+c":
		return h, tea.Quit

	case "esc", "q":
		// Go back to list
		h.mode = HistoryListMode
		h.selectedRecording = nil
		h.editForm = nil
		h.youtubeActionError = ""
		h.youtubeActionSuccess = ""

	case "e":
		// Enter edit mode
		if h.selectedRecording != nil {
			h.mode = HistoryEditMode
			h.initEditForm()
			return h, textinput.Blink
		}

	case "u":
		// Upload to YouTube (only if not already uploaded)
		if h.selectedRecording != nil && !h.selectedRecording.Metadata.IsPublishedToYouTube() {
			// Check if YouTube is connected
			cfg, _ := config.Load()
			if !cfg.IsYouTubeConnected() {
				h.youtubeActionError = "YouTube not connected. Go to Options > YouTube to set up."
				return h, nil
			}
			// Find video file to upload
			videoPath := h.selectedRecording.Files.MergedFile
			if videoPath == "" {
				videoPath = h.selectedRecording.Files.VideoFile
			}
			if videoPath == "" {
				h.youtubeActionError = "No video file found to upload"
				return h, nil
			}
			// Send message to parent to start upload
			return h, func() tea.Msg {
				return startYouTubeUploadMsg{
					recording: h.selectedRecording,
					videoPath: videoPath,
				}
			}
		}

	case "p":
		// Change privacy (only if already uploaded)
		if h.selectedRecording != nil && h.selectedRecording.Metadata.IsPublishedToYouTube() {
			h.mode = HistoryYouTubePrivacyMode
			h.youtubeActionError = ""
			h.youtubeActionSuccess = ""
			// Set current privacy as selected
			currentPrivacy := h.selectedRecording.Metadata.YouTube.Privacy
			for i, p := range h.youtubePrivacyOptions {
				if p == currentPrivacy {
					h.youtubeSelectedPrivacy = i
					break
				}
			}
		}

	case "x":
		// Delete from YouTube (only if already uploaded)
		if h.selectedRecording != nil && h.selectedRecording.Metadata.IsPublishedToYouTube() {
			h.mode = HistoryYouTubeDeleteConfirmMode
			h.youtubeActionError = ""
			h.youtubeActionSuccess = ""
		}

	case "r":
		// Reprocess recording (regenerate output with potentially different settings/logos)
		if h.selectedRecording != nil {
			h.mode = HistoryReprocessConfirmMode
			h.youtubeActionError = ""
			h.youtubeActionSuccess = ""
		}

	case "v":
		if h.selectedRecording != nil {
			if h.selectedRecording.Status == models.StatusFailed {
				// View full error details for failed recordings
				h.mode = HistoryErrorDetailMode
				h.errorViewScrollOffset = 0
			} else if h.selectedRecording.Status == models.StatusCompleted {
				// Play vertical video (preferred for preview)
				videoPath := h.selectedRecording.Files.VerticalFile
				if videoPath != "" {
					return h, h.openVideoInPlayer(videoPath)
				}
				// Fall back to merged if no vertical
				if h.selectedRecording.Files.MergedFile != "" {
					return h, h.openVideoInPlayer(h.selectedRecording.Files.MergedFile)
				}
			}
		}

	case "m":
		// Play merged video
		if h.selectedRecording != nil && h.selectedRecording.Status == models.StatusCompleted {
			videoPath := h.selectedRecording.Files.MergedFile
			if videoPath == "" {
				videoPath = h.selectedRecording.Files.VideoFile
			}
			if videoPath != "" {
				return h, h.openVideoInPlayer(videoPath)
			}
		}

	case "a":
		// Play normalized audio (or original if normalized doesn't exist)
		if h.selectedRecording != nil && h.selectedRecording.Status == models.StatusCompleted {
			// Try normalized audio first
			audioPath := h.selectedRecording.Files.AudioFile
			if audioPath != "" {
				// Construct normalized audio path
				normalizedPath := strings.TrimSuffix(audioPath, ".wav") + "-normalized.wav"
				if _, err := os.Stat(normalizedPath); err == nil {
					return h, h.openVideoInPlayer(normalizedPath)
				}
				// Fall back to original audio
				return h, h.openVideoInPlayer(audioPath)
			}
		}

	case "o":
		// Open work folder in file manager
		if h.selectedRecording != nil {
			folderPath := h.selectedRecording.Files.FolderPath
			if folderPath != "" {
				return h, h.openFolderInFileManager(folderPath)
			}
		}
	}

	return h, nil
}

// updateEditMode handles input in edit mode
func (h *HistoryModel) updateEditMode(msg tea.KeyMsg) (*HistoryModel, tea.Cmd) {
	if h.editForm == nil {
		return h, nil
	}

	switch msg.String() {
	case "ctrl+c":
		return h, tea.Quit

	case "esc":
		// If in input mode, let the form handle it first
		if h.editForm.State.InputMode {
			h.editForm, _ = h.editForm.Update(msg)
			return h, nil
		}
		// Go back to detail view
		h.mode = HistoryDetailMode
		h.editForm = nil
		return h, nil

	case "ctrl+s":
		// Save changes
		if !h.isSaving {
			return h, h.saveRecording()
		}
		return h, nil
	}

	// Delegate all other input to the form
	var cmd tea.Cmd
	h.editForm, cmd = h.editForm.Update(msg)
	return h, cmd
}

// updateYouTubePrivacyMode handles input in YouTube privacy change mode
func (h *HistoryModel) updateYouTubePrivacyMode(msg tea.KeyMsg) (*HistoryModel, tea.Cmd) {
	switch msg.String() {
	case "ctrl+c":
		return h, tea.Quit

	case "esc", "q":
		h.mode = HistoryDetailMode
		h.youtubeActionError = ""

	case "left", "h":
		h.youtubeSelectedPrivacy--
		if h.youtubeSelectedPrivacy < 0 {
			h.youtubeSelectedPrivacy = len(h.youtubePrivacyOptions) - 1
		}

	case "right", "l":
		h.youtubeSelectedPrivacy++
		if h.youtubeSelectedPrivacy >= len(h.youtubePrivacyOptions) {
			h.youtubeSelectedPrivacy = 0
		}

	case "enter":
		if h.selectedRecording != nil && h.selectedRecording.Metadata.YouTube != nil {
			newPrivacy := h.youtubePrivacyOptions[h.youtubeSelectedPrivacy]
			if newPrivacy != h.selectedRecording.Metadata.YouTube.Privacy {
				h.youtubeActionLoading = true
				return h, h.changeYouTubePrivacy(newPrivacy)
			}
			h.mode = HistoryDetailMode
		}
	}

	return h, nil
}

// updateYouTubeDeleteConfirmMode handles input in YouTube delete confirmation mode
func (h *HistoryModel) updateYouTubeDeleteConfirmMode(msg tea.KeyMsg) (*HistoryModel, tea.Cmd) {
	switch msg.String() {
	case "ctrl+c":
		return h, tea.Quit

	case "esc", "n", "N":
		h.mode = HistoryDetailMode
		h.youtubeActionError = ""

	case "y", "Y":
		if h.selectedRecording != nil && h.selectedRecording.Metadata.YouTube != nil {
			h.youtubeActionLoading = true
			return h, h.deleteFromYouTube()
		}
	}

	return h, nil
}

// updateReprocessConfirmMode handles input in reprocess confirmation mode
func (h *HistoryModel) updateReprocessConfirmMode(msg tea.KeyMsg) (*HistoryModel, tea.Cmd) {
	switch msg.String() {
	case "ctrl+c":
		return h, tea.Quit

	case "esc", "n", "N":
		h.mode = HistoryDetailMode
		h.youtubeActionError = ""

	case "y", "Y":
		if h.selectedRecording != nil {
			// Send message to parent to start reprocessing
			return h, func() tea.Msg {
				return startReprocessMsg{
					recording: h.selectedRecording,
				}
			}
		}
	}

	return h, nil
}

// updateErrorDetailMode handles input in error detail view mode
func (h *HistoryModel) updateErrorDetailMode(msg tea.KeyMsg) (*HistoryModel, tea.Cmd) {
	switch msg.String() {
	case "ctrl+c":
		return h, tea.Quit

	case "esc", "q":
		h.mode = HistoryDetailMode
		h.errorViewScrollOffset = 0

	case "up", "k":
		if h.errorViewScrollOffset > 0 {
			h.errorViewScrollOffset--
		}

	case "down", "j":
		h.errorViewScrollOffset++

	case "pgup":
		h.errorViewScrollOffset -= 10
		if h.errorViewScrollOffset < 0 {
			h.errorViewScrollOffset = 0
		}

	case "pgdown":
		h.errorViewScrollOffset += 10

	case "home", "g":
		h.errorViewScrollOffset = 0

	case "r":
		// Reprocess from error view
		if h.selectedRecording != nil {
			h.mode = HistoryReprocessConfirmMode
		}
	}

	return h, nil
}

// changeYouTubePrivacy changes the privacy setting of a YouTube video
func (h *HistoryModel) changeYouTubePrivacy(newPrivacy string) tea.Cmd {
	rec := h.selectedRecording
	return func() tea.Msg {
		ctx := context.Background()
		cfg, err := config.Load()
		if err != nil {
			return youtubePrivacyChangedMsg{err: err}
		}

		// Find the account that matches the video's channel ID
		var clientID, clientSecret, accountID string
		if rec.Metadata.YouTube != nil && rec.Metadata.YouTube.ChannelID != "" {
			if acc := cfg.YouTube.GetAccountByChannelID(rec.Metadata.YouTube.ChannelID); acc != nil {
				clientID = acc.ClientID
				clientSecret = acc.ClientSecret
				accountID = acc.ID
			}
		}
		// Fallback to last used account or legacy
		if clientID == "" {
			if acc := cfg.YouTube.GetLastUsedAccount(); acc != nil {
				clientID = acc.ClientID
				clientSecret = acc.ClientSecret
				accountID = acc.ID
			} else {
				clientID = cfg.YouTube.ClientID
				clientSecret = cfg.YouTube.ClientSecret
				accountID = "legacy"
			}
		}

		auth := youtube.NewAuthForAccount(clientID, clientSecret, config.GetConfigDir(), accountID)
		uploader, err := youtube.NewUploader(ctx, auth)
		if err != nil {
			return youtubePrivacyChangedMsg{err: err}
		}

		err = uploader.UpdateVideoPrivacy(ctx, rec.Metadata.YouTube.VideoID, youtube.PrivacyStatus(newPrivacy))
		if err != nil {
			return youtubePrivacyChangedMsg{err: err}
		}

		return youtubePrivacyChangedMsg{newPrivacy: newPrivacy}
	}
}

// deleteFromYouTube deletes the video from YouTube
func (h *HistoryModel) deleteFromYouTube() tea.Cmd {
	rec := h.selectedRecording
	return func() tea.Msg {
		ctx := context.Background()
		cfg, err := config.Load()
		if err != nil {
			return youtubeVideoDeletedMsg{err: err}
		}

		// Find the account that matches the video's channel ID
		var clientID, clientSecret, accountID string
		if rec.Metadata.YouTube != nil && rec.Metadata.YouTube.ChannelID != "" {
			if acc := cfg.YouTube.GetAccountByChannelID(rec.Metadata.YouTube.ChannelID); acc != nil {
				clientID = acc.ClientID
				clientSecret = acc.ClientSecret
				accountID = acc.ID
			}
		}
		// Fallback to last used account or legacy
		if clientID == "" {
			if acc := cfg.YouTube.GetLastUsedAccount(); acc != nil {
				clientID = acc.ClientID
				clientSecret = acc.ClientSecret
				accountID = acc.ID
			} else {
				clientID = cfg.YouTube.ClientID
				clientSecret = cfg.YouTube.ClientSecret
				accountID = "legacy"
			}
		}

		auth := youtube.NewAuthForAccount(clientID, clientSecret, config.GetConfigDir(), accountID)
		uploader, err := youtube.NewUploader(ctx, auth)
		if err != nil {
			return youtubeVideoDeletedMsg{err: err}
		}

		err = uploader.DeleteVideo(ctx, rec.Metadata.YouTube.VideoID)
		if err != nil {
			return youtubeVideoDeletedMsg{err: err}
		}

		return youtubeVideoDeletedMsg{}
	}
}

// openVideoInPlayer opens the video file in the system default video player
func (h *HistoryModel) openVideoInPlayer(videoPath string) tea.Cmd {
	return func() tea.Msg {
		// Use xdg-open on Linux to open with default application
		cmd := exec.Command("xdg-open", videoPath)
		_ = cmd.Start() // Don't wait for it to finish
		return videoOpenedMsg{}
	}
}

// openFolderInFileManager opens the folder in the system file manager
func (h *HistoryModel) openFolderInFileManager(folderPath string) tea.Cmd {
	return func() tea.Msg {
		var cmd *exec.Cmd
		switch runtime.GOOS {
		case "darwin":
			// macOS: use Finder via 'open' command
			cmd = exec.Command("open", folderPath)
		case "windows":
			// Windows: use Explorer
			cmd = exec.Command("explorer", folderPath)
		default:
			// Linux and others: use xdg-open (works with Nautilus, Dolphin, etc.)
			cmd = exec.Command("xdg-open", folderPath)
		}
		_ = cmd.Start() // Don't wait for it to finish
		return folderOpenedMsg{}
	}
}

// videoOpenedMsg indicates video player was launched
type videoOpenedMsg struct{}

// folderOpenedMsg indicates file manager was launched
type folderOpenedMsg struct{}

// initEditForm creates and populates the edit form from selected recording
func (h *HistoryModel) initEditForm() {
	if h.selectedRecording == nil {
		return
	}

	rec := h.selectedRecording
	cfg, _ := config.Load()

	// Load available monitors
	monitors, _ := monitor.ListMonitors()

	// Load available logos
	var logos []string
	if cfg.LogoDirectory != "" {
		if entries, err := os.ReadDir(cfg.LogoDirectory); err == nil {
			for _, entry := range entries {
				if entry.IsDir() {
					continue
				}
				ext := strings.ToLower(filepath.Ext(entry.Name()))
				if ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" {
					logos = append(logos, entry.Name())
				}
			}
		}
	}

	// Create the shared form in edit mode
	h.editForm = NewRecordingForm(&RecordingFormConfig{
		Mode:       FormModeEditExisting,
		FolderName: rec.Metadata.FolderName,
		Date:       rec.StartTime.Format("2006-01-02"),
		Duration:   models.FormatDuration(rec.Duration),
		Topics:     h.topics,
		Monitors:   monitors,
		Logos:      logos,
		OnConfirm:  nil, // Will be handled by ctrl+s
		OnCancel:   nil, // Will be handled by esc
	})

	// Populate with existing values
	h.editForm.SetTitle(rec.Metadata.Title)
	h.editForm.SetDescription(rec.Metadata.Description)
	h.editForm.SetPresenter(rec.Metadata.Presenter)
	h.editForm.SetSelectedTopic(rec.Metadata.Topic)

	// Set recording settings from existing recording
	h.editForm.State.RecordAudio = rec.Settings.AudioEnabled
	h.editForm.State.RecordWebcam = rec.Settings.WebcamEnabled
	h.editForm.State.RecordScreen = rec.Settings.ScreenEnabled
	h.editForm.State.VerticalVideo = rec.Settings.VerticalEnabled
	h.editForm.State.LeftSplit = rec.Settings.LeftSplitEnabled
	h.editForm.State.WebcamBubble = rec.Settings.WebcamBubbleEnabled
	h.editForm.State.AddLogos = rec.Settings.LogosEnabled

	// Set logo indices from existing settings
	if rec.Settings.LogosEnabled && cfg.LogoDirectory != "" {
		h.editForm.State.SelectedLeftIdx = findLogoIndex(logos, rec.Settings.LeftLogo)
		h.editForm.State.SelectedRightIdx = findLogoIndex(logos, rec.Settings.RightLogo)
		h.editForm.State.SelectedBottomIdx = findLogoIndex(logos, rec.Settings.BottomLogo)

		// Set color index
		for i, c := range config.TitleColors {
			if c == rec.Settings.TitleColor {
				h.editForm.State.SelectedColorIdx = i
				break
			}
		}

		// Set GIF loop mode index
		for i, mode := range config.GifLoopModes {
			if string(mode) == rec.Settings.GifLoopMode {
				h.editForm.State.SelectedGifLoopIdx = i
				break
			}
		}
	}

	// Set form size (account for header ~6 lines and footer ~2 lines)
	contentHeight := h.height - 8
	if contentHeight < 10 {
		contentHeight = 10
	}
	h.editForm.SetSize(h.width, contentHeight)

	// Focus the title field
	h.editForm.Focus()
}

// findLogoIndex finds the index of a logo in the logos slice (1-based, 0 = none)
func findLogoIndex(logos []string, logoPath string) int {
	if logoPath == "" {
		return 0
	}
	name := filepath.Base(logoPath)
	for i, logo := range logos {
		if logo == name {
			return i + 1 // +1 because 0 is "(none)"
		}
	}
	return 0
}

// resolveLogoPath converts a form logo index to a full file path
func (h *HistoryModel) resolveLogoPath(idx int) string {
	if h.editForm == nil || idx <= 0 || idx > len(h.editForm.Config.Logos) {
		return ""
	}
	cfg, _ := config.Load()
	if cfg == nil || cfg.LogoDirectory == "" {
		return ""
	}
	return filepath.Join(cfg.LogoDirectory, h.editForm.Config.Logos[idx-1])
}

// saveRecording saves the edited recording
func (h *HistoryModel) saveRecording() tea.Cmd {
	if h.selectedRecording == nil || h.editForm == nil {
		return nil
	}

	h.isSaving = true
	h.editForm.State.IsSaving = true
	h.editForm.State.ErrorMsg = ""
	h.editForm.State.SuccessMsg = ""

	// Check if this recording needs processing (was created via systray)
	needsProcessing := h.selectedRecording.Status == models.StatusNeedsMetadata

	// Update metadata from form
	h.selectedRecording.Metadata.Title = h.editForm.GetTitle()
	h.selectedRecording.Metadata.Description = h.editForm.GetDescription()
	h.selectedRecording.Metadata.Presenter = h.editForm.GetPresenter()
	h.selectedRecording.Metadata.Topic = h.editForm.GetSelectedTopic().Name
	if num, err := strconv.Atoi(h.editForm.GetNumber()); err == nil && num > 0 {
		h.selectedRecording.Metadata.Number = num
	}

	// Update recording settings from form
	h.selectedRecording.Settings.AudioEnabled = h.editForm.State.RecordAudio
	h.selectedRecording.Settings.WebcamEnabled = h.editForm.State.RecordWebcam
	h.selectedRecording.Settings.ScreenEnabled = h.editForm.State.RecordScreen
	h.selectedRecording.Settings.VerticalEnabled = h.editForm.State.VerticalVideo
	h.selectedRecording.Settings.LeftSplitEnabled = h.editForm.State.LeftSplit
	h.selectedRecording.Settings.WebcamBubbleEnabled = h.editForm.State.WebcamBubble
	h.selectedRecording.Settings.LogosEnabled = h.editForm.State.AddLogos
	h.selectedRecording.Settings.LeftLogo = h.resolveLogoPath(h.editForm.State.SelectedLeftIdx)
	h.selectedRecording.Settings.RightLogo = h.resolveLogoPath(h.editForm.State.SelectedRightIdx)
	h.selectedRecording.Settings.BottomLogo = h.resolveLogoPath(h.editForm.State.SelectedBottomIdx)
	if h.editForm.State.SelectedColorIdx >= 0 && h.editForm.State.SelectedColorIdx < len(config.TitleColors) {
		h.selectedRecording.Settings.TitleColor = config.TitleColors[h.editForm.State.SelectedColorIdx]
	}
	if h.editForm.State.SelectedGifLoopIdx >= 0 && h.editForm.State.SelectedGifLoopIdx < len(config.GifLoopModes) {
		h.selectedRecording.Settings.GifLoopMode = string(config.GifLoopModes[h.editForm.State.SelectedGifLoopIdx])
	}

	rec := h.selectedRecording
	return func() tea.Msg {
		// If this recording needs metadata (from systray), rename the folder first
		if needsProcessing && rec.Metadata.Title != "" {
			if _, err := rec.RenameFolder(); err != nil {
				// Log error but don't fail - folder rename is nice-to-have
				// The recording will still work with the original folder name
			}
		}

		err := rec.Save()
		if err == nil && needsProcessing {
			// Return a message to trigger processing
			return recordingSavedNeedsProcessingMsg{recording: rec}
		}
		return recordingSavedMsg{err: err}
	}
}

// View renders the history view
func (h *HistoryModel) View() string {
	if h.width == 0 {
		return "Loading..."
	}

	switch h.mode {
	case HistoryDetailMode:
		return h.renderDetailView()
	case HistoryEditMode:
		return h.renderEditView()
	case HistoryDeleteConfirmMode:
		return h.renderDeleteConfirmView()
	case HistoryYouTubePrivacyMode:
		return h.renderYouTubePrivacyView()
	case HistoryYouTubeDeleteConfirmMode:
		return h.renderYouTubeDeleteConfirmView()
	case HistoryReprocessConfirmMode:
		return h.renderReprocessConfirmView()
	case HistoryErrorDetailMode:
		return h.renderErrorDetailView()
	default:
		return h.renderListView()
	}
}

// renderListView renders the list mode view
func (h *HistoryModel) renderListView() string {
	header := RenderHeader("Recording History")

	if h.loading {
		loadingStyle := lipgloss.NewStyle().
			Foreground(ColorGray).
			Align(lipgloss.Center)

		mainContent := loadingStyle.Render("Loading recordings...")

		mainSection := lipgloss.JoinVertical(
			lipgloss.Center,
			header,
			"",
			mainContent,
		)

		centeredMain := lipgloss.Place(
			h.width,
			h.height-2,
			lipgloss.Center,
			lipgloss.Top,
			mainSection,
		)

		return centeredMain
	}

	if h.err != nil {
		errorStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Align(lipgloss.Center)

		mainContent := lipgloss.JoinVertical(
			lipgloss.Center,
			errorStyle.Render("Error: "+h.err.Error()),
		)

		mainSection := lipgloss.JoinVertical(
			lipgloss.Center,
			header,
			"",
			mainContent,
		)

		helpStyle := lipgloss.NewStyle().
			Width(h.width).
			Align(lipgloss.Center).
			Foreground(ColorGray).
			Italic(true)

		centeredMain := lipgloss.Place(
			h.width,
			h.height-2,
			lipgloss.Center,
			lipgloss.Top,
			mainSection,
		)

		return lipgloss.JoinVertical(
			lipgloss.Left,
			centeredMain,
			helpStyle.Render("r: retry • esc: back"),
		)
	}

	if len(h.recordings) == 0 {
		emptyStyle := lipgloss.NewStyle().
			Foreground(ColorGray).
			Align(lipgloss.Center)

		mainContent := emptyStyle.Render("No recordings found")

		mainSection := lipgloss.JoinVertical(
			lipgloss.Center,
			header,
			"",
			mainContent,
		)

		helpStyle := lipgloss.NewStyle().
			Width(h.width).
			Align(lipgloss.Center).
			Foreground(ColorGray).
			Italic(true)

		centeredMain := lipgloss.Place(
			h.width,
			h.height-2,
			lipgloss.Center,
			lipgloss.Top,
			mainSection,
		)

		return lipgloss.JoinVertical(
			lipgloss.Left,
			centeredMain,
			helpStyle.Render("esc: back"),
		)
	}

	// Position info
	positionInfo := fmt.Sprintf("Recording %d of %d", h.cursor+1, len(h.recordings))
	posStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Align(lipgloss.Center)

	table := h.renderScrollableTable()
	scrollBar := h.renderScrollBar()

	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	tableWithScroll := lipgloss.JoinHorizontal(lipgloss.Top, table, " ", scrollBar)

	infoLine := posStyle.Render(positionInfo)

	mainSection := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		infoLine,
		"",
		tableWithScroll,
	)

	centeredMain := lipgloss.Place(
		h.width,
		h.height-2,
		lipgloss.Center,
		lipgloss.Top,
		mainSection,
	)

	helpFooter := lipgloss.NewStyle().
		Width(h.width).
		Align(lipgloss.Center)

	helpText := "↑/↓: navigate • enter: view details • d: delete • r: refresh • esc/q: back"

	return lipgloss.JoinVertical(
		lipgloss.Left,
		centeredMain,
		helpFooter.Render(helpStyle.Render(helpText)),
	)
}

// renderDetailView renders the detail view for a selected recording
func (h *HistoryModel) renderDetailView() string {
	if h.selectedRecording == nil {
		return "No recording selected"
	}

	rec := h.selectedRecording
	header := RenderHeader("Recording Details")

	// Responsive widths
	containerWidth := h.detailContainerWidth()
	internalWidth := h.detailInternalWidth()

	// Styles
	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Width(14).
		Align(lipgloss.Right)

	valueStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Bold(true)

	highlightStyle := lipgloss.NewStyle().
		Foreground(ColorBlue).
		Bold(true)

	containerStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorOrange).
		Padding(1, 3).
		Width(containerWidth)

	dividerStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	// Build detail rows
	var rows []string

	// Folder badge
	folderBadge := lipgloss.NewStyle().
		Background(ColorBlue).
		Foreground(ColorWhite).
		Padding(0, 1).
		Bold(true).
		Render(rec.Metadata.FolderName)

	folderRow := lipgloss.NewStyle().Align(lipgloss.Center).Width(internalWidth).Render(folderBadge)
	rows = append(rows, folderRow)
	rows = append(rows, "")

	// Title
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		labelStyle.Render("Title:"),
		"  ",
		highlightStyle.Render(rec.Metadata.Title),
	))

	// Topic
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		labelStyle.Render("Topic:"),
		"  ",
		valueStyle.Render(rec.Metadata.Topic),
	))

	// Presenter
	if rec.Metadata.Presenter != "" {
		rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
			labelStyle.Render("Presenter:"),
			"  ",
			valueStyle.Render(rec.Metadata.Presenter),
		))
	}

	// Divider
	rows = append(rows, "")
	rows = append(rows, dividerStyle.Render(strings.Repeat("─", internalWidth)))
	rows = append(rows, "")

	// Date
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		labelStyle.Render("Date:"),
		"  ",
		valueStyle.Render(rec.StartTime.Format("Monday, January 2, 2006")),
	))

	// Duration
	durationStr := models.FormatDuration(rec.Duration)
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		labelStyle.Render("Duration:"),
		"  ",
		highlightStyle.Render(durationStr),
	))

	// Total size
	totalSize := models.FormatFileSize(rec.Files.TotalSize)
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		labelStyle.Render("Total Size:"),
		"  ",
		valueStyle.Render(totalSize),
	))

	// Divider
	rows = append(rows, "")
	rows = append(rows, dividerStyle.Render(strings.Repeat("─", internalWidth)))
	rows = append(rows, "")

	// Files section
	fileStyle := lipgloss.NewStyle().Foreground(ColorGray)
	if rec.Files.MergedFile != "" {
		rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
			labelStyle.Render("Merged:"),
			"  ",
			fileStyle.Render(filepath.Base(rec.Files.MergedFile)+" ("+models.FormatFileSize(rec.Files.MergedSize)+")"),
		))
	}
	if rec.Files.VerticalFile != "" {
		rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
			labelStyle.Render("Vertical:"),
			"  ",
			fileStyle.Render(filepath.Base(rec.Files.VerticalFile)+" ("+models.FormatFileSize(rec.Files.VerticalSize)+")"),
		))
	}

	// Divider
	rows = append(rows, "")
	rows = append(rows, dividerStyle.Render(strings.Repeat("─", internalWidth)))
	rows = append(rows, "")

	// Description
	rows = append(rows, labelStyle.Render("Description:"))
	desc := rec.Metadata.Description
	if desc == "" {
		desc = "(no description)"
	}
	descTextStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Width(internalWidth - 4).
		MarginLeft(2)
	rows = append(rows, descTextStyle.Render(desc))

	// Error section (shown only if status is failed)
	if rec.Status == models.StatusFailed {
		rows = append(rows, "")
		rows = append(rows, dividerStyle.Render(strings.Repeat("─", internalWidth)))
		rows = append(rows, "")

		// Error badge
		errorBadge := lipgloss.NewStyle().
			Background(ColorRed).
			Foreground(ColorWhite).
			Padding(0, 1).
			Bold(true).
			Render("✗ Processing Failed")
		errorBadgeRow := lipgloss.NewStyle().Align(lipgloss.Center).Width(internalWidth).Render(errorBadge)
		rows = append(rows, errorBadgeRow)
		rows = append(rows, "")

		// Show error summary
		if len(rec.Processing.Errors) > 0 {
			rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
				labelStyle.Render("Error:"),
				"  ",
				lipgloss.NewStyle().Foreground(ColorRed).Bold(true).Render(rec.Processing.Errors[0]),
			))
		}

		// Show error detail if available
		if rec.Processing.ErrorDetail != "" {
			rows = append(rows, "")
			rows = append(rows, labelStyle.Render("Details:"))
			errorDetailStyle := lipgloss.NewStyle().
				Foreground(ColorGray).
				Width(internalWidth - 4).
				MarginLeft(2)
			// Truncate for display, show first 300 chars
			detail := rec.Processing.ErrorDetail
			if len(detail) > 300 {
				detail = detail[:300] + "...\n(Press 'v' to view full error details)"
			}
			rows = append(rows, errorDetailStyle.Render(detail))
		}

		// Hint about viewing full details
		hintStyle := lipgloss.NewStyle().
			Foreground(ColorOrange).
			Italic(true).
			Align(lipgloss.Center).
			Width(internalWidth)
		rows = append(rows, "")
		rows = append(rows, hintStyle.Render("Press 'v' to view full error details and traceback"))
	}

	// YouTube section
	rows = append(rows, "")
	rows = append(rows, dividerStyle.Render(strings.Repeat("─", internalWidth)))
	rows = append(rows, "")

	ytLabelStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Width(14).
		Align(lipgloss.Right)

	if rec.Metadata.IsPublishedToYouTube() {
		// Show YouTube status
		ytStatusBadge := lipgloss.NewStyle().
			Background(ColorRed).
			Foreground(ColorWhite).
			Padding(0, 1).
			Bold(true).
			Render("▶ YouTube")
		ytStatusRow := lipgloss.NewStyle().Align(lipgloss.Center).Width(internalWidth).Render(ytStatusBadge)
		rows = append(rows, ytStatusRow)
		rows = append(rows, "")

		yt := rec.Metadata.YouTube

		// Video URL
		linkStyle := lipgloss.NewStyle().
			Foreground(ColorBlue).
			Underline(true)
		rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
			ytLabelStyle.Render("URL:"),
			"  ",
			linkStyle.Render(yt.VideoURL),
		))

		// Privacy
		privacyStyle := lipgloss.NewStyle().Foreground(ColorOrange).Bold(true)
		rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
			ytLabelStyle.Render("Privacy:"),
			"  ",
			privacyStyle.Render(yt.Privacy),
		))

		// Playlist
		if yt.PlaylistName != "" {
			rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
				ytLabelStyle.Render("Playlist:"),
				"  ",
				valueStyle.Render(yt.PlaylistName),
			))
		}

		// Upload date
		if yt.UploadedAt != "" {
			uploadTime, _ := time.Parse(time.RFC3339, yt.UploadedAt)
			rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
				ytLabelStyle.Render("Uploaded:"),
				"  ",
				valueStyle.Render(uploadTime.Format("Jan 2, 2006 15:04")),
			))
		}
	} else {
		// Not on YouTube
		ytStatusStyle := lipgloss.NewStyle().
			Foreground(ColorGray).
			Italic(true).
			Align(lipgloss.Center).
			Width(internalWidth)
		rows = append(rows, ytStatusStyle.Render("Not published to YouTube"))
	}

	// Success/Error messages
	editSuccess := ""
	if h.editForm != nil {
		editSuccess = h.editForm.State.SuccessMsg
	}
	if editSuccess != "" || h.youtubeActionSuccess != "" {
		successStyle := lipgloss.NewStyle().
			Foreground(ColorGreen).
			Bold(true).
			Align(lipgloss.Center).
			Width(internalWidth)
		rows = append(rows, "")
		msg := editSuccess
		if h.youtubeActionSuccess != "" {
			msg = h.youtubeActionSuccess
		}
		rows = append(rows, successStyle.Render(msg))
	}

	if h.youtubeActionError != "" {
		errorStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Bold(true).
			Align(lipgloss.Center).
			Width(internalWidth)
		rows = append(rows, "")
		rows = append(rows, errorStyle.Render(h.youtubeActionError))
	}

	content := containerStyle.Render(lipgloss.JoinVertical(lipgloss.Left, rows...))

	// Help text - changes based on YouTube status and recording status
	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	var helpText string
	if rec.Status == models.StatusFailed {
		helpText = "o: open folder • e: edit • r: reprocess • v: view error details • esc: back"
	} else if rec.Status == models.StatusCompleted {
		// Build video playback options based on available files
		var videoOptions string
		hasVertical := rec.Files.VerticalFile != ""
		hasMerged := rec.Files.MergedFile != ""
		if hasVertical && hasMerged {
			videoOptions = "v: vertical • m: merged"
		} else if hasVertical {
			videoOptions = "v: vertical"
		} else if hasMerged {
			videoOptions = "v: play • m: merged"
		}

		if rec.Metadata.IsPublishedToYouTube() {
			helpText = videoOptions + " • a: audio • o: folder • e: edit • r: reprocess • p: privacy • x: del YT • esc"
		} else {
			helpText = videoOptions + " • a: audio • o: folder • e: edit • r: reprocess • u: upload • esc"
		}
	} else {
		helpText = "o: open folder • e: edit • r: reprocess • esc: back"
	}

	mainSection := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		content,
	)

	centeredMain := lipgloss.Place(
		h.width,
		h.height-2,
		lipgloss.Center,
		lipgloss.Top,
		mainSection,
	)

	helpFooter := lipgloss.NewStyle().
		Width(h.width).
		Align(lipgloss.Center)

	return lipgloss.JoinVertical(
		lipgloss.Left,
		centeredMain,
		helpFooter.Render(helpStyle.Render(helpText)),
	)
}

// renderDeleteConfirmView renders the delete confirmation dialog
func (h *HistoryModel) renderDeleteConfirmView() string {
	if h.deleteConfirmRecording == nil {
		return "No recording selected"
	}

	rec := h.deleteConfirmRecording
	header := RenderHeader("Delete Recording")

	// Responsive widths
	containerWidth := h.detailContainerWidth()
	internalWidth := h.detailInternalWidth()

	// Styles
	warningStyle := lipgloss.NewStyle().
		Foreground(ColorRed).
		Bold(true).
		Align(lipgloss.Center)

	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Width(14).
		Align(lipgloss.Right)

	valueStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Bold(true)

	containerStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorRed).
		Padding(1, 3).
		Width(containerWidth)

	// Build confirmation dialog
	var rows []string

	// Warning message
	rows = append(rows, warningStyle.Width(internalWidth).Render("⚠ DELETE RECORDING ⚠"))
	rows = append(rows, "")
	rows = append(rows, warningStyle.Width(internalWidth).Render("This action cannot be undone!"))
	rows = append(rows, "")

	// Recording details
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		labelStyle.Render("Folder:"),
		"  ",
		valueStyle.Render(rec.Metadata.FolderName),
	))

	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		labelStyle.Render("Title:"),
		"  ",
		valueStyle.Render(rec.Metadata.Title),
	))

	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		labelStyle.Render("Date:"),
		"  ",
		valueStyle.Render(rec.StartTime.Format("2006-01-02")),
	))

	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		labelStyle.Render("Size:"),
		"  ",
		valueStyle.Render(models.FormatFileSize(rec.Files.TotalSize)),
	))

	rows = append(rows, "")

	// Error message if any
	if h.deleteError != "" {
		errorStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Bold(true).
			Align(lipgloss.Center).
			Width(internalWidth)
		rows = append(rows, errorStyle.Render(h.deleteError))
		rows = append(rows, "")
	}

	// Confirmation prompt
	promptStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true).
		Align(lipgloss.Center).
		Width(internalWidth)
	rows = append(rows, promptStyle.Render("Are you sure you want to delete this recording?"))
	rows = append(rows, "")

	// Buttons
	yesStyle := lipgloss.NewStyle().
		Foreground(ColorRed).
		Bold(true).
		Padding(0, 2).
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorRed)

	noStyle := lipgloss.NewStyle().
		Foreground(ColorGreen).
		Bold(true).
		Padding(0, 2).
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorGreen)

	buttons := lipgloss.JoinHorizontal(lipgloss.Center,
		yesStyle.Render("Y - Yes, Delete"),
		"    ",
		noStyle.Render("N - No, Cancel"),
	)
	buttonRow := lipgloss.NewStyle().Width(internalWidth).Align(lipgloss.Center).Render(buttons)
	rows = append(rows, buttonRow)

	content := containerStyle.Render(lipgloss.JoinVertical(lipgloss.Left, rows...))

	// Help text
	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	mainSection := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		content,
	)

	centeredMain := lipgloss.Place(
		h.width,
		h.height-2,
		lipgloss.Center,
		lipgloss.Top,
		mainSection,
	)

	helpFooter := lipgloss.NewStyle().
		Width(h.width).
		Align(lipgloss.Center)

	return lipgloss.JoinVertical(
		lipgloss.Left,
		centeredMain,
		helpFooter.Render(helpStyle.Render("y: confirm delete • n/esc: cancel")),
	)
}

// renderEditView renders the edit view for a selected recording
// Uses the shared RecordingForm component
func (h *HistoryModel) renderEditView() string {
	if h.selectedRecording == nil || h.editForm == nil {
		return "No recording selected"
	}

	header := RenderHeader("Edit Recording")
	content := h.editForm.View()
	footer := RenderHelpFooter("tab/↓: next • shift+tab/↑: prev • enter: edit field • ←/→: topic • ctrl+s: save • esc: cancel", h.width)

	return LayoutWithHeaderFooter(header, content, footer, h.width, h.height)
}

// renderScrollBar renders a visual scroll indicator
func (h *HistoryModel) renderScrollBar() string {
	if len(h.recordings) == 0 {
		return ""
	}

	visibleCount := h.getVisibleCount()
	totalEntries := len(h.recordings)

	barHeight := h.height - 16
	if barHeight < 5 {
		barHeight = 5
	}

	if totalEntries <= visibleCount {
		return ""
	}

	thumbSize := (visibleCount * barHeight) / totalEntries
	if thumbSize < 1 {
		thumbSize = 1
	}
	if thumbSize > barHeight {
		thumbSize = barHeight
	}

	thumbPos := (h.cursor * (barHeight - thumbSize)) / (totalEntries - 1)
	if thumbPos < 0 {
		thumbPos = 0
	}
	if thumbPos > barHeight-thumbSize {
		thumbPos = barHeight - thumbSize
	}

	var sb strings.Builder
	trackStyle := lipgloss.NewStyle().Foreground(ColorGray)
	thumbStyle := lipgloss.NewStyle().Foreground(ColorOrange)

	for i := 0; i < barHeight; i++ {
		if i >= thumbPos && i < thumbPos+thumbSize {
			sb.WriteString(thumbStyle.Render("┃"))
		} else {
			sb.WriteString(trackStyle.Render("│"))
		}
		if i < barHeight-1 {
			sb.WriteString("\n")
		}
	}

	return sb.String()
}

// renderScrollableTable renders the visible portion of the recordings table
func (h *HistoryModel) renderScrollableTable() string {
	visibleCount := h.getVisibleCount()
	totalEntries := len(h.recordings)

	startIdx := h.cursor - visibleCount/2
	if startIdx < 0 {
		startIdx = 0
	}
	endIdx := startIdx + visibleCount
	if endIdx > totalEntries {
		endIdx = totalEntries
		startIdx = endIdx - visibleCount
		if startIdx < 0 {
			startIdx = 0
		}
	}

	visibleRecordings := h.recordings[startIdx:endIdx]

	// Column headers
	headerStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(ColorOrange).
		Width(12).
		Align(lipgloss.Left)

	cellStyle := lipgloss.NewStyle().
		Width(12).
		Align(lipgloss.Left)

	selectedStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("#000000")).
		Background(ColorOrange).
		Width(12).
		Align(lipgloss.Left)

	descStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Width(62).
		Align(lipgloss.Left)

	selectedDescStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("#000000")).
		Background(ColorOrange).
		Width(62).
		Align(lipgloss.Left)

	header := lipgloss.JoinHorizontal(lipgloss.Top,
		headerStyle.Width(10).Render("Status"),
		headerStyle.Width(12).Render("Topic"),
		headerStyle.Width(12).Render("Date"),
		headerStyle.Width(10).Render("Duration"),
		headerStyle.Width(10).Render("Size"),
	)

	var rows []string
	for i, rec := range visibleRecordings {
		absoluteIdx := startIdx + i
		isSelected := absoluteIdx == h.cursor

		// Determine status icon and color
		statusIcon, statusColor := getStatusDisplay(rec.Status)

		// Add video indicator if a processed video exists
		hasVideo := rec.Files.VerticalFile != "" || rec.Files.MergedFile != ""
		if hasVideo {
			statusIcon = statusIcon + "🎬"
		}

		// Add YouTube indicator if video has been uploaded
		if rec.Metadata.YouTube != nil && rec.Metadata.YouTube.VideoID != "" {
			statusIcon = statusIcon + "📺"
		}

		topic := truncateStr(rec.Metadata.Topic, 10)
		dateStr := rec.StartTime.Format("2006-01-02")
		duration := models.FormatDuration(rec.Duration)
		size := models.FormatFileSize(rec.Files.TotalSize)
		folder := rec.Metadata.FolderName

		// Status cell with appropriate color (width 10 to accommodate YouTube icon)
		statusCellStyle := cellStyle.Width(10).Foreground(statusColor)
		selectedStatusStyle := selectedStyle.Width(10)

		var row1 string
		if isSelected {
			row1 = lipgloss.JoinHorizontal(lipgloss.Top,
				selectedStatusStyle.Render(statusIcon),
				selectedStyle.Width(12).Render(topic),
				selectedStyle.Width(12).Render(dateStr),
				selectedStyle.Width(10).Render(duration),
				selectedStyle.Width(10).Render(size),
			)
		} else {
			row1 = lipgloss.JoinHorizontal(lipgloss.Top,
				statusCellStyle.Render(statusIcon),
				cellStyle.Width(12).Render(topic),
				cellStyle.Width(12).Render(dateStr),
				cellStyle.Width(10).Render(duration),
				cellStyle.Width(10).Render(size),
			)
		}

		var row2 string
		if isSelected {
			row2 = selectedDescStyle.Render("  📁 " + folder)
		} else {
			row2 = descStyle.Render("  📁 " + folder)
		}

		rows = append(rows, row1, row2)

		if i < len(visibleRecordings)-1 {
			sep := lipgloss.NewStyle().
				Foreground(ColorGray).
				Render(strings.Repeat("─", 62))
			rows = append(rows, sep)
		}
	}

	var topIndicator, bottomIndicator string
	indicatorStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true).
		Align(lipgloss.Center).
		Width(62)

	if startIdx > 0 {
		topIndicator = indicatorStyle.Render(fmt.Sprintf("↑ %d more recordings above", startIdx))
	}
	if endIdx < totalEntries {
		bottomIndicator = indicatorStyle.Render(fmt.Sprintf("↓ %d more recordings below", totalEntries-endIdx))
	}

	tableContent := lipgloss.JoinVertical(lipgloss.Left, append([]string{header, ""}, rows...)...)

	if topIndicator != "" {
		tableContent = lipgloss.JoinVertical(lipgloss.Left, topIndicator, "", tableContent)
	}
	if bottomIndicator != "" {
		tableContent = lipgloss.JoinVertical(lipgloss.Left, tableContent, "", bottomIndicator)
	}

	tableStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorOrange).
		Padding(1, 2)

	return tableStyle.Render(tableContent)
}

// loadRecordings loads all recordings from the screencasts folder
func (h *HistoryModel) loadRecordings() tea.Cmd {
	return func() tea.Msg {
		videosDir := config.GetDefaultVideosDir()

		// Check if directory exists
		if _, err := os.Stat(videosDir); os.IsNotExist(err) {
			return recordingsLoadedMsg{recordings: nil, err: nil}
		}

		entries, err := os.ReadDir(videosDir)
		if err != nil {
			return recordingsLoadedMsg{recordings: nil, err: err}
		}

		var recordings []models.RecordingInfo

		for _, entry := range entries {
			if !entry.IsDir() {
				continue
			}

			folderPath := filepath.Join(videosDir, entry.Name())
			info, err := models.LoadRecordingInfo(folderPath)
			if err != nil {
				// Skip folders without valid recording.json
				continue
			}

			recordings = append(recordings, *info)
		}

		// Sort by date, newest first
		sort.Slice(recordings, func(i, j int) bool {
			return recordings[i].StartTime.After(recordings[j].StartTime)
		})

		return recordingsLoadedMsg{recordings: recordings, err: nil}
	}
}

// Helper function
func truncateStr(s string, maxLen int) string {
	if len(s) <= maxLen {
		return s
	}
	return s[:maxLen-3] + "..."
}

// getStatusDisplay returns an icon and color for a recording status
func getStatusDisplay(status string) (string, lipgloss.Color) {
	switch status {
	case models.StatusCompleted:
		return "✓ Done", ColorGreen
	case models.StatusFailed:
		return "✗ Error", ColorRed
	case models.StatusProcessing:
		return "⟳ Proc", ColorOrange
	case models.StatusRecording:
		return "● Rec", ColorRed
	case models.StatusPaused:
		return "⏸ Pause", ColorOrange
	case models.StatusNeedsMetadata:
		return "✎ Edit", ColorBlue
	default:
		return "? Unknown", ColorGray
	}
}

// renderYouTubePrivacyView renders the YouTube privacy change view
func (h *HistoryModel) renderYouTubePrivacyView() string {
	if h.selectedRecording == nil || h.selectedRecording.Metadata.YouTube == nil {
		return "No recording selected"
	}

	rec := h.selectedRecording
	header := RenderHeader("Change YouTube Privacy")

	// Styles
	containerStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorOrange).
		Padding(1, 3).
		Width(60)

	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	valueStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Bold(true)

	// Build rows
	var rows []string

	// Title
	titleBadge := lipgloss.NewStyle().
		Background(ColorBlue).
		Foreground(ColorWhite).
		Padding(0, 1).
		Bold(true).
		Render(rec.Metadata.Title)
	titleRow := lipgloss.NewStyle().Align(lipgloss.Center).Width(52).Render(titleBadge)
	rows = append(rows, titleRow)
	rows = append(rows, "")

	// Current privacy
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		labelStyle.Render("Current Privacy: "),
		valueStyle.Render(rec.Metadata.YouTube.Privacy),
	))
	rows = append(rows, "")

	// Privacy options
	rows = append(rows, labelStyle.Render("Select new privacy:"))
	rows = append(rows, "")

	var privacyOptions []string
	for i, opt := range h.youtubePrivacyOptions {
		style := lipgloss.NewStyle().
			Padding(0, 2).
			Margin(0, 1)

		if i == h.youtubeSelectedPrivacy {
			style = style.
				Background(ColorOrange).
				Foreground(lipgloss.Color("#000000")).
				Bold(true)
		} else {
			style = style.
				Foreground(ColorGray)
		}
		privacyOptions = append(privacyOptions, style.Render(opt))
	}
	optionsRow := lipgloss.NewStyle().Width(52).Align(lipgloss.Center).Render(
		lipgloss.JoinHorizontal(lipgloss.Center, privacyOptions...),
	)
	rows = append(rows, optionsRow)

	// Error message
	if h.youtubeActionError != "" {
		errorStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Bold(true).
			Align(lipgloss.Center).
			Width(52)
		rows = append(rows, "")
		rows = append(rows, errorStyle.Render(h.youtubeActionError))
	}

	// Loading
	if h.youtubeActionLoading {
		loadingStyle := lipgloss.NewStyle().
			Foreground(ColorOrange).
			Bold(true).
			Align(lipgloss.Center).
			Width(52)
		rows = append(rows, "")
		rows = append(rows, loadingStyle.Render("Updating privacy..."))
	}

	content := containerStyle.Render(lipgloss.JoinVertical(lipgloss.Left, rows...))

	// Help text
	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	mainSection := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		content,
	)

	centeredMain := lipgloss.Place(
		h.width,
		h.height-2,
		lipgloss.Center,
		lipgloss.Top,
		mainSection,
	)

	helpFooter := lipgloss.NewStyle().
		Width(h.width).
		Align(lipgloss.Center)

	return lipgloss.JoinVertical(
		lipgloss.Left,
		centeredMain,
		helpFooter.Render(helpStyle.Render("←/→: select • enter: confirm • esc: cancel")),
	)
}

// renderYouTubeDeleteConfirmView renders the YouTube delete confirmation view
func (h *HistoryModel) renderYouTubeDeleteConfirmView() string {
	if h.selectedRecording == nil || h.selectedRecording.Metadata.YouTube == nil {
		return "No recording selected"
	}

	rec := h.selectedRecording
	header := RenderHeader("Delete from YouTube")

	// Styles
	containerStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorRed).
		Padding(1, 3).
		Width(60)

	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	valueStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Bold(true)

	// Build rows
	var rows []string

	// Warning icon
	warningBadge := lipgloss.NewStyle().
		Foreground(ColorRed).
		Bold(true).
		Render("⚠ DELETE VIDEO FROM YOUTUBE")
	warningRow := lipgloss.NewStyle().Align(lipgloss.Center).Width(52).Render(warningBadge)
	rows = append(rows, warningRow)
	rows = append(rows, "")

	// Title
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		labelStyle.Render("Title: "),
		valueStyle.Render(rec.Metadata.Title),
	))

	// URL
	linkStyle := lipgloss.NewStyle().Foreground(ColorBlue).Underline(true)
	rows = append(rows, lipgloss.JoinHorizontal(lipgloss.Top,
		labelStyle.Render("URL: "),
		linkStyle.Render(rec.Metadata.YouTube.VideoURL),
	))
	rows = append(rows, "")

	// Warning message
	warningStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true).
		Align(lipgloss.Center).
		Width(52)
	rows = append(rows, warningStyle.Render("This action cannot be undone!"))
	rows = append(rows, warningStyle.Render("The video will be permanently deleted from YouTube."))
	rows = append(rows, "")

	// Error message
	if h.youtubeActionError != "" {
		errorStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Bold(true).
			Align(lipgloss.Center).
			Width(52)
		rows = append(rows, errorStyle.Render(h.youtubeActionError))
		rows = append(rows, "")
	}

	// Loading
	if h.youtubeActionLoading {
		loadingStyle := lipgloss.NewStyle().
			Foreground(ColorOrange).
			Bold(true).
			Align(lipgloss.Center).
			Width(52)
		rows = append(rows, loadingStyle.Render("Deleting from YouTube..."))
		rows = append(rows, "")
	}

	// Buttons
	yesStyle := lipgloss.NewStyle().
		Foreground(ColorRed).
		Bold(true).
		Padding(0, 2).
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorRed)

	noStyle := lipgloss.NewStyle().
		Foreground(ColorGreen).
		Bold(true).
		Padding(0, 2).
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorGreen)

	buttons := lipgloss.JoinHorizontal(lipgloss.Center,
		yesStyle.Render("Y - Yes, Delete"),
		"    ",
		noStyle.Render("N - No, Cancel"),
	)
	buttonRow := lipgloss.NewStyle().Width(52).Align(lipgloss.Center).Render(buttons)
	rows = append(rows, buttonRow)

	content := containerStyle.Render(lipgloss.JoinVertical(lipgloss.Left, rows...))

	// Help text
	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	mainSection := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		content,
	)

	centeredMain := lipgloss.Place(
		h.width,
		h.height-2,
		lipgloss.Center,
		lipgloss.Top,
		mainSection,
	)

	helpFooter := lipgloss.NewStyle().
		Width(h.width).
		Align(lipgloss.Center)

	return lipgloss.JoinVertical(
		lipgloss.Left,
		centeredMain,
		helpFooter.Render(helpStyle.Render("y: confirm delete • n/esc: cancel")),
	)
}

// renderErrorDetailView renders the full error detail view with scrolling
func (h *HistoryModel) renderErrorDetailView() string {
	if h.selectedRecording == nil {
		return "No recording selected"
	}

	rec := h.selectedRecording
	header := RenderHeader("Error Details")

	// Styles
	containerStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorRed).
		Padding(1, 2).
		Width(h.width - 10)

	// Build content
	var contentLines []string

	// Title and folder
	titleStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)
	contentLines = append(contentLines, titleStyle.Render("Recording: "+rec.Metadata.Title))
	contentLines = append(contentLines, lipgloss.NewStyle().Foreground(ColorGray).Render("Folder: "+rec.Metadata.FolderName))
	contentLines = append(contentLines, "")

	// Error summary
	if len(rec.Processing.Errors) > 0 {
		errorStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Bold(true)
		contentLines = append(contentLines, errorStyle.Render("ERROR SUMMARY:"))
		for _, e := range rec.Processing.Errors {
			contentLines = append(contentLines, "  • "+e)
		}
		contentLines = append(contentLines, "")
	}

	// Error detail
	if rec.Processing.ErrorDetail != "" {
		sectionStyle := lipgloss.NewStyle().
			Foreground(ColorOrange).
			Bold(true)
		contentLines = append(contentLines, sectionStyle.Render("DETAILED ERROR INFORMATION:"))
		contentLines = append(contentLines, strings.Repeat("─", 60))
		// Split detail into lines and add
		detailLines := strings.Split(rec.Processing.ErrorDetail, "\n")
		contentLines = append(contentLines, detailLines...)
		contentLines = append(contentLines, "")
	}

	// Traceback
	if rec.Processing.Traceback != "" {
		sectionStyle := lipgloss.NewStyle().
			Foreground(ColorOrange).
			Bold(true)
		contentLines = append(contentLines, sectionStyle.Render("STACK TRACE (for bug reports):"))
		contentLines = append(contentLines, strings.Repeat("─", 60))
		// Split traceback into lines
		traceLines := strings.Split(rec.Processing.Traceback, "\n")
		for _, line := range traceLines {
			// Truncate very long lines
			if len(line) > h.width-20 {
				line = line[:h.width-23] + "..."
			}
			contentLines = append(contentLines, line)
		}
	}

	// Calculate visible window
	maxVisibleLines := h.height - 15
	if maxVisibleLines < 5 {
		maxVisibleLines = 5
	}

	totalLines := len(contentLines)

	// Clamp scroll offset
	maxOffset := totalLines - maxVisibleLines
	if maxOffset < 0 {
		maxOffset = 0
	}
	if h.errorViewScrollOffset > maxOffset {
		h.errorViewScrollOffset = maxOffset
	}

	// Get visible portion
	startLine := h.errorViewScrollOffset
	endLine := startLine + maxVisibleLines
	if endLine > totalLines {
		endLine = totalLines
	}

	visibleContent := strings.Join(contentLines[startLine:endLine], "\n")

	// Scroll indicator
	scrollInfo := fmt.Sprintf("Lines %d-%d of %d", startLine+1, endLine, totalLines)
	if h.errorViewScrollOffset > 0 {
		scrollInfo = "↑ " + scrollInfo
	}
	if endLine < totalLines {
		scrollInfo = scrollInfo + " ↓"
	}
	scrollStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	content := containerStyle.Render(visibleContent)

	// Help text
	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	mainSection := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		scrollStyle.Render(scrollInfo),
		"",
		content,
	)

	centeredMain := lipgloss.Place(
		h.width,
		h.height-2,
		lipgloss.Center,
		lipgloss.Top,
		mainSection,
	)

	helpFooter := lipgloss.NewStyle().
		Width(h.width).
		Align(lipgloss.Center)

	return lipgloss.JoinVertical(
		lipgloss.Left,
		centeredMain,
		helpFooter.Render(helpStyle.Render("↑/↓: scroll • pgup/pgdn: page • r: reprocess • esc: back")),
	)
}

// Message types
type recordingsLoadedMsg struct {
	recordings []models.RecordingInfo
	err        error
}

type recordingSavedMsg struct {
	err error
}

// backToMenuMsg signals returning to the main menu
type backToMenuMsg struct{}

// YouTube action messages
type youtubePrivacyChangedMsg struct {
	newPrivacy string
	err        error
}

type youtubeVideoDeletedMsg struct {
	err error
}

type startYouTubeUploadMsg struct {
	recording *models.RecordingInfo
	videoPath string
}

type startReprocessMsg struct {
	recording *models.RecordingInfo
}

// recordingSavedNeedsProcessingMsg signals that a recording was saved and needs processing
type recordingSavedNeedsProcessingMsg struct {
	recording *models.RecordingInfo
}

// renderReprocessConfirmView renders the reprocess confirmation dialog
func (h *HistoryModel) renderReprocessConfirmView() string {
	header := RenderHeader("Reprocess Recording")

	if h.selectedRecording == nil {
		return "No recording selected"
	}

	warningStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	errorStyle := lipgloss.NewStyle().
		Foreground(ColorRed).
		Bold(true)

	textStyle := lipgloss.NewStyle().
		Foreground(ColorWhite)

	grayStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	containerStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorOrange).
		Padding(1, 3).
		Width(65)

	var rows []string
	rows = append(rows, warningStyle.Render("Reprocess this recording?"))
	rows = append(rows, "")
	rows = append(rows, textStyle.Render("This will regenerate the processed video files using"))
	rows = append(rows, textStyle.Render("current settings (logos, audio processing, etc.)."))
	rows = append(rows, "")

	// Show what will happen
	rows = append(rows, grayStyle.Render("What will be regenerated:"))
	rows = append(rows, textStyle.Render("  • Merged video/audio"))
	if h.selectedRecording.Settings.VerticalEnabled {
		rows = append(rows, textStyle.Render("  • Vertical video"))
	}
	rows = append(rows, "")

	// Show YouTube warning if video is published
	if h.selectedRecording.Metadata.IsPublishedToYouTube() {
		rows = append(rows, errorStyle.Render("WARNING: This video is published on YouTube!"))
		rows = append(rows, "")
		rows = append(rows, textStyle.Render("The YouTube video will NOT be updated automatically."))
		rows = append(rows, textStyle.Render("To update YouTube, you must:"))
		rows = append(rows, textStyle.Render("  1. Delete the video from YouTube first (X)"))
		rows = append(rows, textStyle.Render("  2. Reprocess the recording"))
		rows = append(rows, textStyle.Render("  3. Upload the new version (U)"))
		rows = append(rows, "")
	}

	rows = append(rows, grayStyle.Render("y: confirm • n/esc: cancel"))

	content := containerStyle.Render(lipgloss.JoinVertical(lipgloss.Left, rows...))

	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	helpText := helpStyle.Render("y: confirm reprocess • n/esc: cancel")

	fullContent := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		content,
		"",
		helpText,
	)

	return lipgloss.Place(h.width, h.height, lipgloss.Center, lipgloss.Center, fullContent)
}
