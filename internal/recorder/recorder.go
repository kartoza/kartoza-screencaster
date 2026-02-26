package recorder

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/kartoza/kartoza-screencaster/internal/audio"
	"github.com/kartoza/kartoza-screencaster/internal/config"
	"github.com/kartoza/kartoza-screencaster/internal/deps"
	"github.com/kartoza/kartoza-screencaster/internal/merger"
	"github.com/kartoza/kartoza-screencaster/internal/models"
	"github.com/kartoza/kartoza-screencaster/internal/monitor"
	"github.com/kartoza/kartoza-screencaster/internal/webcam"
)

// Options for starting a recording
type Options struct {
	Monitor        string
	NoAudio        bool
	NoWebcam       bool
	NoScreen       bool
	HWAccel        bool
	OutputDir      string
	WebcamDevice   string
	WebcamFPS      int
	AudioDevice    string
	Metadata       *models.RecordingMetadata
	RecordingInfo  *models.RecordingInfo
	CreateVertical bool
	LogoSelection  config.LogoSelection
}

// recorderInstance holds a single recorder's state
type recorderInstance struct {
	name    string
	cmd     *exec.Cmd
	pid     int
	file    string
	err     error
	started bool
}

// Recorder manages screen recording sessions
type Recorder struct {
	config *config.Config
	mu     sync.Mutex

	// Active recorder instances
	video  *recorderInstance
	audio  *recorderInstance
	webcam *recorderInstance

	// Recording metadata
	recordingInfo  *models.RecordingInfo
	createVertical bool
	logoSelection  config.LogoSelection

	// Room noise file for audio processing calibration
	roomNoiseFile string

	// Synchronization
	startBarrier chan struct{}
	stopSignal   chan struct{}
	wg           sync.WaitGroup
}

// New creates a new Recorder
func New() *Recorder {
	cfg, _ := config.Load()
	return &Recorder{config: cfg}
}

// IsRecording checks if any recording is currently in progress
func (r *Recorder) IsRecording() bool {
	r.mu.Lock()
	defer r.mu.Unlock()

	return checkPID(config.VideoPIDFile) ||
		checkPID(config.AudioPIDFile) ||
		checkPID(config.WebcamPIDFile)
}

// SetRecordingInfo sets the recording info for reprocessing
func (r *Recorder) SetRecordingInfo(info *models.RecordingInfo) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.recordingInfo = info
	if info != nil {
		r.createVertical = info.Settings.VerticalEnabled
		// Restore logo selection from saved settings so processing can find them
		if info.Settings.LogosEnabled {
			r.logoSelection = config.LogoSelection{
				LeftLogo:    info.Settings.LeftLogo,
				RightLogo:   info.Settings.RightLogo,
				BottomLogo:  info.Settings.BottomLogo,
				TitleColor:  info.Settings.TitleColor,
				GifLoopMode: config.GifLoopMode(info.Settings.GifLoopMode),
			}
		}
	}
}

// SetRoomNoiseFile sets the room noise file path for audio processing calibration
func (r *Recorder) SetRoomNoiseFile(path string) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.roomNoiseFile = path
}

// GetStatus returns the current recording status
func (r *Recorder) GetStatus() models.RecordingStatus {
	r.mu.Lock()
	defer r.mu.Unlock()

	isPaused := r.IsPaused()
	status := models.RecordingStatus{
		IsRecording: checkPID(config.VideoPIDFile) ||
			checkPID(config.AudioPIDFile) ||
			checkPID(config.WebcamPIDFile),
		IsPaused:    isPaused,
		CurrentPart: readPartNumber(),
	}

	if status.IsRecording || isPaused {
		status.VideoPID = readPID(config.VideoPIDFile)
		status.AudioPID = readPID(config.AudioPIDFile)
		status.WebcamPID = readPID(config.WebcamPIDFile)
		status.VideoFile = readPath(config.VideoPathFile)
		status.AudioFile = readPath(config.AudioPathFile)
		status.WebcamFile = readPath(config.WebcamPathFile)

		// Get monitor name - from instance if available, else extract from filename
		if r.video != nil {
			status.Monitor = r.video.name
		} else if status.VideoFile != "" {
			// Extract monitor name from filename: screenrecording-<monitor>-<timestamp>.mp4
			status.Monitor = extractMonitorFromPath(status.VideoFile)
		}

		// Read start time from status file
		if data, err := os.ReadFile(config.StatusFile); err == nil {
			if t, err := time.Parse(time.RFC3339, string(data)); err == nil {
				status.StartTime = t
			}
		}
	}

	return status
}

// Start starts a recording with default options
func (r *Recorder) Start() error {
	return r.StartWithOptions(Options{})
}

