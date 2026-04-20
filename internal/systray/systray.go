//go:build cgo

package systray

import (
	"bytes"
	"image/color"
	_ "embed"
	"fmt"
	"image"
	"image/draw"
	"image/png"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"time"

	"fyne.io/systray"
	"github.com/kartoza/kartoza-screencaster/internal/audio"
	"github.com/kartoza/kartoza-screencaster/internal/beep"
	"github.com/kartoza/kartoza-screencaster/internal/config"
	"github.com/kartoza/kartoza-screencaster/internal/models"
	"github.com/kartoza/kartoza-screencaster/internal/monitor"
	"github.com/kartoza/kartoza-screencaster/internal/recorder"
)

// Embed the three state icons
//
//go:embed icon_ready.png
var iconReadyData []byte

//go:embed icon_recording.png
var iconRecordingData []byte

//go:embed icon_paused.png
var iconPausedData []byte

// TrayState represents the current state of the system tray
type TrayState int

const (
	StateIdle TrayState = iota
	StateRecording
	StatePaused
	StateProcessing
	StateCountdown
	StateRoomNoise // Recording room noise for audio calibration
)

// RecordingInfo contains details about the current recording for tooltip display
type RecordingInfo struct {
	Monitor   string
	StartTime time.Time
	IsPaused  bool
}

// Manager handles the system tray icon and menu
type Manager struct {
	recorder *recorder.Recorder

	// Menu items
	mStartStop *systray.MenuItem
	mPause     *systray.MenuItem
	mStatus    *systray.MenuItem
	mOpenTUI   *systray.MenuItem
	mQuit      *systray.MenuItem

	// Channels for communication
	startChan chan struct{}
	stopChan  chan struct{}
	pauseChan chan struct{}
	tuiChan   chan struct{}
	quitChan  chan struct{}

	// Current state
	currentState TrayState

	// Icons (resized for tray)
	iconReady     []byte
	iconRecording []byte
	iconPaused    []byte

	// Pre-rendered rotated versions of ready icon (for processing animation)
	rotatedReadyIcons [][]byte

	// Icon rotation for processing state
	rotationTicker  *time.Ticker
	stopRotation    chan struct{}
	isRotating      bool
	reverseRotation bool // Rotate backwards (for room noise state)

	// Status polling
	statusTicker *time.Ticker
	stopStatus   chan struct{}
	lastStatus   models.RecordingStatus

	// Recording info for tooltip
	recordingInfo *RecordingInfo

	// Countdown icons (digits 1-5 overlaid on ready icon)
	countdownIcons [6][]byte // index 1-5 = digit icons, 0 unused

	// Countdown cancellation
	cancelCountdown chan struct{}
	isCountingDown  bool

	// Room noise capture
	roomNoiseDoneChan chan struct{}
	pendingOutputDir  string // Output dir to open TUI after room noise capture
}

// New creates a new systray manager
func New() *Manager {
	m := &Manager{
		recorder:          recorder.New(),
		startChan:         make(chan struct{}, 1),
		stopChan:          make(chan struct{}, 1),
		pauseChan:         make(chan struct{}, 1),
		tuiChan:           make(chan struct{}, 1),
		quitChan:          make(chan struct{}, 1),
		stopRotation:      make(chan struct{}),
		stopStatus:        make(chan struct{}),
		roomNoiseDoneChan: make(chan struct{}, 1),
		currentState:      StateIdle,
	}
	m.loadAndPrepareIcons()
	return m
}

// loadAndPrepareIcons loads icons without resizing (let the system tray handle scaling)
func (m *Manager) loadAndPrepareIcons() {
	// Load the ready icon - use original size for better color fidelity
	if img, err := png.Decode(bytes.NewReader(iconReadyData)); err == nil {
		var buf bytes.Buffer
		if err := png.Encode(&buf, img); err == nil {
			m.iconReady = buf.Bytes()
		}

		// Pre-render 12 rotated versions of ready icon for processing animation
		m.rotatedReadyIcons = make([][]byte, 12)
		for i := 0; i < 12; i++ {
			angle := float64(i) * 30.0 * math.Pi / 180.0
			rotated := rotateImage(img, angle)
			var rotBuf bytes.Buffer
			if err := png.Encode(&rotBuf, rotated); err == nil {
				m.rotatedReadyIcons[i] = rotBuf.Bytes()
			}
		}
	}

	// Load the recording icon
	if img, err := png.Decode(bytes.NewReader(iconRecordingData)); err == nil {
		var buf bytes.Buffer
		if err := png.Encode(&buf, img); err == nil {
			m.iconRecording = buf.Bytes()
		}
	}

	// Load the paused icon
	if img, err := png.Decode(bytes.NewReader(iconPausedData)); err == nil {
		var buf bytes.Buffer
		if err := png.Encode(&buf, img); err == nil {
			m.iconPaused = buf.Bytes()
		}
	}

	// Generate countdown digit icons (1-5) by overlaying digits on the ready icon
	if readyImg, err := png.Decode(bytes.NewReader(iconReadyData)); err == nil {
		for digit := 1; digit <= 5; digit++ {
			digitIcon := renderDigitOverlay(readyImg, digit)
			var buf bytes.Buffer
			if err := png.Encode(&buf, digitIcon); err == nil {
				m.countdownIcons[digit] = buf.Bytes()
			}
		}
	}
}

