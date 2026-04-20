package cmd

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/kartoza/kartoza-screencaster/internal/config"
	"github.com/kartoza/kartoza-screencaster/internal/models"
	"github.com/kartoza/kartoza-screencaster/internal/monitor"
	"github.com/kartoza/kartoza-screencaster/internal/recorder"
	"github.com/kartoza/kartoza-screencaster/internal/webcam"
	"github.com/spf13/cobra"
)

var (
	monitorName   string
	noAudio       bool
	noWebcam      bool
	noScreen      bool
	hwAccel       bool
	outputDir     string
	webcamDevice  string // Legacy: single webcam device override
	webcamConfig  string // Multi-webcam: JSON config string or file path
	webcamFPS     int
	audioDevice   string
)

var startCmd = &cobra.Command{
	Use:   "start",
	Short: "Start screen recording",
	Long: `Start a new screen recording session.

The recording will capture:
  - Video from the specified monitor (or the one with the cursor)
  - Audio from the default input device (unless --no-audio is set)
  - Webcam video if available (unless --no-webcam is set)

The recording will be saved to a folder with format: NNN-YYYY-MM-DD-HHMMSS
Use 'kartoza-screencaster stop' to stop recording and process files.`,
	RunE: func(cmd *cobra.Command, args []string) error {
		rec := recorder.New()

		if rec.IsRecording() {
			return fmt.Errorf("recording is already in progress")
		}

		// Get next sequence number
		seqNum, err := config.GetNextRecordingNumber()
		if err != nil {
			seqNum = 1
		}

		// Create metadata with timestamp-based title
		timestamp := time.Now().Format("2006-01-02-150405")
		metadata := models.RecordingMetadata{
			Number: seqNum,
			Title:  timestamp,
		}
		metadata.GenerateFolderName()

		// Determine output directory
		recordingDir := outputDir
		if recordingDir == "" {
			baseDir := config.GetDefaultVideosDir()
			recordingDir = filepath.Join(baseDir, metadata.FolderName)
		}

		// Create the recording directory
		if err := os.MkdirAll(recordingDir, 0755); err != nil {
			return fmt.Errorf("failed to create recording directory: %w", err)
		}

		// Get monitor info for recording info
		monitorResolution := "unknown"
		if monitors, err := monitor.ListMonitors(); err == nil && len(monitors) > 0 {
			for _, m := range monitors {
				if monitorName == "" || m.Name == monitorName {
					monitorResolution = fmt.Sprintf("%dx%d", m.Width, m.Height)
					if monitorName == "" {
						monitorName = m.Name
					}
					break
				}
			}
		}

		// Create RecordingInfo
		recordingInfo := models.NewRecordingInfo(metadata, monitorName, monitorResolution)
		recordingInfo.Files.FolderPath = recordingDir

		// Set recording settings
		recordingInfo.Settings.ScreenEnabled = !noScreen
		recordingInfo.Settings.AudioEnabled = !noAudio
		recordingInfo.Settings.WebcamEnabled = !noWebcam
		recordingInfo.Settings.HardwareAccel = hwAccel
		recordingInfo.Settings.AudioDevice = audioDevice
		recordingInfo.Settings.WebcamDevice = webcamDevice
		recordingInfo.Settings.WebcamFPS = webcamFPS

		// Parse multi-webcam config override from CLI flag
		if webcamConfig != "" {
			var configs []webcam.WebcamConfig
			// Try as file path first
			if data, err := os.ReadFile(webcamConfig); err == nil {
				_ = json.Unmarshal(data, &configs)
			} else {
				// Try as inline JSON
				_ = json.Unmarshal([]byte(webcamConfig), &configs)
			}
			if len(configs) > 0 {
				cfg, _ := config.Load()
				cfg.WebcamConfigs = configs
				_ = config.Save(cfg)
			}
		}

		// Save initial recording.json
		if err := recordingInfo.Save(); err != nil {
			return fmt.Errorf("failed to save recording metadata: %w", err)
		}

		opts := recorder.Options{
			Monitor:       monitorName,
			NoAudio:       noAudio,
			NoWebcam:      noWebcam,
			NoScreen:      noScreen,
			HWAccel:       hwAccel,
			OutputDir:     recordingDir,
			WebcamDevice:  webcamDevice,
			WebcamFPS:     webcamFPS,
			AudioDevice:   audioDevice,
			Metadata:      &metadata,
			RecordingInfo: recordingInfo,
		}

		fmt.Printf("Starting recording #%d...\n", seqNum)
		fmt.Printf("Output: %s\n", recordingDir)
		if err := rec.StartWithOptions(opts); err != nil {
			return err
		}

		fmt.Println("Recording started. Use 'kartoza-screencaster stop' to stop.")
		return nil
	},
}

func init() {
	startCmd.Flags().StringVarP(&monitorName, "monitor", "m", "", "Monitor name to record (default: monitor with cursor)")
	startCmd.Flags().BoolVar(&noAudio, "no-audio", false, "Disable audio recording")
	startCmd.Flags().BoolVar(&noWebcam, "no-webcam", false, "Disable webcam recording")
	startCmd.Flags().BoolVar(&noScreen, "no-screen", false, "Disable screen recording")
	startCmd.Flags().BoolVar(&hwAccel, "hw-accel", false, "Enable hardware acceleration (VAAPI)")
	startCmd.Flags().StringVarP(&outputDir, "output", "o", "", "Output directory (default: ~/Videos/Screencasts/NNN-timestamp)")
	startCmd.Flags().StringVar(&webcamDevice, "webcam-device", "", "Webcam device (legacy single device override)")
	startCmd.Flags().StringVar(&webcamConfig, "webcam-config", "", "Multi-webcam config: JSON string or file path")
	startCmd.Flags().IntVar(&webcamFPS, "webcam-fps", 60, "Webcam framerate")
	startCmd.Flags().StringVar(&audioDevice, "audio-device", "@DEFAULT_SOURCE@", "PipeWire audio device")
}