// StartWithOptions starts a recording with the given options
func (r *Recorder) StartWithOptions(opts Options) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	if r.IsRecordingLocked() {
		return fmt.Errorf("recording already in progress")
	}

	// Ensure output directory exists
	outputDir := opts.OutputDir
	if outputDir == "" {
		outputDir = config.GetDefaultVideosDir()
	}
	if err := os.MkdirAll(outputDir, 0755); err != nil {
		return fmt.Errorf("failed to create output directory: %w", err)
	}

	// Get monitor to record
	monitorName := opts.Monitor
	if monitorName == "" {
		var err error
		monitorName, err = monitor.GetMouseMonitor()
		if err != nil {
			// Fallback to first monitor
			monitors, err := monitor.ListMonitors()
			if err != nil || len(monitors) == 0 {
				return fmt.Errorf("no monitors found")
			}
			monitorName = monitors[0].Name
		}
	}

	// Store recording info and settings
	r.recordingInfo = opts.RecordingInfo
	r.createVertical = opts.CreateVertical
	r.logoSelection = opts.LogoSelection

	// Determine part number: reset to 0 for new recordings, use current for resume
	var partNum int
	if r.recordingInfo != nil && len(r.recordingInfo.Files.VideoParts) == 0 &&
		len(r.recordingInfo.Files.AudioParts) == 0 && len(r.recordingInfo.Files.WebcamParts) == 0 {
		// New recording - reset part number to 0
		partNum = 0
		writePartNumber(0)
	} else {
		// Resume - use current part number (already incremented by Pause)
		partNum = readPartNumber()
	}

	// Generate filenames with part number suffix
	videoFile := filepath.Join(outputDir, fmt.Sprintf("screen_part%03d.mp4", partNum))
	audioFile := filepath.Join(outputDir, fmt.Sprintf("audio_part%03d.wav", partNum))
	webcamFile := filepath.Join(outputDir, fmt.Sprintf("webcam_part%03d.mp4", partNum))

	// Initialize recorder instances based on options
	if !opts.NoScreen {
		r.video = &recorderInstance{name: monitorName, file: videoFile}
	}
	if !opts.NoAudio {
		r.audio = &recorderInstance{name: "audio", file: audioFile}
	}
	if !opts.NoWebcam {
		r.webcam = &recorderInstance{name: "webcam", file: webcamFile}
	}

	// Update recording info with file paths and part tracking
	if r.recordingInfo != nil {
		r.recordingInfo.Files.CurrentPart = partNum
		if r.video != nil {
			r.recordingInfo.Files.VideoFile = videoFile
			r.recordingInfo.Files.VideoParts = append(r.recordingInfo.Files.VideoParts, videoFile)
		}
		if r.audio != nil {
			r.recordingInfo.Files.AudioFile = audioFile
			r.recordingInfo.Files.AudioParts = append(r.recordingInfo.Files.AudioParts, audioFile)
		}
		if r.webcam != nil {
			r.recordingInfo.Files.WebcamFile = webcamFile
			r.recordingInfo.Files.WebcamParts = append(r.recordingInfo.Files.WebcamParts, webcamFile)
		}
		// Save updated file paths
		_ = r.recordingInfo.Save()
	}

	// Save output directory and part number for CLI commands
	_ = os.WriteFile(config.OutputDirFile, []byte(outputDir), 0644)
	writePartNumber(partNum)

	// Create synchronization primitives
	r.startBarrier = make(chan struct{})
	r.stopSignal = make(chan struct{})

	// Count how many recorders we're starting
	numRecorders := 0
	if r.video != nil {
		numRecorders++
	}
	if r.audio != nil {
		numRecorders++
	}
	if r.webcam != nil {
		numRecorders++
	}

	// Must have at least one recorder
	if numRecorders == 0 {
		return fmt.Errorf("no recording sources enabled")
	}

	// Channel to collect readiness and started confirmation
	ready := make(chan string, numRecorders)
	started := make(chan string, numRecorders)
	errors := make(chan error, numRecorders)

	// Start video recorder in goroutine (if enabled)
	if r.video != nil {
		r.wg.Add(1)
		go func() {
			defer r.wg.Done()
			r.startVideoRecorder(opts.HWAccel, ready, started, errors)
		}()
	}

	// Start audio recorder in goroutine
	if r.audio != nil {
		audioDevice := opts.AudioDevice
		if audioDevice == "" {
			audioDevice = "@DEFAULT_SOURCE@"
		}
		r.wg.Add(1)
		go func() {
			defer r.wg.Done()
			r.startAudioRecorder(audioDevice, ready, started, errors)
		}()
	}

	// Start webcam recorder in goroutine
	if r.webcam != nil {
		r.wg.Add(1)
		go func() {
			defer r.wg.Done()
			r.startWebcamRecorder(opts, ready, started, errors)
		}()
	}

	// Wait for all recorders to be ready (with timeout)
	readyCount := 0
	timeout := time.After(5 * time.Second)

waitReady:
	for readyCount < numRecorders {
		select {
		case name := <-ready:
			readyCount++
			_ = name // Could log which recorder is ready
		case err := <-errors:
			// Non-fatal errors for audio/webcam - log but don't notify (appears in recording)
			_ = err // Could log: err.Error()
			numRecorders-- // Reduce expected count
		case <-timeout:
			// Proceed with what we have
			break waitReady
		}
	}

	// All ready - signal them to start simultaneously
	close(r.startBarrier)

	// Wait for recorders to actually start (with timeout)
	startedCount := 0
	expectedStarted := numRecorders
	startTimeout := time.After(5 * time.Second)

waitStarted:
	for startedCount < expectedStarted {
		select {
		case name := <-started:
			startedCount++
			_ = name
		case err := <-errors:
			// Error during start - reduce expected count (don't notify, appears in recording)
			_ = err // Could log: err.Error()
			expectedStarted--
		case <-startTimeout:
			break waitStarted
		}
	}

	// Record start time
	startTime := time.Now()
	_ = os.WriteFile(config.StatusFile, []byte(startTime.Format(time.RFC3339)), 0644)

	// Store file paths and PIDs
	if r.video != nil && r.video.started {
		writePID(config.VideoPIDFile, r.video.pid)
		_ = os.WriteFile(config.VideoPathFile, []byte(r.video.file), 0644)
	}
	if r.audio != nil && r.audio.started {
		writePID(config.AudioPIDFile, r.audio.pid)
		_ = os.WriteFile(config.AudioPathFile, []byte(r.audio.file), 0644)
	}
	if r.webcam != nil && r.webcam.started {
		writePID(config.WebcamPIDFile, r.webcam.pid)
		_ = os.WriteFile(config.WebcamPathFile, []byte(r.webcam.file), 0644)
	}

	// Note: Don't send desktop notifications during recording as they appear in the recording
	return nil
}

