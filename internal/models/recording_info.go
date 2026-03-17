package models

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

// Recording status constants
const (
	StatusRecording       = "recording"
	StatusPaused          = "paused"
	StatusProcessing      = "processing"
	StatusCompleted       = "completed"
	StatusFailed          = "failed"
	StatusNeedsMetadata   = "needs_metadata" // Recording stopped via systray, needs title/description
)

// RecordingInfo contains all information about a recording
type RecordingInfo struct {
	// Current status of the recording
	Status string `json:"status"`

	// User-provided metadata
	Metadata RecordingMetadata `json:"metadata"`

	// Timing information
	StartTime time.Time     `json:"start_time"`
	EndTime   time.Time     `json:"end_time"`
	Duration  time.Duration `json:"duration"`

	// Recording environment
	Environment EnvironmentInfo `json:"environment"`

	// File information
	Files FileInfo `json:"files"`

	// Recording settings used
	Settings RecordingSettings `json:"settings"`

	// Processing information
	Processing ProcessingInfo `json:"processing"`

	// Version info
	AppVersion string    `json:"app_version"`
	CreatedAt  time.Time `json:"created_at"`
	UpdatedAt  time.Time `json:"updated_at"`
}

// EnvironmentInfo contains system environment details
type EnvironmentInfo struct {
	OS                 string `json:"os"`
	Arch               string `json:"arch"`
	Hostname           string `json:"hostname"`
	DesktopEnvironment string `json:"desktop_environment"`
	WaylandCompositor  string `json:"wayland_compositor,omitempty"`
	Monitor            string `json:"monitor"`
	MonitorResolution  string `json:"monitor_resolution"`
}

// VideoFileMetadata contains metadata about a video file
type VideoFileMetadata struct {
	Width       int     `json:"width,omitempty"`
	Height      int     `json:"height,omitempty"`
	FPS         float64 `json:"fps,omitempty"`
	AspectRatio string  `json:"aspect_ratio,omitempty"`
	Duration    float64 `json:"duration_seconds,omitempty"`
	Codec       string  `json:"codec,omitempty"`
	Size        int64   `json:"size,omitempty"`
}

// FileInfo contains information about recording files
type FileInfo struct {
	FolderPath   string `json:"folder_path"`
	VideoFile    string `json:"video_file,omitempty"`
	AudioFile    string `json:"audio_file,omitempty"`
	WebcamFile   string `json:"webcam_file,omitempty"`
	MergedFile   string `json:"merged_file,omitempty"`
	VerticalFile string `json:"vertical_file,omitempty"`

	// Part files for pause/resume support
	VideoParts  []string `json:"video_parts,omitempty"`
	AudioParts  []string `json:"audio_parts,omitempty"`
	WebcamParts []string `json:"webcam_parts,omitempty"`
	CurrentPart int      `json:"current_part"` // Current part number (0-indexed)

	VideoSize    int64 `json:"video_size,omitempty"`
	AudioSize    int64 `json:"audio_size,omitempty"`
	WebcamSize   int64 `json:"webcam_size,omitempty"`
	MergedSize   int64 `json:"merged_size,omitempty"`
	VerticalSize int64 `json:"vertical_size,omitempty"`
	TotalSize    int64 `json:"total_size"`

	// Video metadata for each file
	VideoMeta  *VideoFileMetadata `json:"video_meta,omitempty"`
	WebcamMeta *VideoFileMetadata `json:"webcam_meta,omitempty"`
	MergedMeta   *VideoFileMetadata `json:"merged_meta,omitempty"`
	VerticalMeta *VideoFileMetadata `json:"vertical_meta,omitempty"`
}

