package config

import (
	"encoding/json"
	"os"
	"path/filepath"

	"github.com/kartoza/kartoza-screencaster/internal/models"
	"github.com/kartoza/kartoza-screencaster/internal/syndication"
	"github.com/kartoza/kartoza-screencaster/internal/webcam"
	"github.com/kartoza/kartoza-screencaster/internal/youtube"
)

const (
	// DefaultConfigDir is the default configuration directory
	DefaultConfigDir = ".config/kartoza-screencaster"
	// DefaultVideosDir is the default output directory for recordings
	DefaultVideosDir = "Videos/Screencasts"
	// ConfigFileName is the name of the configuration file
	ConfigFileName = "config.json"
)

// Paths for PID and state files
const (
	VideoPIDFile   = "/tmp/kartoza-video.pid"
	AudioPIDFile   = "/tmp/kartoza-audio.pid"
	WebcamPIDFile  = "/tmp/kartoza-webcam.pid"  // Legacy single webcam PID
	WebcamPIDsFile = "/tmp/kartoza-webcam-pids.json" // Multi-webcam: JSON map of device->pid
	StatusFile     = "/tmp/kartoza-video.status"
	VideoPathFile  = "/tmp/kartoza-video.path"
	AudioPathFile  = "/tmp/kartoza-audio.path"
	WebcamPathFile = "/tmp/kartoza-webcam.path"
	StopSignalFile = "/tmp/kartoza-video.stop"
	OutputDirFile  = "/tmp/kartoza-output.path" // Stores current recording output directory
	PartNumberFile = "/tmp/kartoza-part.num"    // Stores current part number for pause/resume
	PausedFile     = "/tmp/kartoza-paused"      // Indicates recording is paused
)

// GifLoopMode controls how animated GIFs are played
type GifLoopMode string

const (
	GifLoopContinuous GifLoopMode = "continuous" // Loop forever
	GifLoopOnce       GifLoopMode = "once"       // Play once and stop at last frame
	GifLoopNone       GifLoopMode = "none"       // Show only first frame (static)
)

// GifLoopModes is the list of available GIF loop modes
var GifLoopModes = []GifLoopMode{GifLoopContinuous, GifLoopOnce, GifLoopNone}

// GifLoopModeLabels provides human-readable labels for GIF loop modes
var GifLoopModeLabels = map[GifLoopMode]string{
	GifLoopContinuous: "Loop continuously",
	GifLoopOnce:       "Play once, stop at end",
	GifLoopNone:       "First frame only",
}

// LogoSelection holds the selected logos for a recording
type LogoSelection struct {
	LeftLogo    string      `json:"left_logo,omitempty"`    // Top-left logo
	RightLogo   string      `json:"right_logo,omitempty"`   // Top-right logo
	BottomLogo  string      `json:"bottom_logo,omitempty"`  // Lower third logo
	TitleColor  string      `json:"title_color,omitempty"`  // Color for title text overlay
	GifLoopMode GifLoopMode `json:"gif_loop_mode,omitempty"` // How to loop animated GIFs
}

// TerminalRecordingSettings holds settings for terminal (asciinema) recording
type TerminalRecordingSettings struct {
	FontSize      int     `json:"font_size,omitempty"`       // Font size for rendering (default: 16)
	FontFamily    string  `json:"font_family,omitempty"`     // Font family for rendering
	Theme         string  `json:"theme,omitempty"`           // Color theme (e.g., "monokai", "solarized-dark")
	IdleTimeLimit float64 `json:"idle_time_limit,omitempty"` // Max idle time in seconds (default: 2)
	FPSCap        int     `json:"fps_cap,omitempty"`         // FPS cap for rendering (default: 30)
}

// DefaultTerminalRecordingSettings returns sensible defaults
func DefaultTerminalRecordingSettings() TerminalRecordingSettings {
	return TerminalRecordingSettings{
		FontSize:      16,
		FontFamily:    "JetBrains Mono,Fira Code,monospace",
		IdleTimeLimit: 2.0,
		FPSCap:        30,
	}
}

// RecordingPresets holds the user's preferred recording settings
// These are saved and restored between sessions (excludes title, description, number)
type RecordingPresets struct {
	RecordAudio   bool   `json:"record_audio"`
	RecordWebcam  bool   `json:"record_webcam"`
	RecordScreen  bool   `json:"record_screen"`
	VerticalVideo bool   `json:"vertical_video"`
	LeftSplit     bool   `json:"left_split"`     // Use left half of screen for vertical video
	WebcamBubble  bool   `json:"webcam_bubble"`  // Use circular webcam bubble (true) or rectangular (false)
	AddLogos      bool   `json:"add_logos"`
	Topic         string `json:"topic,omitempty"` // Last selected topic name
}

// DefaultRecordingPresets returns sensible defaults for recording presets
func DefaultRecordingPresets() RecordingPresets {
	return RecordingPresets{
		RecordAudio:   true,
		RecordWebcam:  true,
		RecordScreen:  true,
		VerticalVideo: true,
		LeftSplit:     true,  // Default to left split mode for vertical video
		WebcamBubble:  true,  // Default to circular bubble webcam
		AddLogos:      true,
	}
}

// DefaultTitleColor is the default color for title text (Kartoza blue)
const DefaultTitleColor = "#62A4C7"

// DefaultBgColor is the default background color for the vertical video lower third
const DefaultBgColor = "white"

// Available title colors for the user to choose from
// Includes named colors and can accept #RRGGBB hex values
var TitleColors = []string{"#62A4C7", "white", "black", "yellow", "orange", "red", "green", "blue", "cyan", "magenta"}