// startVideoRecorder starts the video recorder and waits for the start signal
func (r *Recorder) startVideoRecorder(hwAccel bool, ready, started chan<- string, errors chan<- error) {
	platform := deps.DetectPlatform()

	switch platform {
	case deps.PlatformDarwin:
		r.startVideoRecorderDarwin(ready, started, errors)
	case deps.PlatformWindows:
		r.startVideoRecorderWindows(ready, started, errors)
	case deps.PlatformLinux:
		// Check display server on Linux
		displayServer := deps.DetectDisplayServer()
		switch displayServer {
		case deps.DisplayServerX11:
			r.startVideoRecorderX11(ready, started, errors)
		default:
			// Wayland - check compositor type
			compositor := os.Getenv("XDG_CURRENT_DESKTOP")
			if compositor == "GNOME" || compositor == "ubuntu:GNOME" || compositor == "gnome" {
				// GNOME doesn't support wl-screenrec, use PipeWire screencast portal
				r.startVideoRecorderGnome(ready, started, errors)
			} else {
				// wlroots-based (Hyprland, Sway, etc) - use wl-screenrec
				r.startVideoRecorderWayland(hwAccel, ready, started, errors)
			}
		}
	default:
		// Unknown platform - try Wayland
		r.startVideoRecorderWayland(hwAccel, ready, started, errors)
	}
}

// startVideoRecorderWayland starts video recording using wl-screenrec (Wayland)
func (r *Recorder) startVideoRecorderWayland(hwAccel bool, ready, started chan<- string, errors chan<- error) {
	args := []string{}

	// Software encoding by default (more compatible)
	if !hwAccel {
		args = append(args, "--no-hw")
	}

	args = append(args,
		"--output="+r.video.name,
		"--filename="+r.video.file,
		"--encode-pixfmt", "yuv420p",
	)

	r.video.cmd = exec.Command("wl-screenrec", args...)
	r.video.cmd.Stdout = nil
	r.video.cmd.Stderr = nil

	// Signal we're ready
	ready <- "video"

	// Wait for synchronized start signal
	<-r.startBarrier

	if err := r.video.cmd.Start(); err != nil {
		r.video.err = fmt.Errorf("failed to start wl-screenrec: %w", err)
		errors <- r.video.err
		return
	}

	r.video.pid = r.video.cmd.Process.Pid
	r.video.started = true

	// Signal that we've started
	started <- "video"

	// Wait for stop signal or process exit
	done := make(chan error, 1)
	go func() {
		done <- r.video.cmd.Wait()
	}()

	select {
	case <-r.stopSignal:
		// Stop requested
	case err := <-done:
		if err != nil {
			r.video.err = err
		}
	}
}

// startVideoRecorderX11 starts video recording using ffmpeg with x11grab (X11)
func (r *Recorder) startVideoRecorderX11(ready, started chan<- string, errors chan<- error) {
	// Get monitor info for position and size
	mon, err := monitor.GetMonitorByName(r.video.name)
	if err != nil {
		// Fallback to full screen capture
		mon = &models.Monitor{X: 0, Y: 0, Width: 1920, Height: 1080}
	}

	// Build ffmpeg x11grab command
	// ffmpeg -f x11grab -framerate 60 -video_size WxH -i :0.0+X,Y -c:v libx264 -pix_fmt yuv420p output.mp4
	display := os.Getenv("DISPLAY")
	if display == "" {
		display = ":0"
	}

	args := []string{
		"-f", "x11grab",
		"-framerate", "60",
		"-video_size", fmt.Sprintf("%dx%d", mon.Width, mon.Height),
		"-i", fmt.Sprintf("%s+%d,%d", display, mon.X, mon.Y),
		"-c:v", "libx264",
		"-preset", "ultrafast",
		"-pix_fmt", "yuv420p",
		"-y", // Overwrite output
		r.video.file,
	}

	r.video.cmd = exec.Command("ffmpeg", args...)
	r.video.cmd.Stdout = nil
	r.video.cmd.Stderr = nil

	// Signal we're ready
	ready <- "video"

	// Wait for synchronized start signal
	<-r.startBarrier

	if err := r.video.cmd.Start(); err != nil {
		r.video.err = fmt.Errorf("failed to start ffmpeg x11grab: %w", err)
		errors <- r.video.err
		return
	}

	r.video.pid = r.video.cmd.Process.Pid
	r.video.started = true

	// Signal that we've started
	started <- "video"

	// Wait for stop signal or process exit
	done := make(chan error, 1)
	go func() {
		done <- r.video.cmd.Wait()
	}()

	select {
	case <-r.stopSignal:
		// Stop requested
	case err := <-done:
		if err != nil {
			r.video.err = err
		}
	}
}

// startVideoRecorderGnome starts video recording using GStreamer with PipeWire screencast portal (GNOME Wayland)
func (r *Recorder) startVideoRecorderGnome(ready, started chan<- string, errors chan<- error) {
	// Use GStreamer with pipewiresrc for GNOME's screencast portal
	// This uses the xdg-desktop-portal-gnome screencast interface
	// The pipeline: pipewiresrc ! videoconvert ! x264enc ! mp4mux ! filesink
	pipeline := fmt.Sprintf(
		"pipewiresrc ! videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast ! mp4mux ! filesink location=%s",
		r.video.file,
	)

	args := []string{
		"-e", // Send EOS on interrupt for clean shutdown
		pipeline,
	}

	r.video.cmd = exec.Command("gst-launch-1.0", args...)
	r.video.cmd.Stdout = nil
	r.video.cmd.Stderr = nil

	// Signal we're ready
	ready <- "video"

	// Wait for synchronized start signal
	<-r.startBarrier

	if err := r.video.cmd.Start(); err != nil {
		r.video.err = fmt.Errorf("failed to start gst-launch-1.0 for GNOME screencast: %w", err)
		errors <- r.video.err
		return
	}

	r.video.pid = r.video.cmd.Process.Pid
	r.video.started = true

	// Signal that we've started
	started <- "video"

	// Wait for stop signal or process exit
	done := make(chan error, 1)
	go func() {
		done <- r.video.cmd.Wait()
	}()

	select {
	case <-r.stopSignal:
		// Stop requested - send SIGINT for clean EOS
	case err := <-done:
		if err != nil {
			r.video.err = err
		}
	}
}

