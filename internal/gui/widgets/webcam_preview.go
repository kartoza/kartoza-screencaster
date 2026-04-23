package widgets

import (
	"fmt"
	"io"
	"os/exec"
	"sync"

	qt "github.com/mappu/miqt/qt6"
)

const (
	previewWidth  = 320
	previewHeight = 240
	previewFPS    = 15
	frameSize     = previewWidth * previewHeight * 3 // RGB24
)

// WebcamPreview displays a live webcam feed in a QLabel
type WebcamPreview struct {
	label  *qt.QLabel
	device string
	cmd    *exec.Cmd
	mu     sync.Mutex
	active bool

	// Double buffer: reader writes to writeBuf, UI reads from readBuf, swap on new frame
	writeBuf []byte
	readBuf  []byte
	newFrame bool // flag indicating a new frame is ready

	// UI refresh timer (runs on main thread)
	refreshTimer *qt.QTimer
}

// NewWebcamPreview creates a new webcam preview widget for the given device
func NewWebcamPreview(device string) *WebcamPreview {
	wp := &WebcamPreview{
		device:   device,
		writeBuf: make([]byte, frameSize),
		readBuf:  make([]byte, frameSize),
	}

	wp.label = qt.NewQLabel2()
	wp.label.SetAlignment(qt.AlignCenter)
	wp.label.SetFixedSize2(previewWidth, previewHeight)
	wp.label.SetStyleSheet(`
		QLabel {
			background: #313244;
			border: 2px solid #45475a;
			border-radius: 8px;
		}
	`)

	// Create a refresh timer on the main thread that polls for new frames
	wp.refreshTimer = qt.NewQTimer()
	wp.refreshTimer.SetInterval(1000 / previewFPS)
	wp.refreshTimer.OnTimeout(func() {
		wp.renderFrame()
	})

	return wp
}

// Widget returns the underlying QLabel as QWidget
func (wp *WebcamPreview) Widget() *qt.QWidget {
	return wp.label.QFrame.QWidget
}

// Start begins capturing and displaying webcam frames
func (wp *WebcamPreview) Start() error {
	wp.mu.Lock()
	if wp.active {
		wp.mu.Unlock()
		return nil
	}

	args := []string{
		"-f", "v4l2",
		"-framerate", fmt.Sprintf("%d", previewFPS),
		"-i", "/dev/" + wp.device,
		"-vf", fmt.Sprintf("scale=%d:%d", previewWidth, previewHeight),
		"-f", "rawvideo",
		"-pix_fmt", "rgb24",
		"-an",
		"pipe:1",
	}

	wp.cmd = exec.Command("ffmpeg", args...)
	wp.cmd.Stderr = nil

	stdout, err := wp.cmd.StdoutPipe()
	if err != nil {
		wp.mu.Unlock()
		return fmt.Errorf("stdout pipe failed for %s: %w", wp.device, err)
	}

	if err := wp.cmd.Start(); err != nil {
		wp.mu.Unlock()
		return fmt.Errorf("ffmpeg start failed for %s: %w", wp.device, err)
	}

	wp.active = true
	wp.mu.Unlock()

	// Start reading frames in a goroutine
	go wp.readFrames(stdout)

	// Start the UI refresh timer (must be called from main thread context)
	wp.refreshTimer.Start2()

	return nil
}

// Stop stops the webcam preview capture
func (wp *WebcamPreview) Stop() {
	wp.refreshTimer.Stop()

	wp.mu.Lock()
	defer wp.mu.Unlock()

	if !wp.active {
		return
	}

	wp.active = false
	if wp.cmd != nil && wp.cmd.Process != nil {
		_ = wp.cmd.Process.Kill()
		_ = wp.cmd.Wait()
	}
	wp.cmd = nil
}

func (wp *WebcamPreview) readFrames(stdout io.ReadCloser) {
	buf := make([]byte, frameSize)

	for {
		wp.mu.Lock()
		active := wp.active
		wp.mu.Unlock()
		if !active {
			break
		}

		_, err := io.ReadFull(stdout, buf)
		if err != nil {
			break
		}

		// Swap buffer: copy new frame data to writeBuf and flag it
		wp.mu.Lock()
		copy(wp.writeBuf, buf)
		wp.newFrame = true
		wp.mu.Unlock()
	}
}

// renderFrame is called by the refresh timer on the main Qt thread
func (wp *WebcamPreview) renderFrame() {
	wp.mu.Lock()
	if !wp.newFrame || !wp.active {
		wp.mu.Unlock()
		return
	}
	// Swap read/write buffers
	wp.readBuf, wp.writeBuf = wp.writeBuf, wp.readBuf
	wp.newFrame = false
	wp.mu.Unlock()

	// Create QImage from readBuf — this runs on the main thread so it's safe
	img := qt.NewQImage6(&wp.readBuf[0], previewWidth, previewHeight,
		int64(previewWidth*3), // bytesPerLine for RGB24
		qt.QImage__Format_RGB888)
	pixmap := qt.QPixmap_FromImage(img)
	wp.label.SetPixmap(pixmap)
}

// Device returns the device path
func (wp *WebcamPreview) Device() string {
	return wp.device
}