// rotateImage rotates an image by the given angle (in radians)
func rotateImage(src image.Image, angle float64) image.Image {
	bounds := src.Bounds()
	w, h := bounds.Dx(), bounds.Dy()

	// Create a new RGBA image
	dst := image.NewRGBA(image.Rect(0, 0, w, h))

	// Center of rotation
	cx, cy := float64(w)/2, float64(h)/2

	// Precompute sin and cos
	sin, cos := math.Sin(-angle), math.Cos(-angle)

	// For each pixel in the destination
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			// Translate to center
			dx := float64(x) - cx
			dy := float64(y) - cy

			// Rotate (inverse rotation to find source pixel)
			srcX := dx*cos - dy*sin + cx
			srcY := dx*sin + dy*cos + cy

			// Check bounds and copy pixel
			sx, sy := int(srcX), int(srcY)
			if sx >= 0 && sx < w && sy >= 0 && sy < h {
				dst.Set(x, y, src.At(sx, sy))
			}
		}
	}

	return dst
}

// setIcon sets the appropriate icon based on current state
func (m *Manager) setIcon(state TrayState) {
	m.currentState = state

	// Stop any existing rotation if changing to a non-rotating state
	if m.isRotating && state != StateProcessing && state != StateRoomNoise {
		m.stopIconRotation()
	}

	switch state {
	case StateIdle:
		if m.iconReady != nil {
			systray.SetIcon(m.iconReady)
		}
	case StateRecording:
		if m.iconRecording != nil {
			systray.SetIcon(m.iconRecording)
		}
	case StatePaused:
		if m.iconPaused != nil {
			systray.SetIcon(m.iconPaused)
		}
	case StateProcessing:
		// Start spinning the ready icon forward
		m.reverseRotation = false
		m.startIconRotation()
	case StateRoomNoise:
		// Start spinning the ready icon in reverse at 2x speed
		m.reverseRotation = true
		m.startIconRotationWithSpeed(50 * time.Millisecond) // 2x speed (50ms vs 100ms)
	}
}

// StartChan returns the channel that signals when to start recording
func (m *Manager) StartChan() <-chan struct{} {
	return m.startChan
}

// StopChan returns the channel that signals when to stop recording
func (m *Manager) StopChan() <-chan struct{} {
	return m.stopChan
}

// PauseChan returns the channel that signals when to pause/resume recording
func (m *Manager) PauseChan() <-chan struct{} {
	return m.pauseChan
}

// TUIChan returns the channel that signals when to open TUI
func (m *Manager) TUIChan() <-chan struct{} {
	return m.tuiChan
}

// QuitChan returns the channel that signals when to quit
func (m *Manager) QuitChan() <-chan struct{} {
	return m.quitChan
}

// RoomNoiseDoneChan returns the channel that signals when room noise capture is complete
func (m *Manager) RoomNoiseDoneChan() <-chan struct{} {
	return m.roomNoiseDoneChan
}