// startVideoRecorderDarwin starts video recording using ffmpeg with avfoundation (macOS)
func (r *Recorder) startVideoRecorderDarwin(ready, started chan<- string, errors chan<- error) {
	// Get screen index - default to 0 (main display)
	// Users can run: ffmpeg -f avfoundation -list_devices true -i ""
	// to find available screens
	screenIndex := "1" // "1" is typically the screen capture device on macOS

	// Build ffmpeg avfoundation command for screen capture
	// Format: -i "screen_index:" (colon with no audio index means video only)
	args := []string{
		"-f", "avfoundation",
		"-framerate", "60",
		"-capture_cursor", "1",
		"-i", screenIndex + ":",
		"-c:v", "libx264",
		"-preset", "ultrafast",
		"-pix_fmt", "yuv420p",
		"-y",
		r.video.file,
	}

	r.video.cmd = exec.Command("ffmpeg", args...)
	r.video.cmd.Stdout = nil
	r.video.cmd.Stderr = nil

	// Signal we're ready
	ready <- "video"

	// Wait for synchronized start signal
	<-r.startBarrier

	if err := r.video.cmd.Start(); err != nil {
		r.video.err = fmt.Errorf("failed to start ffmpeg avfoundation: %w", err)
		errors <- r.video.err
		return
	}

	r.video.pid = r.video.cmd.Process.Pid
	r.video.started = true

	// Signal that we've started
	started <- "video"

	// Wait for stop signal or process exit
	done := make(chan error, 1)
	go func() {
		done <- r.video.cmd.Wait()
	}()

	select {
	case <-r.stopSignal:
		// Stop requested
	case err := <-done:
		if err != nil {
			r.video.err = err
		}
	}
}

// startVideoRecorderWindows starts video recording using ffmpeg with gdigrab (Windows)
func (r *Recorder) startVideoRecorderWindows(ready, started chan<- string, errors chan<- error) {
	// gdigrab captures the entire desktop or a specific window
	// Use "desktop" for full screen capture

	// Build ffmpeg gdigrab command
	args := []string{
		"-f", "gdigrab",
		"-framerate", "60",
		"-i", "desktop",
		"-c:v", "libx264",
		"-preset", "ultrafast",
		"-pix_fmt", "yuv420p",
		"-y",
		r.video.file,
	}

	r.video.cmd = exec.Command("ffmpeg", args...)
	r.video.cmd.Stdout = nil
	r.video.cmd.Stderr = nil

	// Signal we're ready
	ready <- "video"

	// Wait for synchronized start signal
	<-r.startBarrier

	if err := r.video.cmd.Start(); err != nil {
		r.video.err = fmt.Errorf("failed to start ffmpeg gdigrab: %w", err)
		errors <- r.video.err
		return
	}

	r.video.pid = r.video.cmd.Process.Pid
	r.video.started = true

	// Signal that we've started
	started <- "video"

	// Wait for stop signal or process exit
	done := make(chan error, 1)
	go func() {
		done <- r.video.cmd.Wait()
	}()

	select {
	case <-r.stopSignal:
		// Stop requested
	case err := <-done:
		if err != nil {
			r.video.err = err
		}
	}
}

// startAudioRecorder starts the audio recorder and waits for the start signal
func (r *Recorder) startAudioRecorder(device string, ready, started chan<- string, errors chan<- error) {
	audioRecorder := audio.NewRecorder(device, r.audio.file)

	// Signal we're ready
	ready <- "audio"

	// Wait for synchronized start signal
	<-r.startBarrier

	if err := audioRecorder.Start(); err != nil {
		r.audio.err = fmt.Errorf("failed to start audio recording: %w", err)
		errors <- r.audio.err
		return
	}

	r.audio.pid = audioRecorder.PID()
	r.audio.started = true

	// Signal that we've started
	started <- "audio"

	// Wait for stop signal
	<-r.stopSignal
}

// startWebcamRecorder starts the webcam recorder and waits for the start signal
func (r *Recorder) startWebcamRecorder(opts Options, ready, started chan<- string, errors chan<- error) {
	webcamOpts := webcam.Options{
		Device:     opts.WebcamDevice,
		FPS:        opts.WebcamFPS,
		Resolution: "1920x1080",
		OutputFile: r.webcam.file,
	}

	if webcamOpts.FPS == 0 {
		webcamOpts.FPS = 60
	}

	cam := webcam.New(webcamOpts)

	// Signal we're ready
	ready <- "webcam"

	// Wait for synchronized start signal
	<-r.startBarrier

	if err := cam.Start(); err != nil {
		r.webcam.err = fmt.Errorf("no webcam detected: %w", err)
		errors <- r.webcam.err
		return
	}

	r.webcam.pid = cam.PID()
	r.webcam.started = true

	// Signal that we've started
	started <- "webcam"

	// Wait for stop signal
	<-r.stopSignal
}

// Stop stops the current recording (processing runs in background for TUI use)
func (r *Recorder) Stop() error {
	return r.stopInternal(false)
}

// StopAndProcess stops the recording and optionally waits for processing to complete
// If process is true, waits for all post-processing to finish before returning
// If process is false, only stops recording without post-processing
func (r *Recorder) StopAndProcess(process bool) error {
	if err := r.stopInternal(process); err != nil {
		return err
	}
	return nil
}