// BgColors is the list of available background colors for the vertical video lower third
var BgColors = []string{"white", "black", "#62A4C7", "yellow", "orange", "red", "green", "blue", "cyan", "magenta"}

// Config holds the application configuration
type Config struct {
	OutputDir        string                        `json:"output_dir"`
	DefaultOptions   models.RecordingOptions       `json:"default_options"`
	AudioProcessing  models.AudioProcessingOptions `json:"audio_processing"`
	Topics           []models.Topic                `json:"topics,omitempty"`
	DefaultPresenter string                        `json:"default_presenter,omitempty"`

	// Logo settings
	LogoDirectory  string        `json:"logo_directory,omitempty"`   // Directory to browse for logos
	LastUsedLogos  LogoSelection `json:"last_used_logos,omitempty"`  // Last used logo selection
	BgColor        string        `json:"bg_color,omitempty"`         // Background color for vertical video lower third

	// Recording presets (saved between sessions)
	RecordingPresets  RecordingPresets `json:"recording_presets,omitempty"`
	PresetsConfigured bool             `json:"presets_configured,omitempty"` // Whether user has explicitly configured presets

	// YouTube integration settings
	YouTube youtube.Config `json:"youtube,omitempty"`

	// Syndication settings for multi-platform posting
	Syndication syndication.Config `json:"syndication,omitempty"`

	// Terminal recording settings (asciinema)
	TerminalRecording TerminalRecordingSettings `json:"terminal_recording,omitempty"`

	// Multi-webcam configuration (persisted per device)
	WebcamConfigs []webcam.WebcamConfig `json:"webcam_configs,omitempty"`
}

// DefaultConfig returns the default configuration
func DefaultConfig() Config {
	return Config{
		OutputDir:       GetDefaultVideosDir(),
		DefaultOptions:  models.DefaultRecordingOptions(),
		AudioProcessing: models.DefaultAudioProcessingOptions(),
		YouTube:         youtube.DefaultConfig(),
		Syndication:     syndication.DefaultConfig(),
	}
}

// GetYouTubeAuthStatus returns the current YouTube authentication status
func (c *Config) GetYouTubeAuthStatus() youtube.AuthStatus {
	return youtube.GetAuthStatus(&c.YouTube, GetConfigDir())
}

// IsYouTubeConnected returns true if YouTube is fully authenticated
func (c *Config) IsYouTubeConnected() bool {
	return c.GetYouTubeAuthStatus() == youtube.AuthStatusAuthenticated
}

// GetConfigDir returns the configuration directory path
func GetConfigDir() string {
	home, err := os.UserHomeDir()
	if err != nil {
		return DefaultConfigDir
	}
	return filepath.Join(home, DefaultConfigDir)
}

// GetDefaultVideosDir returns the default videos directory path
func GetDefaultVideosDir() string {
	home, err := os.UserHomeDir()
	if err != nil {
		return DefaultVideosDir
	}
	return filepath.Join(home, DefaultVideosDir)
}

// EnsureDirectories creates the necessary directories
func EnsureDirectories() error {
	dirs := []string{
		GetConfigDir(),
		GetDefaultVideosDir(),
	}

	for _, dir := range dirs {
		if err := os.MkdirAll(dir, 0755); err != nil {
			return err
		}
	}

	return nil
}

// Load loads the configuration from disk
func Load() (*Config, error) {
	configPath := filepath.Join(GetConfigDir(), ConfigFileName)

	data, err := os.ReadFile(configPath)
	if err != nil {
		if os.IsNotExist(err) {
			// Return default config if file doesn't exist
			cfg := DefaultConfig()
			return &cfg, nil
		}
		return nil, err
	}

	var cfg Config
	if err := json.Unmarshal(data, &cfg); err != nil {
		return nil, err
	}

	return &cfg, nil
}

// Save saves the configuration to disk
func Save(cfg *Config) error {
	if err := EnsureDirectories(); err != nil {
		return err
	}

	configPath := filepath.Join(GetConfigDir(), ConfigFileName)

	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}

	return os.WriteFile(configPath, data, 0644)
}

// GetNextRecordingNumber returns the next recording number by scanning existing folders
func GetNextRecordingNumber() (int, error) {
	return ScanHighestRecordingNumber() + 1, nil
}

// GetCurrentRecordingNumber returns the next recording number by scanning existing folders
func GetCurrentRecordingNumber() int {
	return ScanHighestRecordingNumber() + 1
}

// ReadPath reads a path from a file
func ReadPath(pathFile string) string {
	data, err := os.ReadFile(pathFile)
	if err != nil {
		return ""
	}
	return string(data)
}

// ScanHighestRecordingNumber scans the output directory for existing recordings
// and returns the highest recording number found (0 if none found)
func ScanHighestRecordingNumber() int {
	cfg, err := Load()
	if err != nil {
		return 0
	}

	outputDir := cfg.OutputDir
	if outputDir == "" {
		outputDir = GetDefaultVideosDir()
	}

	entries, err := os.ReadDir(outputDir)
	if err != nil {
		return 0
	}

	highest := 0
	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}

		// Parse folder name format: NNN-title (e.g., "001-my-video", "042-tutorial")
		name := entry.Name()
		if len(name) < 4 || name[3] != '-' {
			continue
		}

		// Try to parse the first 3 characters as a number
		numStr := name[:3]
		num := 0
		for _, c := range numStr {
			if c >= '0' && c <= '9' {
				num = num*10 + int(c-'0')
			} else {
				num = -1
				break
			}
		}

		if num > highest {
			highest = num
		}
	}

	return highest
}