// OnReady is called when the systray is ready
func (m *Manager) OnReady() {
	// Set initial icon (ready/idle state)
	m.setIcon(StateIdle)
	systray.SetTitle("Kartoza Video")
	systray.SetTooltip("Kartoza Video Processor - Click to start recording")

	// Set up left-click handler
	// Click while idle: start recording with countdown
	// Click while recording: pause
	// Click while paused: resume
	// Stop is only available via right-click menu
	systray.SetOnTapped(func() {
		// If counting down, cancel on any click
		if m.isCountingDown {
			m.CancelCountdown()
			return
		}

		// If recording room noise, reject all clicks
		if m.currentState == StateRoomNoise {
			return
		}

		status := m.recorder.GetStatus()

		if status.IsRecording {
			// Click while recording: pause
			select {
			case m.pauseChan <- struct{}{}:
			default:
			}
		} else if status.IsPaused {
			// Click while paused: resume
			select {
			case m.pauseChan <- struct{}{}:
			default:
			}
		} else {
			// Click while idle: start recording with countdown
			select {
			case m.startChan <- struct{}{}:
			default:
			}
		}
	})

	// Add menu items (shown on right-click)
	m.mStartStop = systray.AddMenuItem("Start Recording", "Start a new recording")
	m.mPause = systray.AddMenuItem("Pause", "Pause the recording")
	m.mPause.Hide()
	systray.AddSeparator()
	m.mStatus = systray.AddMenuItem("Idle", "Current status")
	m.mStatus.Disable()
	systray.AddSeparator()
	m.mOpenTUI = systray.AddMenuItem("Open TUI", "Open the full interface")
	systray.AddSeparator()
	m.mQuit = systray.AddMenuItem("Quit", "Quit the application")

	// Handle menu clicks in a goroutine
	go m.handleClicks()

	// Start status polling
	m.startStatusPolling()
}

// OnExit is called when the systray is exiting
func (m *Manager) OnExit() {
	m.stopIconRotation()
	m.stopStatusPolling()
}

// handleClicks handles menu item clicks
func (m *Manager) handleClicks() {
	for {
		select {
		case <-m.mStartStop.ClickedCh:
			// If counting down, cancel
			if m.isCountingDown {
				m.CancelCountdown()
				continue
			}
			status := m.recorder.GetStatus()
			if status.IsRecording || status.IsPaused {
				// Stop recording
				select {
				case m.stopChan <- struct{}{}:
				default:
				}
			} else {
				// Start recording with countdown
				select {
				case m.startChan <- struct{}{}:
				default:
				}
			}
		case <-m.mPause.ClickedCh:
			select {
			case m.pauseChan <- struct{}{}:
			default:
			}
		case <-m.mOpenTUI.ClickedCh:
			select {
			case m.tuiChan <- struct{}{}:
			default:
			}
		case <-m.mQuit.ClickedCh:
			select {
			case m.quitChan <- struct{}{}:
			default:
			}
			return
		}
	}
}

// startStatusPolling starts polling for recording status changes
func (m *Manager) startStatusPolling() {
	m.statusTicker = time.NewTicker(1 * time.Second)
	go func() {
		// Initial update
		m.updateStatus()

		for {
			select {
			case <-m.statusTicker.C:
				m.updateStatus()
			case <-m.stopStatus:
				return
			}
		}
	}()
}

// stopStatusPolling stops the status polling
func (m *Manager) stopStatusPolling() {
	if m.statusTicker != nil {
		m.statusTicker.Stop()
		m.statusTicker = nil
	}
	select {
	case m.stopStatus <- struct{}{}:
	default:
	}
}

// updateStatus updates the tray based on current recording status
func (m *Manager) updateStatus() {
	// Don't update status during countdown - the countdown goroutine manages state
	if m.isCountingDown {
		return
	}

	status := m.recorder.GetStatus()

	// Check if status changed
	statusChanged := status.IsRecording != m.lastStatus.IsRecording ||
		status.IsPaused != m.lastStatus.IsPaused
	m.lastStatus = status

	if status.IsRecording {
		// Recording active
		if statusChanged {
			m.SetRecordingActive(status.Monitor, status.StartTime)
		} else {
			// Just update tooltip with elapsed time
			m.updateTooltip()
		}
	} else if status.IsPaused {
		// Recording paused
		if statusChanged {
			m.SetRecordingPaused()
		}
	} else {
		// Idle (but check if we're processing or recording room noise - don't change state)
		if statusChanged && m.currentState != StateProcessing && m.currentState != StateRoomNoise {
			m.SetIdle()
		}
	}
}

// SetRecordingActive updates the tray to show recording is active
func (m *Manager) SetRecordingActive(monitor string, startTime time.Time) {
	m.recordingInfo = &RecordingInfo{
		Monitor:   monitor,
		StartTime: startTime,
		IsPaused:  false,
	}

	// Update menu
	m.mStartStop.SetTitle("Stop Recording")
	m.mStartStop.SetTooltip("Stop the current recording")
	m.mPause.SetTitle("Pause")
	m.mPause.SetTooltip("Pause the recording")
	m.mPause.Show()

	// Update status
	elapsed := formatDuration(time.Since(startTime))
	m.mStatus.SetTitle(fmt.Sprintf("Recording: %s", elapsed))

	// Update tooltip
	m.updateTooltip()

	// Set recording icon (static, no rotation)
	m.setIcon(StateRecording)
}