// RecordingSettings contains the settings used for recording
type RecordingSettings struct {
	// Recording sources
	ScreenEnabled  bool `json:"screen_enabled"`
	AudioEnabled   bool `json:"audio_enabled"`
	WebcamEnabled  bool `json:"webcam_enabled"`

	// Output options
	VerticalEnabled     bool `json:"vertical_enabled"`      // Whether vertical video will be created
	LeftSplitEnabled    bool `json:"left_split_enabled"`    // Use left half of screen for vertical video
	WebcamBubbleEnabled bool `json:"webcam_bubble_enabled"` // Use circular webcam bubble (true) or rectangular (false)
	LogosEnabled        bool `json:"logos_enabled"`         // Whether logos will be added

	// Hardware/device settings
	HardwareAccel bool   `json:"hardware_accel"`
	AudioDevice   string `json:"audio_device"`
	WebcamDevice  string `json:"webcam_device,omitempty"`
	WebcamFPS     int    `json:"webcam_fps,omitempty"`

	// Processing options
	NormalizeEnabled bool `json:"normalize_enabled"`

	// Logo settings (if logos enabled)
	LeftLogo    string `json:"left_logo,omitempty"`
	RightLogo   string `json:"right_logo,omitempty"`
	BottomLogo  string `json:"bottom_logo,omitempty"`
	TitleColor  string `json:"title_color,omitempty"`
	GifLoopMode string `json:"gif_loop_mode,omitempty"`
	BgColor     string `json:"bg_color,omitempty"` // Background color for vertical video lower third
}

// ProcessingInfo contains information about post-processing
type ProcessingInfo struct {
	ProcessedAt      time.Time     `json:"processed_at,omitempty"`
	ProcessingTime   time.Duration `json:"processing_time,omitempty"`
	NormalizeApplied bool          `json:"normalize_applied"`
	VerticalCreated  bool          `json:"vertical_created"`
	Errors           []string      `json:"errors,omitempty"`
	// ErrorDetail provides a detailed, user-friendly explanation of what went wrong
	ErrorDetail string `json:"error_detail,omitempty"`
	// Traceback contains the full stack trace or error chain for debugging
	Traceback string `json:"traceback,omitempty"`
}

// NewRecordingInfo creates a new RecordingInfo with system information populated
func NewRecordingInfo(metadata RecordingMetadata, monitor, resolution string) *RecordingInfo {
	hostname, _ := os.Hostname()

	return &RecordingInfo{
		Status:    StatusRecording,
		Metadata:  metadata,
		StartTime: time.Now(),
		Environment: EnvironmentInfo{
			OS:                 runtime.GOOS,
			Arch:               runtime.GOARCH,
			Hostname:           hostname,
			DesktopEnvironment: getDesktopEnvironment(),
			WaylandCompositor:  getWaylandCompositor(),
			Monitor:            monitor,
			MonitorResolution:  resolution,
		},
		AppVersion: "1.0.0",
		CreatedAt:  time.Now(),
		UpdatedAt:  time.Now(),
	}
}

// SetStatus updates the recording status
func (r *RecordingInfo) SetStatus(status string) {
	r.Status = status
	r.UpdatedAt = time.Now()
}

// SetEndTime sets the recording end time and calculates duration
func (r *RecordingInfo) SetEndTime(t time.Time) {
	r.EndTime = t
	r.Duration = t.Sub(r.StartTime)
	r.UpdatedAt = time.Now()
}

// Save saves the recording info to a JSON file in the recording folder
func (r *RecordingInfo) Save() error {
	if r.Files.FolderPath == "" {
		return nil
	}

	infoPath := filepath.Join(r.Files.FolderPath, "recording.json")
	r.UpdatedAt = time.Now()

	data, err := json.MarshalIndent(r, "", "  ")
	if err != nil {
		return err
	}

	return os.WriteFile(infoPath, data, 0644)
}

