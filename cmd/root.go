package cmd

import (
	"fmt"
	"os"

	"github.com/kartoza/kartoza-screencaster/internal/systray"
	"github.com/spf13/cobra"
)

var (
	version           = "0.8.2"
	debugMode         bool
	dataDir           string
	noSplash          bool
	presetsMode       bool
	editRecordingMode bool
)

// SetVersion sets the application version (called from main)
func SetVersion(v string) {
	version = v
}

var rootCmd = &cobra.Command{
	Use:   "kartoza-screencaster",
	Short: "Screen recording and video processing tool for Wayland",
	Long: `Kartoza Screencaster is a screen recording tool for Wayland compositors.

It supports:
  - Multi-monitor screen recording with automatic cursor detection
  - Separate audio recording with noise reduction
  - Webcam recording and vertical video creation
  - Audio normalization using EBU R128 loudness standards
  - Hardware and software video encoding

The tool integrates with Hyprland and other wlroots-based compositors.`,
	Run: func(cmd *cobra.Command, args []string) {
		// If presets or edit-recording mode is requested, run TUI instead of systray
		if presetsMode || editRecordingMode {
			if err := runTUIApp(noSplash, presetsMode); err != nil {
				fmt.Fprintf(os.Stderr, "Error: %v\n", err)
				os.Exit(1)
			}
			return
		}
		// Default action: run systray mode
		systray.RunWithHandler()
	},
}

func Execute() {
	if err := rootCmd.Execute(); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}

func init() {
	rootCmd.PersistentFlags().BoolVar(&debugMode, "debug", false, "Enable debug mode")
	rootCmd.PersistentFlags().StringVar(&dataDir, "data-dir", "", "Data directory (default: ~/.config/kartoza-screencaster)")
	rootCmd.PersistentFlags().BoolVar(&noSplash, "nosplash", false, "Skip splash screens on startup and exit")
	rootCmd.PersistentFlags().BoolVar(&presetsMode, "presets", false, "Open directly to recording presets configuration")
	rootCmd.PersistentFlags().BoolVar(&editRecordingMode, "edit-recording", false, "Open to edit the latest recording that needs metadata")

	// Add subcommands
	rootCmd.AddCommand(toggleCmd)
	rootCmd.AddCommand(startCmd)
	rootCmd.AddCommand(stopCmd)
	rootCmd.AddCommand(pauseCmd)
	rootCmd.AddCommand(resumeCmd)
	rootCmd.AddCommand(statusCmd)
	rootCmd.AddCommand(versionCmd)
	rootCmd.AddCommand(monitorsCmd)
}

var tuiCmd = &cobra.Command{
	Use:   "tui",
	Short: "Run the terminal user interface",
	Long: `Run the Kartoza Screencaster in TUI (terminal user interface) mode.

The TUI provides a full-featured interface for:
  - Starting and stopping recordings with all options
  - Managing recording presets
  - Viewing recording history
  - Editing recording metadata
  - YouTube upload configuration

Use this when you need the complete recording workflow with metadata entry.`,
	Run: func(cmd *cobra.Command, args []string) {
		if err := runTUIApp(noSplash, presetsMode); err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}
	},
}

func init() {
	rootCmd.AddCommand(tuiCmd)
}