// stopInternal is the internal stop implementation
func (r *Recorder) stopInternal(waitForProcessing bool) error {
	r.mu.Lock()

	// Check if we have an active recording OR a paused recording session
	isPaused := r.IsPaused()
	isRecording := r.IsRecordingLocked()

	if !isRecording && !isPaused {
		r.mu.Unlock()
		return fmt.Errorf("no recording in progress")
	}

	// If paused, clear the paused state
	if isPaused {
		_ = os.Remove(config.PausedFile)
	}

	// Only stop processes if we're actively recording (not just paused)
	if isRecording {
		// Signal all recorders to stop simultaneously (only if we started them in this process)
		shouldWaitForGoroutines := r.stopSignal != nil
		if r.stopSignal != nil {
			close(r.stopSignal)
			r.stopSignal = nil
		}

		// Stop all processes simultaneously using goroutines
		var stopWg sync.WaitGroup

		// Stop video
		if pid := readPID(config.VideoPIDFile); pid > 0 {
			stopWg.Add(1)
			go func(p int) {
				defer stopWg.Done()
				_ = stopProcess(p)
				_ = os.Remove(config.VideoPIDFile)
			}(pid)
		}

		// Stop audio
		if pid := readPID(config.AudioPIDFile); pid > 0 {
			stopWg.Add(1)
			go func(p int) {
				defer stopWg.Done()
				_ = stopProcess(p)
				_ = os.Remove(config.AudioPIDFile)
			}(pid)
		}

		// Stop webcam
		if pid := readPID(config.WebcamPIDFile); pid > 0 {
			stopWg.Add(1)
			go func(p int) {
				defer stopWg.Done()
				_ = stopProcess(p)
				_ = os.Remove(config.WebcamPIDFile)
			}(pid)
		}

		// Wait for all stop operations to complete
		stopWg.Wait()

		// Wait for recorder goroutines to finish (only if we started them)
		if shouldWaitForGoroutines {
			r.wg.Wait()
		}

		// Note: Don't send desktop notifications during recording as they appear in the recording

		// Wait for files to be fully written (only if we were actively recording)
		time.Sleep(2 * time.Second)
	}

	// Load output directory from file if not already set (CLI stop case)
	outputDir := readPath(config.OutputDirFile)

	// Update recording info with end time, file sizes, and status
	if r.recordingInfo != nil {
		r.recordingInfo.SetEndTime(time.Now())
		r.recordingInfo.SetStatus(models.StatusProcessing)
		r.recordingInfo.UpdateFileSizes()
		_ = r.recordingInfo.Save()
	} else if outputDir != "" {
		// Try to load recording info from output directory (CLI stop case)
		if info, err := models.LoadRecordingInfo(outputDir); err == nil {
			r.recordingInfo = info
			r.recordingInfo.SetEndTime(time.Now())
			r.recordingInfo.SetStatus(models.StatusProcessing)
			r.recordingInfo.UpdateFileSizes()
			_ = r.recordingInfo.Save()
			// Set createVertical from recording info settings
			r.createVertical = info.Settings.VerticalEnabled
		}
	}

	// Clear instances
	r.video = nil
	r.audio = nil
	r.webcam = nil

	// Clean up state files
	_ = os.Remove(config.PartNumberFile)
	_ = os.Remove(config.OutputDirFile)

	r.mu.Unlock()

	// Process recordings synchronously only if requested (CLI mode)
	// For TUI mode (waitForProcessing=false), the TUI will call ProcessWithProgress() itself
	if waitForProcessing {
		// Run synchronously for CLI
		fmt.Println("Processing recordings...")
		r.processRecordingsWithOutput()
	}
	// Note: For TUI, don't start processing here - the TUI handles it via ProcessWithProgress()

	return nil
}

// processRecordingsWithOutput processes recordings with console output for CLI use
func (r *Recorder) processRecordingsWithOutput() {
	progressChan := make(chan ProgressUpdate, 10)

	// Process updates in a goroutine
	done := make(chan struct{})
	go func() {
		stepNames := []string{
			"Stopping recorders",
			"Analyzing audio",
			"Normalizing audio",
			"Merging video and audio",
			"Creating vertical video",
		}
		for update := range progressChan {
			if update.Step >= 0 && update.Step < len(stepNames) {
				if update.Skipped {
					fmt.Printf("  [SKIP] %s\n", stepNames[update.Step])
				} else if update.Completed {
					fmt.Printf("  [DONE] %s\n", stepNames[update.Step])
				} else if update.Percent >= 0 {
					// Progress update - could show a progress bar
					fmt.Printf("  [....] %s: %.0f%%\r", stepNames[update.Step], update.Percent)
				} else if update.Error != nil {
					fmt.Printf("  [FAIL] %s: %v\n", stepNames[update.Step], update.Error)
				} else {
					fmt.Printf("  [....] %s\n", stepNames[update.Step])
				}
			}
		}
		close(done)
	}()

	r.ProcessWithProgress(progressChan)
	<-done
}

// IsRecordingLocked checks recording status without locking (internal use)
func (r *Recorder) IsRecordingLocked() bool {
	return checkPID(config.VideoPIDFile) ||
		checkPID(config.AudioPIDFile) ||
		checkPID(config.WebcamPIDFile)
}

// ProgressUpdate represents a progress update from the processing pipeline
type ProgressUpdate struct {
	Step      int     // Step index (0-based, add 1 for TUI which has "stopping recorders" as step 0)
	Completed bool
	Skipped   bool
	Error     error
	Percent   float64 // Progress percentage (0-100), -1 means not a percent update
}