// RenameFolder renames the recording folder to the format NNN-title_name
// where NNN is the zero-padded sequence number and title_name is a sanitized version
// of the recording title. Returns the new folder path or an error.
func (r *RecordingInfo) RenameFolder() (string, error) {
	if r.Files.FolderPath == "" {
		return "", fmt.Errorf("no folder path set")
	}

	// Get the new folder name from metadata
	r.Metadata.GenerateFolderName()
	if r.Metadata.FolderName == "" {
		return r.Files.FolderPath, nil // No name to change to
	}

	// Get parent directory
	parentDir := filepath.Dir(r.Files.FolderPath)
	newFolderPath := filepath.Join(parentDir, r.Metadata.FolderName)

	// If the folder is already named correctly, just return
	if r.Files.FolderPath == newFolderPath {
		return r.Files.FolderPath, nil
	}

	// Check if target folder already exists
	if _, err := os.Stat(newFolderPath); err == nil {
		// Target exists, try adding a suffix
		for i := 2; i <= 100; i++ {
			altPath := fmt.Sprintf("%s_%d", newFolderPath, i)
			if _, err := os.Stat(altPath); os.IsNotExist(err) {
				newFolderPath = altPath
				break
			}
		}
	}

	// Rename the folder
	if err := os.Rename(r.Files.FolderPath, newFolderPath); err != nil {
		return r.Files.FolderPath, fmt.Errorf("failed to rename folder: %w", err)
	}

	// Update all file paths to reflect the new folder location
	r.fixFilePaths(newFolderPath)

	// Update metadata folder name to match actual folder
	r.Metadata.FolderName = filepath.Base(newFolderPath)

	// Save updated info to the new location
	if err := r.Save(); err != nil {
		return newFolderPath, fmt.Errorf("folder renamed but failed to save metadata: %w", err)
	}

	return newFolderPath, nil
}

// LoadRecordingInfo loads recording info from a folder
func LoadRecordingInfo(folderPath string) (*RecordingInfo, error) {
	infoPath := filepath.Join(folderPath, "recording.json")

	data, err := os.ReadFile(infoPath)
	if err != nil {
		return nil, err
	}

	var info RecordingInfo
	if err := json.Unmarshal(data, &info); err != nil {
		return nil, err
	}

	// Fix file paths to use the actual folder path (in case folder was moved or user changed)
	info.fixFilePaths(folderPath)

	return &info, nil
}

// fixFilePaths updates all file paths to be relative to the given folder path
// This handles cases where the recording folder was moved or the user changed
func (r *RecordingInfo) fixFilePaths(folderPath string) {
	r.Files.FolderPath = folderPath

	// Helper to fix a single path
	fixPath := func(oldPath string) string {
		if oldPath == "" {
			return ""
		}
		// Get just the filename and join with the new folder path
		return filepath.Join(folderPath, filepath.Base(oldPath))
	}

	// Fix main file paths
	r.Files.VideoFile = fixPath(r.Files.VideoFile)
	r.Files.AudioFile = fixPath(r.Files.AudioFile)
	r.Files.WebcamFile = fixPath(r.Files.WebcamFile)
	r.Files.MergedFile = fixPath(r.Files.MergedFile)
	r.Files.VerticalFile = fixPath(r.Files.VerticalFile)

	// Fix part file paths
	for i, part := range r.Files.VideoParts {
		r.Files.VideoParts[i] = fixPath(part)
	}
	for i, part := range r.Files.AudioParts {
		r.Files.AudioParts[i] = fixPath(part)
	}
	for i, part := range r.Files.WebcamParts {
		r.Files.WebcamParts[i] = fixPath(part)
	}
}

// UpdateFileSizes updates the file size information
func (r *RecordingInfo) UpdateFileSizes() {
	r.Files.TotalSize = 0

	if r.Files.VideoFile != "" {
		if stat, err := os.Stat(r.Files.VideoFile); err == nil {
			r.Files.VideoSize = stat.Size()
			r.Files.TotalSize += stat.Size()
		}
	}

	if r.Files.AudioFile != "" {
		if stat, err := os.Stat(r.Files.AudioFile); err == nil {
			r.Files.AudioSize = stat.Size()
			r.Files.TotalSize += stat.Size()
		}
	}

	if r.Files.WebcamFile != "" {
		if stat, err := os.Stat(r.Files.WebcamFile); err == nil {
			r.Files.WebcamSize = stat.Size()
			r.Files.TotalSize += stat.Size()
		}
	}

	if r.Files.MergedFile != "" {
		if stat, err := os.Stat(r.Files.MergedFile); err == nil {
			r.Files.MergedSize = stat.Size()
			r.Files.TotalSize += stat.Size()
		}
	}

	if r.Files.VerticalFile != "" {
		if stat, err := os.Stat(r.Files.VerticalFile); err == nil {
			r.Files.VerticalSize = stat.Size()
			r.Files.TotalSize += stat.Size()
		}
	}

	r.UpdatedAt = time.Now()
}