// SetRecordingPaused updates the tray to show recording is paused
func (m *Manager) SetRecordingPaused() {
	if m.recordingInfo != nil {
		m.recordingInfo.IsPaused = true
	}

	// Update menu
	m.mStartStop.SetTitle("Stop Recording")
	m.mStartStop.SetTooltip("Stop and process the recording")
	m.mPause.SetTitle("Resume")
	m.mPause.SetTooltip("Resume the recording")
	m.mPause.Show()

	// Update status
	m.mStatus.SetTitle("Paused")

	// Update tooltip
	systray.SetTooltip("Recording Paused - Click to resume")

	// Set paused icon
	m.setIcon(StatePaused)
}

// SetIdle updates the tray to show no recording is active
func (m *Manager) SetIdle() {
	m.recordingInfo = nil

	// Update menu
	m.mStartStop.SetTitle("Start Recording")
	m.mStartStop.SetTooltip("Start a new recording")
	m.mPause.Hide()

	// Update status
	m.mStatus.SetTitle("Idle")

	// Update tooltip
	systray.SetTooltip("Kartoza Video Processor - Click to start recording")

	// Set ready/idle icon
	m.setIcon(StateIdle)
}

// SetProcessing updates the tray to show processing is in progress
func (m *Manager) SetProcessing() {
	// Update menu
	m.mStartStop.SetTitle("Start Recording")
	m.mStartStop.SetTooltip("Start a new recording")
	m.mPause.Hide()

	// Update status
	m.mStatus.SetTitle("Processing...")

	// Update tooltip
	systray.SetTooltip("Processing video - Please wait...")

	// Set processing state (spinning ready icon)
	m.setIcon(StateProcessing)
}

// SetRoomNoise updates the tray to show room noise recording is in progress
func (m *Manager) SetRoomNoise() {
	// Update menu
	m.mStartStop.SetTitle("Recording Room Noise...")
	m.mStartStop.SetTooltip("Please keep quiet - recording room noise for audio calibration")
	m.mStartStop.Disable()
	m.mPause.Hide()

	// Update status
	m.mStatus.SetTitle("Recording Room Noise")

	// Update tooltip with user instruction
	systray.SetTooltip("🎤 Recording room noise - Please keep quiet!\nThis helps calibrate audio quality.")

	// Set room noise state (reverse spinning icon at 2x speed)
	m.setIcon(StateRoomNoise)
}

// ClearRoomNoise transitions from room noise state back to idle
func (m *Manager) ClearRoomNoise() {
	m.mStartStop.Enable()
	m.SetIdle()
}

// updateTooltip updates the systray tooltip with current recording info
func (m *Manager) updateTooltip() {
	if m.recordingInfo == nil {
		systray.SetTooltip("Kartoza Video Processor - Click to start recording")
		return
	}

	elapsed := time.Since(m.recordingInfo.StartTime)
	hours := int(elapsed.Hours())
	minutes := int(elapsed.Minutes()) % 60
	seconds := int(elapsed.Seconds()) % 60

	tooltip := fmt.Sprintf("Recording on %s\nElapsed: %02d:%02d:%02d\nClick to stop",
		m.recordingInfo.Monitor, hours, minutes, seconds)

	systray.SetTooltip(tooltip)

	// Also update the menu status item
	m.mStatus.SetTitle(fmt.Sprintf("Recording: %02d:%02d:%02d", hours, minutes, seconds))
}

// startIconRotation starts the icon rotation animation (for processing state)
func (m *Manager) startIconRotation() {
	m.startIconRotationWithSpeed(100 * time.Millisecond)
}

// startIconRotationWithSpeed starts the icon rotation animation with a custom speed
func (m *Manager) startIconRotationWithSpeed(interval time.Duration) {
	if m.isRotating || len(m.rotatedReadyIcons) == 0 {
		return
	}

	m.isRotating = true
	m.rotationTicker = time.NewTicker(interval)
	// Create a fresh stop channel for this rotation session
	m.stopRotation = make(chan struct{})

	go func() {
		iconIndex := 0
		numIcons := len(m.rotatedReadyIcons)
		stopCh := m.stopRotation // Capture the channel for this goroutine
		for {
			select {
			case <-m.rotationTicker.C:
				if iconIndex < numIcons && m.rotatedReadyIcons[iconIndex] != nil {
					systray.SetIcon(m.rotatedReadyIcons[iconIndex])
				}
				// Rotate forward or backward based on reverseRotation flag
				if m.reverseRotation {
					iconIndex = (iconIndex - 1 + numIcons) % numIcons
				} else {
					iconIndex = (iconIndex + 1) % numIcons
				}
			case <-stopCh:
				return
			}
		}
	}()
}