// ProcessWithProgress processes recordings and sends progress updates to the channel
func (r *Recorder) ProcessWithProgress(progressChan chan<- ProgressUpdate) {
	defer close(progressChan)

	// Try to load recording info from output directory if not already loaded
	if r.recordingInfo == nil {
		outputDir := readPath(config.OutputDirFile)
		if outputDir != "" {
			if info, err := models.LoadRecordingInfo(outputDir); err == nil {
				r.recordingInfo = info
			}
		}
	}

	// Clear previous processing errors (in case of reprocessing)
	if r.recordingInfo != nil {
		r.recordingInfo.Processing.Errors = nil
		r.recordingInfo.Processing.ErrorDetail = ""
		r.recordingInfo.Processing.Traceback = ""
	}

	// Get file paths from recording info or fallback to path files
	var videoFile, audioFile, webcamFile string
	if r.recordingInfo != nil {
		videoFile = r.recordingInfo.Files.VideoFile
		audioFile = r.recordingInfo.Files.AudioFile
		webcamFile = r.recordingInfo.Files.WebcamFile
	} else {
		videoFile = readPath(config.VideoPathFile)
		audioFile = readPath(config.AudioPathFile)
		webcamFile = readPath(config.WebcamPathFile)
	}

	if videoFile == "" && audioFile == "" {
		// No input files found - save error to recording info
		if r.recordingInfo != nil {
			errMsg := "No video or audio files found to process"
			r.recordingInfo.Processing.Errors = append(r.recordingInfo.Processing.Errors, errMsg)
			r.recordingInfo.Processing.ErrorDetail = buildErrorDetailFromMessage(errMsg, r.recordingInfo.Files.FolderPath)
			r.recordingInfo.Processing.Traceback = captureTraceback()
			r.recordingInfo.SetStatus(models.StatusFailed)
			_ = r.recordingInfo.Save()
		}
		// Don't show desktop notification - error is already shown in TUI
		return
	}

	m := merger.New(r.config.AudioProcessing)

	// Set up progress callback
	m.SetProgressCallback(func(step merger.ProcessingStep, completed bool, skipped bool, err error) {
		// Map merger steps to TUI steps (add 1 because TUI step 0 is "stopping recorders")
		tuiStep := int(step) + 1
		progressChan <- ProgressUpdate{
			Step:      tuiStep,
			Completed: completed,
			Skipped:   skipped,
			Error:     err,
			Percent:   -1, // Not a percent update
		}
	})

	// Set up percent callback for progress bars
	m.SetPercentCallback(func(step merger.ProcessingStep, percent float64) {
		tuiStep := int(step) + 1
		progressChan <- ProgressUpdate{
			Step:    tuiStep,
			Percent: percent,
		}
	})

	// Build merge options
	mergeOpts := merger.MergeOptions{
		VideoFile:      videoFile,
		AudioFile:      audioFile,
		WebcamFile:     webcamFile,
		RoomNoiseFile:  r.roomNoiseFile,
		CreateVertical: r.createVertical && webcamFile != "",
		LeftSplit:      r.recordingInfo != nil && r.recordingInfo.Settings.LeftSplitEnabled,
	}
	// Add part files if available (for pause/resume support)
	if r.recordingInfo != nil && len(r.recordingInfo.Files.VideoParts) > 0 {
		mergeOpts.VideoParts = r.recordingInfo.Files.VideoParts
		mergeOpts.AudioParts = r.recordingInfo.Files.AudioParts
		mergeOpts.WebcamParts = r.recordingInfo.Files.WebcamParts
	}

	// Add logo options from the recording's logo selection (in-memory)
	// or from recording info settings (CLI stop case)
	if r.logoSelection.LeftLogo != "" || r.logoSelection.RightLogo != "" || r.logoSelection.BottomLogo != "" {
		mergeOpts.ProductLogo1 = r.logoSelection.LeftLogo
		mergeOpts.ProductLogo2 = r.logoSelection.RightLogo
		mergeOpts.CompanyLogo = r.logoSelection.BottomLogo
		mergeOpts.TitleColor = r.logoSelection.TitleColor
		mergeOpts.GifLoopMode = r.logoSelection.GifLoopMode
	} else if r.recordingInfo != nil {
		// Load from recording info settings (CLI stop case)
		mergeOpts.ProductLogo1 = r.recordingInfo.Settings.LeftLogo
		mergeOpts.ProductLogo2 = r.recordingInfo.Settings.RightLogo
		mergeOpts.CompanyLogo = r.recordingInfo.Settings.BottomLogo
		mergeOpts.TitleColor = r.recordingInfo.Settings.TitleColor
		mergeOpts.GifLoopMode = config.GifLoopMode(r.recordingInfo.Settings.GifLoopMode)
		mergeOpts.CreateVertical = r.recordingInfo.Settings.VerticalEnabled && webcamFile != ""
	}
	// Check if any logos are configured
	mergeOpts.AddLogos = mergeOpts.ProductLogo1 != "" || mergeOpts.ProductLogo2 != "" || mergeOpts.CompanyLogo != ""
	// Set background color: prefer saved recording setting, fall back to config
	if r.recordingInfo != nil && r.recordingInfo.Settings.BgColor != "" {
		mergeOpts.BgColor = r.recordingInfo.Settings.BgColor
	} else if r.config != nil && r.config.BgColor != "" {
		mergeOpts.BgColor = r.config.BgColor
	}
	// Get video title, number, and output directory from recording info
	if r.recordingInfo != nil {
		mergeOpts.VideoTitle = r.recordingInfo.Metadata.Title
		mergeOpts.VideoNumber = r.recordingInfo.Metadata.Number
		mergeOpts.OutputDir = r.recordingInfo.Files.FolderPath
	}

	mergeResult, err := m.Merge(mergeOpts)

	hasErrors := false
	if err != nil {
		// Don't show desktop notification - error is already shown in TUI
		hasErrors = true
		if r.recordingInfo != nil {
			r.recordingInfo.Processing.Errors = append(r.recordingInfo.Processing.Errors, err.Error())
			// Capture detailed error information for user diagnostics
			r.recordingInfo.Processing.ErrorDetail = buildErrorDetail(err, mergeOpts)
			r.recordingInfo.Processing.Traceback = captureTraceback()
		}
	}

	// Update recording info with merged file paths and processing info
	if r.recordingInfo != nil {
		if mergeResult != nil {
			if mergeResult.MergedFile != "" {
				r.recordingInfo.Files.MergedFile = mergeResult.MergedFile
			}
			if mergeResult.VerticalFile != "" {
				r.recordingInfo.Files.VerticalFile = mergeResult.VerticalFile
			}
			r.recordingInfo.Processing.NormalizeApplied = mergeResult.NormalizeApplied
			r.recordingInfo.Processing.VerticalCreated = mergeResult.VerticalFile != ""
			// Capture vertical video errors (these were previously lost)
			if mergeResult.VerticalError != nil {
				r.recordingInfo.Processing.Errors = append(r.recordingInfo.Processing.Errors,
					"vertical video: "+mergeResult.VerticalError.Error())
			}
		}
		r.recordingInfo.Processing.ProcessedAt = time.Now()
		r.recordingInfo.UpdateFileSizes()

		// Update video metadata (resolution, fps, aspect ratio)
		r.recordingInfo.UpdateVideoMetadata(func(filepath string) (*models.VideoFileMetadata, error) {
			meta, err := webcam.GetFullVideoInfo(filepath)
			if err != nil {
				return nil, err
			}
			return &models.VideoFileMetadata{
				Width:       meta.Width,
				Height:      meta.Height,
				FPS:         meta.FPS,
				AspectRatio: meta.AspectRatio,
				Duration:    meta.Duration,
				Codec:       meta.Codec,
			}, nil
		})

		// Set final status based on whether there were errors
		if hasErrors {
			r.recordingInfo.SetStatus(models.StatusFailed)
		} else {
			r.recordingInfo.SetStatus(models.StatusCompleted)
		}

		_ = r.recordingInfo.Save()
	}

	// Clean up path files
	_ = os.Remove(config.VideoPathFile)
	_ = os.Remove(config.AudioPathFile)
	_ = os.Remove(config.WebcamPathFile)
	_ = os.Remove(config.StatusFile)
	_ = os.Remove(config.OutputDirFile)
	_ = os.Remove(config.PartNumberFile)
	_ = os.Remove(config.PausedFile)
}