// VideoInfoFunc is a function type for getting video metadata
// This allows dependency injection to avoid circular imports
type VideoInfoFunc func(filepath string) (*VideoFileMetadata, error)

// UpdateVideoMetadata updates metadata for all video files using the provided function
func (r *RecordingInfo) UpdateVideoMetadata(getInfo VideoInfoFunc) {
	if getInfo == nil {
		return
	}

	if r.Files.VideoFile != "" {
		if meta, err := getInfo(r.Files.VideoFile); err == nil {
			r.Files.VideoMeta = meta
		}
	}

	if r.Files.WebcamFile != "" {
		if meta, err := getInfo(r.Files.WebcamFile); err == nil {
			r.Files.WebcamMeta = meta
		}
	}

	if r.Files.MergedFile != "" {
		if meta, err := getInfo(r.Files.MergedFile); err == nil {
			r.Files.MergedMeta = meta
		}
	}

	if r.Files.VerticalFile != "" {
		if meta, err := getInfo(r.Files.VerticalFile); err == nil {
			r.Files.VerticalMeta = meta
		}
	}

	r.UpdatedAt = time.Now()
}

// Helper functions

func getDesktopEnvironment() string {
	// Check common environment variables
	if de := os.Getenv("XDG_CURRENT_DESKTOP"); de != "" {
		return de
	}
	if de := os.Getenv("DESKTOP_SESSION"); de != "" {
		return de
	}
	if os.Getenv("GNOME_DESKTOP_SESSION_ID") != "" {
		return "GNOME"
	}
	if os.Getenv("KDE_FULL_SESSION") != "" {
		return "KDE"
	}
	return "Unknown"
}

func getWaylandCompositor() string {
	// Check for common Wayland compositors
	if os.Getenv("HYPRLAND_INSTANCE_SIGNATURE") != "" {
		return "Hyprland"
	}
	if os.Getenv("SWAYSOCK") != "" {
		return "Sway"
	}
	if os.Getenv("WAYLAND_DISPLAY") != "" {
		// Try to get compositor from loginctl
		cmd := exec.Command("loginctl", "show-session", "", "--property=Type")
		if output, err := cmd.Output(); err == nil {
			if strings.Contains(string(output), "wayland") {
				return "Wayland (unknown compositor)"
			}
		}
	}
	return ""
}

// FormatDuration formats a duration for display
func FormatDuration(d time.Duration) string {
	h := int(d.Hours())
	m := int(d.Minutes()) % 60
	s := int(d.Seconds()) % 60

	if h > 0 {
		return fmt.Sprintf("%dh%02dm%02ds", h, m, s)
	}
	if m > 0 {
		return fmt.Sprintf("%dm%02ds", m, s)
	}
	return fmt.Sprintf("%ds", s)
}

// FormatFileSize formats a file size in bytes for display
func FormatFileSize(bytes int64) string {
	const (
		KB float64 = 1024
		MB         = KB * 1024
		GB         = MB * 1024
	)

	b := float64(bytes)

	switch {
	case b >= GB:
		return fmt.Sprintf("%.1f GB", b/GB)
	case b >= MB:
		return fmt.Sprintf("%.1f MB", b/MB)
	case b >= KB:
		return fmt.Sprintf("%.1f KB", b/KB)
	default:
		return fmt.Sprintf("%d B", bytes)
	}
}