// stopIconRotation stops the icon rotation
func (m *Manager) stopIconRotation() {
	if !m.isRotating {
		return
	}

	m.isRotating = false
	if m.rotationTicker != nil {
		m.rotationTicker.Stop()
		m.rotationTicker = nil
	}

	// Close the channel to signal all goroutines to stop
	if m.stopRotation != nil {
		close(m.stopRotation)
		m.stopRotation = nil
	}
}

// renderDigitOverlay creates an icon with a large digit rendered over a dimmed version of the base icon
func renderDigitOverlay(base image.Image, digit int) image.Image {
	bounds := base.Bounds()
	w, h := bounds.Dx(), bounds.Dy()

	dst := image.NewRGBA(image.Rect(0, 0, w, h))

	// Draw dimmed base icon
	draw.Draw(dst, dst.Bounds(), base, bounds.Min, draw.Src)

	// Dim the base icon to ~40% opacity by blending with dark background
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			r, g, b, a := dst.At(x, y).RGBA()
			// Dim to 40%
			dst.Set(x, y, color.RGBA{
				R: uint8((r >> 8) * 40 / 100),
				G: uint8((g >> 8) * 40 / 100),
				B: uint8((b >> 8) * 40 / 100),
				A: uint8(a >> 8),
			})
		}
	}

	// Render digit using a simple bitmap font
	// For a ~57x60 icon, we want digits that fill most of the space
	digitBitmap := getDigitBitmap(digit)
	if digitBitmap == nil {
		return dst
	}

	// Calculate the size of the digit bitmap
	bitmapH := len(digitBitmap)
	bitmapW := 0
	if bitmapH > 0 {
		bitmapW = len(digitBitmap[0])
	}

	// Center the digit on the icon
	offsetX := (w - bitmapW) / 2
	offsetY := (h - bitmapH) / 2

	// Choose color based on digit (orange for 5,4; dark orange for 3,2; red for 1)
	var digitColor color.RGBA
	switch digit {
	case 5, 4:
		digitColor = color.RGBA{R: 255, G: 165, B: 0, A: 255} // Orange
	case 3, 2:
		digitColor = color.RGBA{R: 255, G: 140, B: 0, A: 255} // Dark orange
	case 1:
		digitColor = color.RGBA{R: 255, G: 50, B: 50, A: 255} // Red
	default:
		digitColor = color.RGBA{R: 255, G: 255, B: 255, A: 255}
	}

	// Draw the digit
	for row := 0; row < bitmapH; row++ {
		for col := 0; col < bitmapW; col++ {
			if digitBitmap[row][col] {
				px := offsetX + col
				py := offsetY + row
				if px >= 0 && px < w && py >= 0 && py < h {
					dst.Set(px, py, digitColor)
				}
			}
		}
	}

	return dst
}

// getDigitBitmap returns a boolean bitmap for a digit (1-5)
// Each bitmap is designed for ~57x60 pixel icons
// Uses ASCII '#' for filled pixels, ' ' for empty - avoids multi-byte rune issues
func getDigitBitmap(digit int) [][]bool {
	// Block-style digits, 24 wide x 19 tall
	patterns := map[int][]string{
		5: {
			"########################",
			"########################",
			"########################",
			"####                    ",
			"####                    ",
			"####                    ",
			"####                    ",
			"####                    ",
			"########################",
			"########################",
			"########################",
			"                    ####",
			"                    ####",
			"                    ####",
			"                    ####",
			"                    ####",
			"########################",
			"########################",
			"########################",
		},
		4: {
			"####                ####",
			"####                ####",
			"####                ####",
			"####                ####",
			"####                ####",
			"####                ####",
			"####                ####",
			"####                ####",
			"########################",
			"########################",
			"########################",
			"                    ####",
			"                    ####",
			"                    ####",
			"                    ####",
			"                    ####",
			"                    ####",
			"                    ####",
			"                    ####",
		},
		3: {
			"########################",
			"########################",
			"########################",
			"                    ####",
			"                    ####",
			"                    ####",
			"                    ####",
			"                    ####",
			"########################",
			"########################",
			"########################",
			"                    ####",
			"                    ####",
			"                    ####",
			"                    ####",
			"                    ####",
			"########################",
			"########################",
			"########################",
		},
		2: {
			"########################",
			"########################",
			"########################",
			"                    ####",
			"                    ####",
			"                    ####",
			"                    ####",
			"                    ####",
			"########################",
			"########################",
			"########################",
			"####                    ",
			"####                    ",
			"####                    ",
			"####                    ",
			"####                    ",
			"########################",
			"########################",
			"########################",
		},
		1: {
			"        ########        ",
			"        ########        ",
			"    ############        ",
			"    ############        ",
			"        ########        ",
			"        ########        ",
			"        ########        ",
			"        ########        ",
			"        ########        ",
			"        ########        ",
			"        ########        ",
			"        ########        ",
			"        ########        ",
			"        ########        ",
			"        ########        ",
			"        ########        ",
			"    ################    ",
			"    ################    ",
			"    ################    ",
		},
	}

	pattern, ok := patterns[digit]
	if !ok {
		return nil
	}

	bitmap := make([][]bool, len(pattern))
	for i, row := range pattern {
		bitmap[i] = make([]bool, len(row))
		for j, ch := range row {
			bitmap[i][j] = ch != ' '
		}
	}

	return bitmap
}