// Helper functions

func checkPID(pidFile string) bool {
	pid := readPID(pidFile)
	if pid <= 0 {
		return false
	}

	// Check if process exists
	process, err := os.FindProcess(pid)
	if err != nil {
		return false
	}

	err = process.Signal(syscall.Signal(0))
	return err == nil
}

func readPID(pidFile string) int {
	data, err := os.ReadFile(pidFile)
	if err != nil {
		return 0
	}

	pid, err := strconv.Atoi(string(data))
	if err != nil {
		return 0
	}

	return pid
}

func writePID(pidFile string, pid int) {
	_ = os.WriteFile(pidFile, []byte(strconv.Itoa(pid)), 0644)
}

func readPath(pathFile string) string {
	data, err := os.ReadFile(pathFile)
	if err != nil {
		return ""
	}
	return string(data)
}

// extractMonitorFromPath extracts monitor name from filename like:
// screenrecording-HDMI-A-1-20260118-103045.mp4 -> HDMI-A-1
func extractMonitorFromPath(filePath string) string {
	base := filepath.Base(filePath)
	// Remove extension
	name := base[:len(base)-len(filepath.Ext(base))]

	// Expected format: screenrecording-<monitor>-<timestamp>
	const prefix = "screenrecording-"
	if len(name) <= len(prefix) {
		return ""
	}
	name = name[len(prefix):]

	// Find the timestamp part (format: YYYYMMDD-HHMMSS)
	// Look for pattern: -NNNNNNNN-NNNNNN at the end
	// The timestamp is 15 chars: YYYYMMDD-HHMMSS
	if len(name) > 16 && name[len(name)-7] == '-' {
		// Find where the date starts (should be preceded by a -)
		for i := len(name) - 16; i >= 0; i-- {
			if name[i] == '-' && i > 0 {
				return name[:i]
			}
		}
	}

	return name
}

func stopProcess(pid int) error {
	process, err := os.FindProcess(pid)
	if err != nil {
		return err
	}

	// Send SIGINT for graceful shutdown
	if err := process.Signal(syscall.SIGINT); err != nil {
		// If SIGINT fails, try SIGTERM
		if err := process.Signal(syscall.SIGTERM); err != nil {
			return process.Kill()
		}
	}

	// Wait for process to finish (max 2 seconds)
	for i := 0; i < 10; i++ {
		if err := process.Signal(syscall.Signal(0)); err != nil {
			break
		}
		time.Sleep(200 * time.Millisecond)
	}

	return nil
}

func readPartNumber() int {
	data, err := os.ReadFile(config.PartNumberFile)
	if err != nil {
		return 0
	}
	num, err := strconv.Atoi(string(data))
	if err != nil {
		return 0
	}
	return num
}

func writePartNumber(num int) {
	_ = os.WriteFile(config.PartNumberFile, []byte(strconv.Itoa(num)), 0644)
}

// IsPaused checks if recording is currently paused
func (r *Recorder) IsPaused() bool {
	_, err := os.Stat(config.PausedFile)
	return err == nil
}

// Pause pauses the current recording
func (r *Recorder) Pause() error {
	if !r.IsRecording() {
		return fmt.Errorf("no recording in progress")
	}

	if r.IsPaused() {
		return fmt.Errorf("recording is already paused")
	}

	// Signal recorder goroutines to stop (audio/webcam only wait on stopSignal)
	if r.stopSignal != nil {
		close(r.stopSignal)
		r.stopSignal = nil
	}

	// Stop all recording processes
	var stopWg sync.WaitGroup

	if pid := readPID(config.VideoPIDFile); pid > 0 {
		stopWg.Add(1)
		go func(p int) {
			defer stopWg.Done()
			_ = stopProcess(p)
			_ = os.Remove(config.VideoPIDFile)
		}(pid)
	}

	if pid := readPID(config.AudioPIDFile); pid > 0 {
		stopWg.Add(1)
		go func(p int) {
			defer stopWg.Done()
			_ = stopProcess(p)
			_ = os.Remove(config.AudioPIDFile)
		}(pid)
	}

	if pid := readPID(config.WebcamPIDFile); pid > 0 {
		stopWg.Add(1)
		go func(p int) {
			defer stopWg.Done()
			_ = stopProcess(p)
			_ = os.Remove(config.WebcamPIDFile)
		}(pid)
	}

	stopWg.Wait()

	// Wait for recorder goroutines to finish
	r.wg.Wait()

	// Wait briefly for files to be written
	time.Sleep(300 * time.Millisecond)

	// Mark as paused
	_ = os.WriteFile(config.PausedFile, []byte("paused"), 0644)

	// Increment part number for next resume
	currentPart := readPartNumber()
	writePartNumber(currentPart + 1)

	// Update recording info status
	outputDir := readPath(config.OutputDirFile)
	if outputDir != "" {
		if info, err := models.LoadRecordingInfo(outputDir); err == nil {
			info.SetStatus(models.StatusPaused)
			_ = info.Save()
		}
	}

	// Note: Don't send desktop notifications during recording as they appear in the recording
	return nil
}