// StartRecordingWithCountdown starts recording after a 5-second countdown with beeps and icon updates
func (m *Manager) StartRecordingWithCountdown() error {
	if m.recorder.IsRecording() {
		return fmt.Errorf("recording already in progress")
	}

	if m.isCountingDown {
		return fmt.Errorf("countdown already in progress")
	}

	m.isCountingDown = true
	m.cancelCountdown = make(chan struct{})
	m.currentState = StateCountdown

	go func() {
		defer func() {
			m.isCountingDown = false
		}()

		// Countdown from 5 to 1
		for count := 5; count >= 1; count-- {
			// Set countdown icon
			if count >= 1 && count <= 5 && m.countdownIcons[count] != nil {
				systray.SetIcon(m.countdownIcons[count])
			}
			systray.SetTooltip(fmt.Sprintf("Recording starts in %d...", count))
			m.mStatus.SetTitle(fmt.Sprintf("Starting in %d...", count))

			// Play beep
			go beep.Play(count)

			// Wait 1 second or cancel
			select {
			case <-m.cancelCountdown:
				// Cancelled - restore idle state
				m.SetIdle()
				return
			case <-time.After(1 * time.Second):
				// Continue countdown
			}
		}

		// Countdown finished - start recording
		if err := m.StartRecording(); err != nil {
			fmt.Fprintf(os.Stderr, "Failed to start recording: %v\n", err)
			m.SetIdle()
		}
	}()

	return nil
}

// CancelCountdown cancels an in-progress countdown
func (m *Manager) CancelCountdown() {
	if m.isCountingDown && m.cancelCountdown != nil {
		close(m.cancelCountdown)
	}
}

// StartRecording starts a quick recording without metadata
func (m *Manager) StartRecording() error {
	if m.recorder.IsRecording() {
		return fmt.Errorf("recording already in progress")
	}

	// Create output directory
	cfg, err := config.Load()
	if err != nil {
		return fmt.Errorf("failed to load config: %w", err)
	}

	// First-run check: if presets haven't been configured, open TUI to presets
	if !cfg.PresetsConfigured {
		return m.OpenTUIToPresets()
	}

	baseDir := cfg.OutputDir
	if baseDir == "" {
		baseDir = config.GetDefaultVideosDir()
	}

	// Create a temporary folder name - will be renamed when user provides metadata
	timestamp := time.Now().Format("20060102-150405")
	tempFolderName := fmt.Sprintf("recording-%s", timestamp)
	outputDir := fmt.Sprintf("%s/%s", baseDir, tempFolderName)

	if err := os.MkdirAll(outputDir, 0755); err != nil {
		return fmt.Errorf("failed to create output directory: %w", err)
	}

	// Create minimal recording info with the next available number
	nextNumber := config.GetCurrentRecordingNumber()
	metadata := models.RecordingMetadata{
		Number:      nextNumber,
		Title:       "Untitled Recording",
		Description: "",
		FolderName:  tempFolderName,
	}

	// Get recording presets
	presets := cfg.RecordingPresets

	// Get the monitor where the mouse is currently located
	monitorName, err := monitor.GetMouseMonitor()
	if err != nil {
		// Fallback to primary monitor
		if monitors, mErr := monitor.ListMonitors(); mErr == nil && len(monitors) > 0 {
			monitorName = monitors[0].Name
		}
	}

	// Get monitor resolution
	monitorResolution := ""
	if monitors, mErr := monitor.ListMonitors(); mErr == nil {
		for _, mon := range monitors {
			if mon.Name == monitorName {
				monitorResolution = fmt.Sprintf("%dx%d", mon.Width, mon.Height)
				break
			}
		}
	}

	recordingInfo := models.NewRecordingInfo(metadata, monitorName, monitorResolution)
	recordingInfo.Files.FolderPath = outputDir
	recordingInfo.Settings.ScreenEnabled = presets.RecordScreen
	recordingInfo.Settings.AudioEnabled = presets.RecordAudio
	recordingInfo.Settings.WebcamEnabled = presets.RecordWebcam
	recordingInfo.Settings.VerticalEnabled = presets.VerticalVideo
	recordingInfo.Settings.LogosEnabled = presets.AddLogos

	// Save initial recording.json
	if err := recordingInfo.Save(); err != nil {
		return fmt.Errorf("failed to save recording info: %w", err)
	}

	// Start recording
	opts := recorder.Options{
		OutputDir:      outputDir,
		Monitor:        monitorName,
		NoAudio:        !presets.RecordAudio,
		NoWebcam:       !presets.RecordWebcam,
		NoScreen:       !presets.RecordScreen,
		CreateVertical: presets.VerticalVideo,
		RecordingInfo:  recordingInfo,
	}

	return m.recorder.StartWithOptions(opts)
}

// StopRecording stops the current recording and starts room noise capture if audio was enabled.
// Returns (capturingRoomNoise, error) - if capturingRoomNoise is true, caller should wait for
// RoomNoiseDoneChan before opening TUI.
func (m *Manager) StopRecording() (bool, error) {
	if !m.recorder.IsRecording() && !m.recorder.IsPaused() {
		return false, fmt.Errorf("no recording in progress")
	}

	// Get output directory before stopping
	outputDir := config.ReadPath(config.OutputDirFile)

	// Check if audio was enabled (we need to know before stopping)
	audioEnabled := false
	if outputDir != "" {
		if info, err := models.LoadRecordingInfo(outputDir); err == nil {
			audioEnabled = info.Settings.AudioEnabled
		}
	}

	// If we'll capture room noise, set state BEFORE stopping recorder
	// This prevents status polling from calling SetIdle() during the transition
	willCaptureRoomNoise := audioEnabled && outputDir != ""
	if willCaptureRoomNoise {
		m.SetRoomNoise()
	}

	// Stop recording without processing - we'll process after user provides metadata
	if err := m.recorder.Stop(); err != nil {
		if willCaptureRoomNoise {
			m.ClearRoomNoise() // Reset state if stop failed
		}
		return false, err
	}

	// Mark the recording as needing metadata
	if outputDir != "" {
		if info, err := models.LoadRecordingInfo(outputDir); err == nil {
			info.SetStatus(models.StatusNeedsMetadata)
			_ = info.Save()
		}
	}

	// If audio was enabled, start room noise capture (async)
	if willCaptureRoomNoise {
		m.pendingOutputDir = outputDir
		m.startRoomNoiseCapture(outputDir)
		return true, nil
	}

	return false, nil
}

// startRoomNoiseCapture captures 30 seconds of room noise for audio calibration
// This runs asynchronously and signals completion via roomNoiseDoneChan
// Note: SetRoomNoise() must be called before this function
func (m *Manager) startRoomNoiseCapture(outputDir string) {
	go func() {
		roomNoiseFile := filepath.Join(outputDir, "room_noise.wav")

		// Start audio recorder for room noise
		roomNoiseRecorder := audio.NewRecorder("", roomNoiseFile)
		if err := roomNoiseRecorder.Start(); err != nil {
			// If room noise recording fails, just signal done
			m.ClearRoomNoise()
			select {
			case m.roomNoiseDoneChan <- struct{}{}:
			default:
			}
			return
		}

		// Capture for 30 seconds with countdown updates
		for countdown := 30; countdown > 0; countdown-- {
			m.mStatus.SetTitle(fmt.Sprintf("Room Noise: %ds", countdown))
			systray.SetTooltip(fmt.Sprintf("🎤 Recording room noise - %d seconds remaining\nPlease keep quiet!", countdown))
			time.Sleep(1 * time.Second)
		}

		// Stop the room noise recorder
		_ = roomNoiseRecorder.Stop()

		// Return to idle state
		m.ClearRoomNoise()

		// Signal completion
		select {
		case m.roomNoiseDoneChan <- struct{}{}:
		default:
		}
	}()
}

// PauseRecording pauses or resumes the current recording
func (m *Manager) PauseRecording() error {
	status := m.recorder.GetStatus()
	if status.IsPaused {
		return m.recorder.Resume()
	}
	if status.IsRecording {
		return m.recorder.Pause()
	}
	return fmt.Errorf("no recording to pause/resume")
}