// Resume resumes a paused recording
func (r *Recorder) Resume() error {
	if !r.IsPaused() {
		return fmt.Errorf("recording is not paused")
	}

	// Load recording info to get settings
	outputDir := readPath(config.OutputDirFile)
	if outputDir == "" {
		return fmt.Errorf("no recording session found")
	}

	info, err := models.LoadRecordingInfo(outputDir)
	if err != nil {
		return fmt.Errorf("failed to load recording info: %w", err)
	}

	// Remove paused marker
	_ = os.Remove(config.PausedFile)

	// Build options from recording info
	opts := Options{
		OutputDir:     outputDir,
		NoAudio:       !info.Settings.AudioEnabled,
		NoWebcam:      !info.Settings.WebcamEnabled,
		NoScreen:      !info.Settings.ScreenEnabled,
		HWAccel:       info.Settings.HardwareAccel,
		AudioDevice:   info.Settings.AudioDevice,
		WebcamDevice:  info.Settings.WebcamDevice,
		WebcamFPS:     info.Settings.WebcamFPS,
		RecordingInfo: info,
	}

	// Update status to recording
	info.SetStatus(models.StatusRecording)
	_ = info.Save()

	// Start recording with the new part number
	return r.StartWithOptions(opts)
}

// buildErrorDetail creates a user-friendly error description with context
func buildErrorDetail(err error, opts merger.MergeOptions) string {
	var sb strings.Builder

	sb.WriteString("Video post-processing failed.\n\n")
	sb.WriteString("Error: ")
	sb.WriteString(err.Error())
	sb.WriteString("\n\n")

	sb.WriteString("Processing Context:\n")
	if opts.VideoFile != "" {
		sb.WriteString("  - Video file: ")
		sb.WriteString(opts.VideoFile)
		if _, statErr := os.Stat(opts.VideoFile); statErr != nil {
			sb.WriteString(" (NOT FOUND)")
		}
		sb.WriteString("\n")
	}
	if opts.AudioFile != "" {
		sb.WriteString("  - Audio file: ")
		sb.WriteString(opts.AudioFile)
		if _, statErr := os.Stat(opts.AudioFile); statErr != nil {
			sb.WriteString(" (NOT FOUND)")
		}
		sb.WriteString("\n")
	}
	if opts.WebcamFile != "" {
		sb.WriteString("  - Webcam file: ")
		sb.WriteString(opts.WebcamFile)
		if _, statErr := os.Stat(opts.WebcamFile); statErr != nil {
			sb.WriteString(" (NOT FOUND)")
		}
		sb.WriteString("\n")
	}
	if opts.OutputDir != "" {
		sb.WriteString("  - Output directory: ")
		sb.WriteString(opts.OutputDir)
		sb.WriteString("\n")
	}
	sb.WriteString("  - Create vertical: ")
	if opts.CreateVertical {
		sb.WriteString("yes\n")
	} else {
		sb.WriteString("no\n")
	}
	sb.WriteString("  - Add logos: ")
	if opts.AddLogos {
		sb.WriteString("yes\n")
	} else {
		sb.WriteString("no\n")
	}

	sb.WriteString("\nPossible causes:\n")
	errStr := err.Error()
	if strings.Contains(errStr, "ffmpeg") || strings.Contains(errStr, "FFmpeg") {
		sb.WriteString("  - FFmpeg encountered an error during encoding\n")
		sb.WriteString("  - Input files may be corrupted or incompatible\n")
		sb.WriteString("  - Insufficient disk space or memory\n")
	}
	if strings.Contains(errStr, "no such file") || strings.Contains(errStr, "not found") {
		sb.WriteString("  - Required input files were not created during recording\n")
		sb.WriteString("  - Recording may have been interrupted\n")
	}
	if strings.Contains(errStr, "permission denied") {
		sb.WriteString("  - Insufficient permissions to read/write files\n")
	}

	sb.WriteString("\nSuggested actions:\n")
	sb.WriteString("  1. Check that all input files exist and are valid\n")
	sb.WriteString("  2. Ensure sufficient disk space is available\n")
	sb.WriteString("  3. Try reprocessing the recording from History\n")
	sb.WriteString("  4. If the issue persists, file a bug report with this error detail\n")

	return sb.String()
}

// captureTraceback captures the current stack trace for debugging
func captureTraceback() string {
	// Capture stack trace
	buf := make([]byte, 4096)
	n := runtime.Stack(buf, false)
	return string(buf[:n])
}

// buildErrorDetailFromMessage creates a user-friendly error description from a simple error message
func buildErrorDetailFromMessage(errMsg string, folderPath string) string {
	var sb strings.Builder

	sb.WriteString("Video post-processing failed.\n\n")
	sb.WriteString("Error: ")
	sb.WriteString(errMsg)
	sb.WriteString("\n\n")

	sb.WriteString("Processing Context:\n")
	if folderPath != "" {
		sb.WriteString("  - Output directory: ")
		sb.WriteString(folderPath)
		sb.WriteString("\n")
	}

	sb.WriteString("\nPossible causes:\n")
	if strings.Contains(errMsg, "No video or audio") {
		sb.WriteString("  - Recording files were not created properly\n")
		sb.WriteString("  - Recording was stopped before any data was captured\n")
		sb.WriteString("  - Files may have been moved or deleted\n")
	}

	sb.WriteString("\nSuggested actions:\n")
	sb.WriteString("  1. Check that the recording folder contains the expected files\n")
	sb.WriteString("  2. Try creating a new recording\n")
	sb.WriteString("  3. If the issue persists, file a bug report with this error detail\n")

	return sb.String()
}