// OpenTUI opens the TUI for metadata entry, going directly to the recording edit screen
func (m *Manager) OpenTUI() error {
	return m.openTUIWithArgs("tui", "--edit-recording", "--nosplash")
}

// OpenTUIMain opens the normal TUI (main menu)
func (m *Manager) OpenTUIMain() error {
	return m.openTUIWithArgs("tui")
}

// openTUIWithArgs launches the TUI in a terminal with the given extra arguments
func (m *Manager) openTUIWithArgs(extraArgs ...string) error {
	baseArgs := []string{"kartoza-screencaster"}
	baseArgs = append(baseArgs, extraArgs...)

	terminals := []struct {
		cmd     string
		argsFmt func(args []string) []string
	}{
		{"kitty", func(args []string) []string {
			return append([]string{"--title=Kartoza Screencaster"}, args...)
		}},
		{"cosmic-term", func(args []string) []string {
			return append([]string{"-e"}, args...)
		}},
		{"foot", func(args []string) []string {
			return append([]string{"--title=Kartoza Screencaster", "-e"}, args...)
		}},
		{"alacritty", func(args []string) []string {
			return append([]string{"--title", "Kartoza Screencaster", "-e"}, args...)
		}},
		{"gnome-terminal", func(args []string) []string {
			return append([]string{"--title=Kartoza Screencaster", "--"}, args...)
		}},
		{"konsole", func(args []string) []string {
			return append([]string{"--title", "Kartoza Screencaster", "-e"}, args...)
		}},
		{"xterm", func(args []string) []string {
			return append([]string{"-T", "Kartoza Screencaster", "-e"}, args...)
		}},
	}

	for _, term := range terminals {
		if _, err := exec.LookPath(term.cmd); err == nil {
			cmd := exec.Command(term.cmd, term.argsFmt(baseArgs)...)
			cmd.Stdin = nil
			cmd.Stdout = nil
			cmd.Stderr = nil
			return cmd.Start()
		}
	}

	return fmt.Errorf("no supported terminal emulator found")
}

// OpenTUIToPresets opens the TUI directly to the recording presets configuration
func (m *Manager) OpenTUIToPresets() error {
	return m.openTUIWithArgs("tui", "--presets")
}

// formatDuration formats a duration as HH:MM:SS or MM:SS
func formatDuration(d time.Duration) string {
	hours := int(d.Hours())
	minutes := int(d.Minutes()) % 60
	seconds := int(d.Seconds()) % 60

	if hours > 0 {
		return fmt.Sprintf("%d:%02d:%02d", hours, minutes, seconds)
	}
	return fmt.Sprintf("%02d:%02d", minutes, seconds)
}

// Run starts the systray application
func Run() {
	manager := New()
	systray.Run(manager.OnReady, manager.OnExit)
}

// RunWithHandler starts the systray and handles events
func RunWithHandler() {
	manager := New()

	// Start systray in a goroutine
	go systray.Run(manager.OnReady, manager.OnExit)

	// Handle events
	for {
		select {
		case <-manager.StartChan():
			if err := manager.StartRecordingWithCountdown(); err != nil {
				fmt.Fprintf(os.Stderr, "Failed to start recording: %v\n", err)
			}
		case <-manager.StopChan():
			capturingRoomNoise, err := manager.StopRecording()
			if err != nil {
				fmt.Fprintf(os.Stderr, "Failed to stop recording: %v\n", err)
			} else if !capturingRoomNoise {
				// No room noise capture needed, open TUI immediately
				if err := manager.OpenTUI(); err != nil {
					fmt.Fprintf(os.Stderr, "Failed to open TUI: %v\n", err)
				}
			}
			// If capturingRoomNoise is true, TUI will be opened when RoomNoiseDoneChan fires
		case <-manager.RoomNoiseDoneChan():
			// Room noise capture complete, now open TUI for metadata entry
			if err := manager.OpenTUI(); err != nil {
				fmt.Fprintf(os.Stderr, "Failed to open TUI: %v\n", err)
			}
		case <-manager.PauseChan():
			if err := manager.PauseRecording(); err != nil {
				fmt.Fprintf(os.Stderr, "Failed to pause/resume: %v\n", err)
			}
		case <-manager.TUIChan():
			if err := manager.OpenTUIMain(); err != nil {
				fmt.Fprintf(os.Stderr, "Failed to open TUI: %v\n", err)
			}
		case <-manager.QuitChan():
			systray.Quit()
			return
		}
	}
}
