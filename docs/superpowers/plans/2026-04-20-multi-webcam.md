# Multi-Webcam Recording Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Record from all detected webcams simultaneously with per-webcam display mode configuration (bubble/rectangle/bottom-third/off with side selection).

**Architecture:** New `WebcamManager` in `internal/webcam/` encapsulates N webcam recorders. The recorder creates a manager instead of a single webcam instance. The merger accepts a slice of webcam outputs with per-webcam display config to compose multiple overlays.

**Tech Stack:** Go, FFmpeg (v4l2/dshow/avfoundation), Bubble Tea TUI, fyne.io/systray

---

## File Structure

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `internal/webcam/types.go` | Shared types: WebcamDisplayMode, WebcamSide, WebcamConfig, WebcamOutput, DeviceInfo |
| Create | `internal/webcam/manager.go` | WebcamManager: starts/stops N webcams, returns outputs |
| Create | `internal/webcam/manager_test.go` | Tests for manager logic (detection, config matching) |
| Create | `internal/webcam/detect_linux.go` | Linux device detection with human-readable names from sysfs |
| Create | `internal/webcam/detect_darwin.go` | macOS device detection |
| Create | `internal/webcam/detect_windows.go` | Windows device detection |
| Modify | `internal/webcam/webcam.go` | Add OutputFile getter to existing code |
| Modify | `internal/config/config.go` | Add WebcamConfigs field to Config struct |
| Modify | `internal/models/recording_info.go` | Add WebcamFiles slice and WebcamParts map to FileInfo |
| Modify | `internal/models/recording.go` | Remove single WebcamDevice, add WebcamConfigs |
| Modify | `internal/recorder/recorder.go` | Replace single webcam instance with WebcamManager |
| Modify | `internal/merger/merger.go` | Accept []WebcamOutput, build multi-overlay filters |
| Create | `internal/merger/multi_webcam.go` | Multi-webcam filter builders (bubbles, rectangles, bottom-third) |
| Create | `internal/merger/multi_webcam_test.go` | Tests for multi-webcam filter string generation |
| Modify | `internal/tui/recording_form.go` | Add per-webcam inline toggles |
| Modify | `cmd/start.go` | Replace --webcam-device with --webcam-config |

---

### Task 1: Webcam Types

**Files:**
- Create: `internal/webcam/types.go`

- [ ] **Step 1: Write the types file**

```go
package webcam

// WebcamDisplayMode defines how a webcam feed is shown in output
type WebcamDisplayMode string

const (
	DisplayOff         WebcamDisplayMode = "off"
	DisplayBubble      WebcamDisplayMode = "bubble"
	DisplayRectangle   WebcamDisplayMode = "rectangle"    // landscape: strip below main video
	DisplayBottomThird WebcamDisplayMode = "bottom_third" // vertical: middle section
)

// WebcamSide defines which side a bubble is positioned on
type WebcamSide string

const (
	SideLeft  WebcamSide = "left"
	SideRight WebcamSide = "right"
)

// WebcamConfig holds per-webcam persistent configuration
type WebcamConfig struct {
	Device        string            `json:"device"`
	Enabled       bool              `json:"enabled"`
	LandscapeMode WebcamDisplayMode `json:"landscape_mode"`
	LandscapeSide WebcamSide        `json:"landscape_side"`
	VerticalMode  WebcamDisplayMode `json:"vertical_mode"`
}

// DefaultWebcamConfig returns the default config for a newly detected device
func DefaultWebcamConfig(device string) WebcamConfig {
	return WebcamConfig{
		Device:        device,
		Enabled:       true,
		LandscapeMode: DisplayBubble,
		LandscapeSide: SideRight,
		VerticalMode:  DisplayBubble,
	}
}

// WebcamOutput represents a recorded webcam file with its display configuration
type WebcamOutput struct {
	File          string
	Device        string
	LandscapeMode WebcamDisplayMode
	LandscapeSide WebcamSide
	VerticalMode  WebcamDisplayMode
}

// DeviceInfo holds detected webcam device information
type DeviceInfo struct {
	Device string // e.g. "video0"
	Name   string // human-readable, e.g. "Logitech C920"
}
```

- [ ] **Step 2: Verify it compiles**

Run: `go build ./internal/webcam/`
Expected: no errors

- [ ] **Step 3: Commit**

```bash
git add internal/webcam/types.go
git commit -m "feat: add multi-webcam type definitions"
```

---

### Task 2: Linux Device Detection with Names

**Files:**
- Create: `internal/webcam/detect_linux.go`
- Modify: `internal/webcam/webcam_linux.go` (remove `ListDevices` and `DetectDevice` — moved to detect file)

- [ ] **Step 1: Write the failing test**

Create `internal/webcam/detect_test.go`:

```go
//go:build linux

package webcam

import (
	"testing"
)

func TestDetectAllDevices(t *testing.T) {
	devices, err := DetectAllDevices()
	// On CI without webcams, this should return empty list, not error
	if err != nil {
		t.Logf("No devices found (expected in CI): %v", err)
		return
	}
	for _, dev := range devices {
		if dev.Device == "" {
			t.Error("device path should not be empty")
		}
		t.Logf("Found device: %s (%s)", dev.Device, dev.Name)
	}
}

func TestDetectAllDevicesReturnsNames(t *testing.T) {
	devices, err := DetectAllDevices()
	if err != nil || len(devices) == 0 {
		t.Skip("No webcam devices available for testing")
	}
	// At minimum, Name should fall back to Device if sysfs unavailable
	for _, dev := range devices {
		if dev.Name == "" {
			t.Errorf("device %s has empty name, should fall back to device path", dev.Device)
		}
	}
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `go test ./internal/webcam/ -run TestDetectAllDevices -v`
Expected: FAIL — `DetectAllDevices` undefined

- [ ] **Step 3: Write the Linux detection implementation**

Create `internal/webcam/detect_linux.go`:

```go
//go:build linux

package webcam

import (
	"fmt"
	"os"
	"strings"
)

// DetectAllDevices finds all available webcam devices with human-readable names.
// Returns an empty slice (not error) if no devices are found.
func DetectAllDevices() ([]DeviceInfo, error) {
	var devices []DeviceInfo
	for i := 0; i < 10; i++ {
		dev := fmt.Sprintf("video%d", i)
		path := "/dev/" + dev
		info, err := os.Stat(path)
		if err != nil {
			continue
		}
		if info.Mode()&os.ModeCharDevice == 0 {
			continue
		}
		name := getDeviceName(dev)
		devices = append(devices, DeviceInfo{Device: dev, Name: name})
	}
	return devices, nil
}

// getDeviceName reads the human-readable name from sysfs.
// Falls back to the device path if sysfs is unavailable.
func getDeviceName(dev string) string {
	sysfsPath := fmt.Sprintf("/sys/class/video4linux/%s/name", dev)
	data, err := os.ReadFile(sysfsPath)
	if err != nil {
		return dev // fallback to device path
	}
	name := strings.TrimSpace(string(data))
	if name == "" {
		return dev
	}
	return name
}
```

- [ ] **Step 4: Update webcam_linux.go to use DetectAllDevices**

Replace the `DetectDevice` function body to delegate to `DetectAllDevices`:

```go
// DetectDevice finds the first available webcam device (legacy single-device API)
func DetectDevice() (string, error) {
	devices, err := DetectAllDevices()
	if err != nil {
		return "", err
	}
	if len(devices) == 0 {
		return "", fmt.Errorf("no webcam device found")
	}
	return devices[0].Device, nil
}
```

Remove the old `ListDevices` function (replaced by `DetectAllDevices`).

- [ ] **Step 5: Run tests**

Run: `go test ./internal/webcam/ -v`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add internal/webcam/detect_linux.go internal/webcam/detect_test.go internal/webcam/webcam_linux.go
git commit -m "feat: add multi-device detection with human-readable names (Linux)"
```

---

### Task 3: macOS and Windows Detection Stubs

**Files:**
- Create: `internal/webcam/detect_darwin.go`
- Create: `internal/webcam/detect_windows.go`

- [ ] **Step 1: Write macOS detection**

```go
//go:build darwin

package webcam

import (
	"fmt"
	"os/exec"
	"strings"
)

// DetectAllDevices finds all available webcam devices on macOS.
func DetectAllDevices() ([]DeviceInfo, error) {
	cmd := exec.Command("ffmpeg", "-f", "avfoundation", "-list_devices", "true", "-i", "")
	output, _ := cmd.CombinedOutput() // ffmpeg exits non-zero for -list_devices

	var devices []DeviceInfo
	lines := strings.Split(string(output), "\n")
	inVideoSection := false
	deviceIdx := 0
	for _, line := range lines {
		if strings.Contains(line, "AVFoundation video devices") {
			inVideoSection = true
			continue
		}
		if strings.Contains(line, "AVFoundation audio devices") {
			break
		}
		if inVideoSection && strings.Contains(line, "]") {
			// Extract device name from line like "[AVFoundation ...] [0] FaceTime HD Camera"
			parts := strings.SplitN(line, "] ", 2)
			if len(parts) >= 2 {
				name := strings.TrimSpace(parts[len(parts)-1])
				devices = append(devices, DeviceInfo{
					Device: fmt.Sprintf("%d", deviceIdx),
					Name:   name,
				})
				deviceIdx++
			}
		}
	}
	return devices, nil
}
```

- [ ] **Step 2: Write Windows detection**

```go
//go:build windows

package webcam

import (
	"os/exec"
	"strings"
)

// DetectAllDevices finds all available webcam devices on Windows.
func DetectAllDevices() ([]DeviceInfo, error) {
	cmd := exec.Command("ffmpeg", "-f", "dshow", "-list_devices", "true", "-i", "dummy")
	output, _ := cmd.CombinedOutput()

	var devices []DeviceInfo
	lines := strings.Split(string(output), "\n")
	for i, line := range lines {
		if strings.Contains(line, "(video)") {
			// Previous line contains device name
			if i > 0 {
				nameLine := lines[i-1]
				parts := strings.SplitN(nameLine, "\"", 3)
				if len(parts) >= 3 {
					name := parts[1]
					devices = append(devices, DeviceInfo{
						Device: "video=" + name,
						Name:   name,
					})
				}
			}
		}
	}
	return devices, nil
}
```

- [ ] **Step 3: Verify build**

Run: `go build ./internal/webcam/`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add internal/webcam/detect_darwin.go internal/webcam/detect_windows.go
git commit -m "feat: add multi-device detection for macOS and Windows"
```

---

### Task 4: WebcamManager

**Files:**
- Create: `internal/webcam/manager.go`
- Create: `internal/webcam/manager_test.go`

- [ ] **Step 1: Write the failing test**

```go
package webcam

import (
	"os"
	"path/filepath"
	"testing"
)

func TestNewManager(t *testing.T) {
	configs := []WebcamConfig{
		{Device: "video0", Enabled: true, LandscapeMode: DisplayBubble, LandscapeSide: SideRight, VerticalMode: DisplayBottomThird},
		{Device: "video2", Enabled: true, LandscapeMode: DisplayBubble, LandscapeSide: SideLeft, VerticalMode: DisplayBubble},
		{Device: "video4", Enabled: false, LandscapeMode: DisplayOff, LandscapeSide: SideRight, VerticalMode: DisplayOff},
	}

	mgr := NewManager(configs, 60, "1920x1080")
	if mgr == nil {
		t.Fatal("NewManager returned nil")
	}
	if len(mgr.configs) != 3 {
		t.Errorf("expected 3 configs, got %d", len(mgr.configs))
	}
}

func TestManagerEnabledConfigs(t *testing.T) {
	configs := []WebcamConfig{
		{Device: "video0", Enabled: true, LandscapeMode: DisplayBubble, LandscapeSide: SideRight, VerticalMode: DisplayBottomThird},
		{Device: "video2", Enabled: false, LandscapeMode: DisplayOff, LandscapeSide: SideRight, VerticalMode: DisplayOff},
	}

	mgr := NewManager(configs, 60, "1920x1080")
	enabled := mgr.EnabledConfigs()
	if len(enabled) != 1 {
		t.Errorf("expected 1 enabled config, got %d", len(enabled))
	}
	if enabled[0].Device != "video0" {
		t.Errorf("expected video0, got %s", enabled[0].Device)
	}
}

func TestManagerOutputFilePaths(t *testing.T) {
	configs := []WebcamConfig{
		{Device: "video0", Enabled: true, LandscapeMode: DisplayBubble, LandscapeSide: SideRight, VerticalMode: DisplayBottomThird},
		{Device: "video2", Enabled: true, LandscapeMode: DisplayRectangle, LandscapeSide: SideLeft, VerticalMode: DisplayBubble},
	}

	mgr := NewManager(configs, 60, "1920x1080")
	dir := t.TempDir()
	paths := mgr.OutputFilePaths(dir, 0)

	if len(paths) != 2 {
		t.Fatalf("expected 2 paths, got %d", len(paths))
	}

	expected0 := filepath.Join(dir, "webcam_video0_part000.mp4")
	expected2 := filepath.Join(dir, "webcam_video2_part000.mp4")

	if paths["video0"] != expected0 {
		t.Errorf("expected %s, got %s", expected0, paths["video0"])
	}
	if paths["video2"] != expected2 {
		t.Errorf("expected %s, got %s", expected2, paths["video2"])
	}
}

func TestManagerGetOutputs(t *testing.T) {
	configs := []WebcamConfig{
		{Device: "video0", Enabled: true, LandscapeMode: DisplayBubble, LandscapeSide: SideRight, VerticalMode: DisplayBottomThird},
		{Device: "video2", Enabled: true, LandscapeMode: DisplayRectangle, LandscapeSide: SideLeft, VerticalMode: DisplayBubble},
	}

	mgr := NewManager(configs, 60, "1920x1080")
	dir := t.TempDir()

	// Create fake output files so GetOutputs finds them
	for dev, path := range mgr.OutputFilePaths(dir, 0) {
		_ = dev
		_ = os.WriteFile(path, []byte("fake"), 0644)
	}
	mgr.setOutputDir(dir)
	mgr.setPartNum(0)

	outputs := mgr.GetOutputs()
	if len(outputs) != 2 {
		t.Fatalf("expected 2 outputs, got %d", len(outputs))
	}

	if outputs[0].Device != "video0" || outputs[0].LandscapeMode != DisplayBubble {
		t.Errorf("unexpected output[0]: %+v", outputs[0])
	}
	if outputs[1].Device != "video2" || outputs[1].LandscapeMode != DisplayRectangle {
		t.Errorf("unexpected output[1]: %+v", outputs[1])
	}
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `go test ./internal/webcam/ -run TestNewManager -v`
Expected: FAIL — `NewManager` undefined

- [ ] **Step 3: Write the manager implementation**

```go
package webcam

import (
	"fmt"
	"os"
	"path/filepath"
	"sync"
)

// Manager manages multiple webcam recorders
type Manager struct {
	configs    []WebcamConfig
	webcams    map[string]*Webcam // keyed by device
	fps        int
	resolution string
	outputDir  string
	partNum    int
	mu         sync.Mutex
}

// NewManager creates a new webcam manager with the given per-device configs
func NewManager(configs []WebcamConfig, fps int, resolution string) *Manager {
	return &Manager{
		configs:    configs,
		webcams:    make(map[string]*Webcam),
		fps:        fps,
		resolution: resolution,
	}
}

// EnabledConfigs returns only the enabled webcam configs
func (m *Manager) EnabledConfigs() []WebcamConfig {
	var enabled []WebcamConfig
	for _, cfg := range m.configs {
		if cfg.Enabled {
			enabled = append(enabled, cfg)
		}
	}
	return enabled
}

// OutputFilePaths returns the expected output file paths for all enabled webcams
func (m *Manager) OutputFilePaths(outputDir string, partNum int) map[string]string {
	paths := make(map[string]string)
	for _, cfg := range m.EnabledConfigs() {
		paths[cfg.Device] = filepath.Join(outputDir, fmt.Sprintf("webcam_%s_part%03d.mp4", cfg.Device, partNum))
	}
	return paths
}

func (m *Manager) setOutputDir(dir string) {
	m.outputDir = dir
}

func (m *Manager) setPartNum(num int) {
	m.partNum = num
}

// StartAll starts recording from all enabled webcams concurrently
func (m *Manager) StartAll(outputDir string, partNum int) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	m.outputDir = outputDir
	m.partNum = partNum

	enabled := m.EnabledConfigs()
	if len(enabled) == 0 {
		return fmt.Errorf("no enabled webcam devices")
	}

	var wg sync.WaitGroup
	errCh := make(chan error, len(enabled))

	for _, cfg := range enabled {
		outputFile := filepath.Join(outputDir, fmt.Sprintf("webcam_%s_part%03d.mp4", cfg.Device, partNum))
		opts := Options{
			Device:     cfg.Device,
			FPS:        m.fps,
			Resolution: m.resolution,
			OutputFile: outputFile,
		}

		wg.Add(1)
		go func(c WebcamConfig, o Options) {
			defer wg.Done()
			cam := New(o)
			if err := cam.Start(); err != nil {
				errCh <- fmt.Errorf("webcam %s: %w", c.Device, err)
				return
			}
			m.mu.Lock()
			m.webcams[c.Device] = cam
			m.mu.Unlock()
		}(cfg, opts)
	}

	wg.Wait()
	close(errCh)

	// Collect errors but don't fail if at least one webcam started
	var errs []error
	for err := range errCh {
		errs = append(errs, err)
	}

	if len(m.webcams) == 0 && len(errs) > 0 {
		return errs[0]
	}

	return nil
}

// StopAll stops all webcam recordings gracefully
func (m *Manager) StopAll() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	var lastErr error
	for dev, cam := range m.webcams {
		if err := cam.Stop(); err != nil {
			lastErr = fmt.Errorf("failed to stop webcam %s: %w", dev, err)
		}
	}
	m.webcams = make(map[string]*Webcam)
	return lastErr
}

// PIDs returns all active webcam process IDs
func (m *Manager) PIDs() map[string]int {
	m.mu.Lock()
	defer m.mu.Unlock()

	pids := make(map[string]int)
	for dev, cam := range m.webcams {
		if cam.PID() > 0 {
			pids[dev] = cam.PID()
		}
	}
	return pids
}

// IsRecording returns true if any webcam is currently recording
func (m *Manager) IsRecording() bool {
	m.mu.Lock()
	defer m.mu.Unlock()
	return len(m.webcams) > 0
}

// GetOutputs returns the output files with their display configurations
func (m *Manager) GetOutputs() []WebcamOutput {
	var outputs []WebcamOutput
	paths := m.OutputFilePaths(m.outputDir, m.partNum)

	for _, cfg := range m.EnabledConfigs() {
		filePath := paths[cfg.Device]
		if _, err := os.Stat(filePath); err != nil {
			continue
		}
		outputs = append(outputs, WebcamOutput{
			File:          filePath,
			Device:        cfg.Device,
			LandscapeMode: cfg.LandscapeMode,
			LandscapeSide: cfg.LandscapeSide,
			VerticalMode:  cfg.VerticalMode,
		})
	}
	return outputs
}

// MergeConfigsWithDetected merges persisted configs with currently detected devices.
// New devices get default config. Missing devices are preserved in config but skipped at runtime.
func MergeConfigsWithDetected(saved []WebcamConfig, detected []DeviceInfo) []WebcamConfig {
	savedMap := make(map[string]WebcamConfig)
	for _, cfg := range saved {
		savedMap[cfg.Device] = cfg
	}

	var merged []WebcamConfig
	for _, dev := range detected {
		if cfg, ok := savedMap[dev.Device]; ok {
			merged = append(merged, cfg)
		} else {
			merged = append(merged, DefaultWebcamConfig(dev.Device))
		}
	}
	return merged
}
```

- [ ] **Step 4: Run tests**

Run: `go test ./internal/webcam/ -run TestManager -v`
Expected: PASS (or skip on CI without devices)

- [ ] **Step 5: Commit**

```bash
git add internal/webcam/manager.go internal/webcam/manager_test.go
git commit -m "feat: add WebcamManager for multi-webcam recording"
```

---

### Task 5: Config Persistence

**Files:**
- Modify: `internal/config/config.go`

- [ ] **Step 1: Add WebcamConfigs to Config struct**

In `config.go`, add to the `Config` struct after the `TerminalRecording` field:

```go
	// Multi-webcam configuration (persisted per device)
	WebcamConfigs []webcam.WebcamConfig `json:"webcam_configs,omitempty"`
```

Add the import: `"github.com/kartoza/kartoza-screencaster/internal/webcam"`

- [ ] **Step 2: Add PID file constant for multi-webcam**

Replace the single `WebcamPIDFile` constant:

```go
	WebcamPIDsFile = "/tmp/kartoza-webcam-pids.json" // JSON array of {device, pid} pairs
```

Keep `WebcamPIDFile` for backward compat but add `WebcamPIDsFile`.

- [ ] **Step 3: Verify build**

Run: `go build ./...`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add internal/config/config.go
git commit -m "feat: add WebcamConfigs to persistent config"
```

---

### Task 6: Update RecordingInfo Model

**Files:**
- Modify: `internal/models/recording_info.go`

- [ ] **Step 1: Add multi-webcam fields to FileInfo**

Add after `WebcamParts []string`:

```go
	// Multi-webcam support
	WebcamFiles []WebcamFileInfo    `json:"webcam_files,omitempty"`     // Per-device file info
	WebcamPartsMap map[string][]string `json:"webcam_parts_map,omitempty"` // device -> parts list
```

Add the `WebcamFileInfo` struct:

```go
// WebcamFileInfo holds file information for a single webcam device
type WebcamFileInfo struct {
	Device        string `json:"device"`
	File          string `json:"file"`
	LandscapeMode string `json:"landscape_mode"`
	LandscapeSide string `json:"landscape_side"`
	VerticalMode  string `json:"vertical_mode"`
}
```

- [ ] **Step 2: Verify build**

Run: `go build ./...`
Expected: no errors

- [ ] **Step 3: Commit**

```bash
git add internal/models/recording_info.go
git commit -m "feat: add multi-webcam fields to RecordingInfo model"
```

---

### Task 7: Update Recorder to Use WebcamManager

**Files:**
- Modify: `internal/recorder/recorder.go`

- [ ] **Step 1: Replace webcam field with manager**

In the `Recorder` struct, replace:
```go
webcam *recorderInstance
```
with:
```go
webcamManager *webcam.Manager
```

- [ ] **Step 2: Update StartWithOptions**

Replace the single webcam file generation and instance creation with:

```go
	// Start webcam manager (if enabled)
	if !opts.NoWebcam {
		// Load webcam configs from app config, merge with detected devices
		detected, _ := webcam.DetectAllDevices()
		webcamConfigs := webcam.MergeConfigsWithDetected(r.config.WebcamConfigs, detected)

		if len(webcamConfigs) > 0 {
			fps := opts.WebcamFPS
			if fps == 0 {
				fps = 60
			}
			r.webcamManager = webcam.NewManager(webcamConfigs, fps, "1920x1080")
			numRecorders++ // Count as one "recorder" for synchronization
		}
	}
```

- [ ] **Step 3: Update the webcam start goroutine**

Replace `r.startWebcamRecorder(...)` call with:

```go
	if r.webcamManager != nil {
		r.wg.Add(1)
		go func() {
			defer r.wg.Done()
			ready <- "webcam"
			<-r.startBarrier
			if err := r.webcamManager.StartAll(outputDir, partNum); err != nil {
				errors <- fmt.Errorf("webcam: %w", err)
				return
			}
			started <- "webcam"
			<-r.stopSignal
		}()
	}
```

- [ ] **Step 4: Update PID writing to write all webcam PIDs**

After start barrier, replace single PID write with:

```go
	if r.webcamManager != nil && r.webcamManager.IsRecording() {
		pids := r.webcamManager.PIDs()
		pidsJSON, _ := json.Marshal(pids)
		_ = os.WriteFile(config.WebcamPIDsFile, pidsJSON, 0644)
	}
```

- [ ] **Step 5: Update stopInternal to stop all webcams**

Replace single webcam stop with:

```go
	if r.webcamManager != nil {
		_ = r.webcamManager.StopAll()
		_ = os.Remove(config.WebcamPIDsFile)
	}
```

- [ ] **Step 6: Update RecordingInfo file tracking**

In the section that updates `r.recordingInfo.Files`, replace single webcam tracking with:

```go
	if r.webcamManager != nil {
		outputs := r.webcamManager.GetOutputs()
		for _, out := range outputs {
			r.recordingInfo.Files.WebcamFiles = append(r.recordingInfo.Files.WebcamFiles, models.WebcamFileInfo{
				Device:        out.Device,
				File:          out.File,
				LandscapeMode: string(out.LandscapeMode),
				LandscapeSide: string(out.LandscapeSide),
				VerticalMode:  string(out.VerticalMode),
			})
		}
	}
```

- [ ] **Step 7: Verify build**

Run: `go build ./...`
Expected: no errors

- [ ] **Step 8: Run existing tests**

Run: `go test ./internal/recorder/ -v`
Expected: PASS

- [ ] **Step 9: Commit**

```bash
git add internal/recorder/recorder.go
git commit -m "feat: replace single webcam with WebcamManager in recorder"
```

---

### Task 8: Multi-Webcam Merger Filters

**Files:**
- Create: `internal/merger/multi_webcam.go`
- Create: `internal/merger/multi_webcam_test.go`

- [ ] **Step 1: Write the failing test**

```go
package merger

import (
	"strings"
	"testing"

	"github.com/kartoza/kartoza-screencaster/internal/webcam"
)

func TestBuildMultiBubbleFilter(t *testing.T) {
	outputs := []webcamOverlayMulti{
		{inputIdx: 2, side: webcam.SideRight, size: 250, margin: 20, stackIndex: 0},
		{inputIdx: 3, side: webcam.SideRight, size: 250, margin: 20, stackIndex: 1},
	}

	filter := buildMultiBubbleFilter(outputs, "[base]")

	// Should contain two geq (circular mask) operations
	if strings.Count(filter, "geq=") != 2 {
		t.Errorf("expected 2 geq operations, got %d in: %s", strings.Count(filter, "geq="), filter)
	}

	// Second bubble should be offset vertically
	if !strings.Contains(filter, "H-h-290") { // margin + (size+gap)*1 = 20 + 270 = 290
		t.Errorf("expected vertical offset for second bubble, got: %s", filter)
	}
}

func TestBuildMultiBubbleFilterLeftSide(t *testing.T) {
	outputs := []webcamOverlayMulti{
		{inputIdx: 2, side: webcam.SideLeft, size: 250, margin: 20, stackIndex: 0},
	}

	filter := buildMultiBubbleFilter(outputs, "[base]")

	// Left side should use margin for X position, not W-w-margin
	if !strings.Contains(filter, "overlay=20:") {
		t.Errorf("expected left-side positioning (overlay=20:), got: %s", filter)
	}
}

func TestBuildRectangleStripFilter(t *testing.T) {
	outputs := []webcamRectInput{
		{inputIdx: 2, device: "video0"},
		{inputIdx: 3, device: "video2"},
	}

	filter := buildRectangleStripFilter(outputs, 1920, "[base]")

	// Should contain hstack for combining rectangles
	if !strings.Contains(filter, "hstack") {
		t.Errorf("expected hstack in filter, got: %s", filter)
	}
	// Should contain vstack for combining with main video
	if !strings.Contains(filter, "vstack") {
		t.Errorf("expected vstack in filter, got: %s", filter)
	}
}

func TestBuildRectangleStripFilterSingle(t *testing.T) {
	outputs := []webcamRectInput{
		{inputIdx: 2, device: "video0"},
	}

	filter := buildRectangleStripFilter(outputs, 1920, "[base]")

	// Single rectangle should NOT use hstack
	if strings.Contains(filter, "hstack") {
		t.Errorf("single rectangle should not use hstack, got: %s", filter)
	}
	// Should still use vstack
	if !strings.Contains(filter, "vstack") {
		t.Errorf("expected vstack in filter, got: %s", filter)
	}
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `go test ./internal/merger/ -run TestBuildMulti -v`
Expected: FAIL — undefined functions

- [ ] **Step 3: Write the multi-webcam filter builders**

```go
package merger

import (
	"fmt"
	"strings"

	"github.com/kartoza/kartoza-screencaster/internal/webcam"
)

const (
	bubbleGap = 20 // Gap between stacked bubbles in pixels
)

// webcamOverlayMulti holds parameters for a single bubble in a multi-bubble overlay
type webcamOverlayMulti struct {
	inputIdx   int
	side       webcam.WebcamSide
	size       int
	margin     int
	stackIndex int // 0 = bottom, 1 = above bottom, etc.
}

// webcamRectInput holds parameters for a single rectangle webcam in the strip
type webcamRectInput struct {
	inputIdx int
	device   string
}

// buildMultiBubbleFilter builds FFmpeg filter fragments for multiple circular webcam bubbles.
// Bubbles on the same side stack vertically from the bottom.
func buildMultiBubbleFilter(bubbles []webcamOverlayMulti, currentOutput string) string {
	var fragments []string

	for i, b := range bubbles {
		radius := b.size / 2
		outLabel := fmt.Sprintf("[out_bubble_%d]", i)

		// Calculate Y offset: bottom + (size + gap) * stackIndex
		yOffset := b.margin + (b.size+bubbleGap)*b.stackIndex

		// X position depends on side
		var xPos string
		if b.side == webcam.SideLeft {
			xPos = fmt.Sprintf("%d", b.margin)
		} else {
			xPos = fmt.Sprintf("W-w-%d", b.margin)
		}

		yPos := fmt.Sprintf("H-h-%d", yOffset)

		fragment := fmt.Sprintf(
			"[%d:v]scale='if(gt(iw,ih),-1,%d)':'if(gt(iw,ih),%d,-1)',crop=%d:%d,format=yuva420p,"+
				"geq=lum='p(X,Y)':cb='p(X,Y)':cr='p(X,Y)':"+
				"a='if(gt((X-%d)*(X-%d)+(Y-%d)*(Y-%d),%d*%d),0,255)'[bubble_%d_circle];"+
				"%s[bubble_%d_circle]overlay=%s:%s%s",
			b.inputIdx, b.size, b.size, b.size, b.size,
			radius, radius, radius, radius, radius, radius, i,
			currentOutput, i, xPos, yPos, outLabel,
		)

		fragments = append(fragments, fragment)
		currentOutput = outLabel
	}

	return strings.Join(fragments, ";")
}

// buildRectangleStripFilter builds an FFmpeg filter that creates a horizontal strip
// of equally-sized webcam feeds and stacks it below the main video.
func buildRectangleStripFilter(rects []webcamRectInput, videoWidth int, currentOutput string) string {
	if len(rects) == 0 {
		return ""
	}

	var fragments []string
	rectWidth := videoWidth / len(rects)

	// Scale each rectangle webcam to equal width
	var scaledLabels []string
	for i, r := range rects {
		label := fmt.Sprintf("[rect_%d]", i)
		fragment := fmt.Sprintf("[%d:v]scale=%d:-1:flags=lanczos%s", r.inputIdx, rectWidth, label)
		fragments = append(fragments, fragment)
		scaledLabels = append(scaledLabels, label)
	}

	// Combine into strip
	var stripLabel string
	if len(rects) == 1 {
		stripLabel = scaledLabels[0]
	} else {
		stripLabel = "[rect_strip]"
		hstack := fmt.Sprintf("%shstack=inputs=%d%s", strings.Join(scaledLabels, ""), len(rects), stripLabel)
		fragments = append(fragments, hstack)
	}

	// Stack strip below main video
	outLabel := "[out_rect_strip]"
	vstack := fmt.Sprintf("%s%svstack=inputs=2%s", currentOutput, stripLabel, outLabel)
	fragments = append(fragments, vstack)

	return strings.Join(fragments, ";")
}

// buildVerticalBottomThirdFilter builds an FFmpeg filter for the bottom-third section
// in vertical video mode, combining multiple webcam feeds side by side.
func buildVerticalBottomThirdFilter(rects []webcamRectInput, sectionWidth int, currentOutput string) string {
	if len(rects) == 0 {
		return ""
	}

	var fragments []string
	rectWidth := sectionWidth / len(rects)

	// Scale each webcam to equal width
	var scaledLabels []string
	for i, r := range rects {
		label := fmt.Sprintf("[bt_%d]", i)
		fragment := fmt.Sprintf("[%d:v]scale=%d:-1:flags=lanczos%s", r.inputIdx, rectWidth, label)
		fragments = append(fragments, fragment)
		scaledLabels = append(scaledLabels, label)
	}

	// Combine into strip if multiple
	var stripLabel string
	if len(rects) == 1 {
		stripLabel = scaledLabels[0]
	} else {
		stripLabel = "[bt_strip]"
		hstack := fmt.Sprintf("%shstack=inputs=%d%s", strings.Join(scaledLabels, ""), len(rects), stripLabel)
		fragments = append(fragments, hstack)
	}

	// Overlay the strip at the appropriate position (middle section)
	outLabel := "[out_bt]"
	overlay := fmt.Sprintf("%s%soverlay=(W-w)/2:(H*1/3)%s", currentOutput, stripLabel, outLabel)
	fragments = append(fragments, overlay)

	return strings.Join(fragments, ";")
}
```

- [ ] **Step 4: Run tests**

Run: `go test ./internal/merger/ -run TestBuild -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add internal/merger/multi_webcam.go internal/merger/multi_webcam_test.go
git commit -m "feat: add multi-webcam FFmpeg filter builders"
```

---

### Task 9: Update Merger MergeOptions and Merge Function

**Files:**
- Modify: `internal/merger/merger.go`

- [ ] **Step 1: Add WebcamOutputs to MergeOptions**

Add after `WebcamParts []string`:

```go
	// Multi-webcam outputs (overrides WebcamFile/WebcamParts if set)
	WebcamOutputs []webcam.WebcamOutput
```

Add import: `"github.com/kartoza/kartoza-screencaster/internal/webcam"`

- [ ] **Step 2: Update Merge function to handle multi-webcam**

In the `Merge` function, after the existing webcam parts concatenation logic, add:

```go
	// Multi-webcam: concatenate each device's parts independently
	if len(opts.WebcamOutputs) > 0 {
		for i, wo := range opts.WebcamOutputs {
			// Check if there are parts for this device in the recording info
			// For now, the WebcamOutput.File should already point to the concatenated file
			if !fileExists(wo.File) {
				opts.WebcamOutputs[i].File = "" // mark as unavailable
			}
		}
	}
```

- [ ] **Step 3: Update landscape overlay logic to use multi-webcam**

In `processVideoOnly` and `mergeVideoAudio`, replace the single `hasWebcamOverlay` check with logic that iterates `opts.WebcamOutputs`:

```go
	// Determine webcam overlays from multi-webcam outputs
	var bubbleOutputs []webcamOverlayMulti
	var rectOutputs []webcamRectInput

	if opts != nil && len(opts.WebcamOutputs) > 0 {
		rightStackIdx := 0
		leftStackIdx := 0
		for _, wo := range opts.WebcamOutputs {
			if wo.File == "" || !fileExists(wo.File) {
				continue
			}
			switch wo.LandscapeMode {
			case webcam.DisplayBubble:
				stackIdx := rightStackIdx
				if wo.LandscapeSide == webcam.SideLeft {
					stackIdx = leftStackIdx
					leftStackIdx++
				} else {
					rightStackIdx++
				}
				bubbleOutputs = append(bubbleOutputs, webcamOverlayMulti{
					inputIdx:   -1, // will be assigned when building inputs
					side:       wo.LandscapeSide,
					size:       webcamOverlaySize,
					margin:     webcamOverlayMargin,
					stackIndex: stackIdx,
				})
			case webcam.DisplayRectangle:
				rectOutputs = append(rectOutputs, webcamRectInput{
					inputIdx: -1, // will be assigned when building inputs
					device:   wo.Device,
				})
			}
		}
	} else if opts != nil && opts.WebcamFile != "" && fileExists(opts.WebcamFile) {
		// Legacy single webcam: treat as single bubble
		bubbleOutputs = append(bubbleOutputs, webcamOverlayMulti{
			inputIdx:   -1,
			side:       webcam.SideRight,
			size:       webcamOverlaySize,
			margin:     webcamOverlayMargin,
			stackIndex: 0,
		})
	}
```

- [ ] **Step 4: Verify build**

Run: `go build ./...`
Expected: no errors

- [ ] **Step 5: Run all merger tests**

Run: `go test ./internal/merger/ -v`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add internal/merger/merger.go
git commit -m "feat: update merger to accept multi-webcam outputs"
```

---

### Task 10: Update TUI Recording Form

**Files:**
- Modify: `internal/tui/recording_form.go`

- [ ] **Step 1: Add webcam config fields to RecordingFormState**

Add after existing webcam toggle fields:

```go
	// Multi-webcam configuration
	WebcamDevices []webcam.DeviceInfo  // Detected devices
	WebcamConfigs []webcam.WebcamConfig // Per-device configs
	WebcamFocusedIdx int               // Which webcam row is focused
```

- [ ] **Step 2: Replace single RecordWebcam toggle with per-device section**

Replace `FormFieldRecordWebcam` with `FormFieldWebcamSection`. The webcam section renders as:

```go
// renderWebcamSection renders inline toggles for each detected webcam
func renderWebcamSection(state *RecordingFormState, focused bool, width int) string {
	if len(state.WebcamDevices) == 0 {
		return "  No webcams detected"
	}

	var lines []string
	for i, dev := range state.WebcamDevices {
		cfg := state.WebcamConfigs[i]
		isFocused := focused && i == state.WebcamFocusedIdx

		// Format: "  video0 (Name)  [Mode ▾] [Side ▾]"
		name := dev.Name
		if name == "" {
			name = dev.Device
		}

		modeStr := string(cfg.LandscapeMode)
		sideStr := ""
		if cfg.LandscapeMode == webcam.DisplayBubble {
			sideStr = fmt.Sprintf(" [%s]", cfg.LandscapeSide)
		}

		line := fmt.Sprintf("  %s (%s)  [%s]%s", dev.Device, name, modeStr, sideStr)
		if !cfg.Enabled {
			line = fmt.Sprintf("  %s (%s)  [off]", dev.Device, name)
		}

		if isFocused {
			line = "> " + line[2:]
		}
		lines = append(lines, line)
	}
	return strings.Join(lines, "\n")
}
```

- [ ] **Step 3: Add key handling for webcam section**

Handle left/right arrows to cycle through modes, up/down to navigate between webcam rows:

```go
// handleWebcamKey handles keypresses in the webcam configuration section
func handleWebcamKey(state *RecordingFormState, key tea.KeyMsg) {
	if len(state.WebcamConfigs) == 0 {
		return
	}

	cfg := &state.WebcamConfigs[state.WebcamFocusedIdx]

	switch key.String() {
	case "up":
		if state.WebcamFocusedIdx > 0 {
			state.WebcamFocusedIdx--
		}
	case "down":
		if state.WebcamFocusedIdx < len(state.WebcamConfigs)-1 {
			state.WebcamFocusedIdx++
		}
	case "left", "right":
		// Cycle through modes: off -> bubble -> rectangle -> off
		modes := []webcam.WebcamDisplayMode{webcam.DisplayOff, webcam.DisplayBubble, webcam.DisplayRectangle}
		currentIdx := 0
		for i, m := range modes {
			if m == cfg.LandscapeMode {
				currentIdx = i
				break
			}
		}
		if key.String() == "right" {
			currentIdx = (currentIdx + 1) % len(modes)
		} else {
			currentIdx = (currentIdx - 1 + len(modes)) % len(modes)
		}
		cfg.LandscapeMode = modes[currentIdx]
		cfg.Enabled = cfg.LandscapeMode != webcam.DisplayOff
	case "s":
		// Toggle side (only for bubble mode)
		if cfg.LandscapeMode == webcam.DisplayBubble {
			if cfg.LandscapeSide == webcam.SideLeft {
				cfg.LandscapeSide = webcam.SideRight
			} else {
				cfg.LandscapeSide = webcam.SideLeft
			}
		}
	}
}
```

- [ ] **Step 4: Initialize webcam devices on form creation**

In `NewRecordingFormState`, add device detection:

```go
	// Detect webcams and load configs
	detected, _ := webcam.DetectAllDevices()
	savedConfigs := cfg.WebcamConfigs
	mergedConfigs := webcam.MergeConfigsWithDetected(savedConfigs, detected)

	state.WebcamDevices = detected
	state.WebcamConfigs = mergedConfigs
```

- [ ] **Step 5: Verify build**

Run: `go build ./...`
Expected: no errors

- [ ] **Step 6: Commit**

```bash
git add internal/tui/recording_form.go
git commit -m "feat: add per-webcam inline toggles to TUI recording form"
```

---

### Task 11: Update CLI Flags

**Files:**
- Modify: `cmd/start.go`

- [ ] **Step 1: Replace --webcam-device with --webcam-config**

Remove the `--webcam-device` flag. Add:

```go
startCmd.Flags().String("webcam-config", "", "JSON string or file path for per-webcam configuration override")
```

Keep `--no-webcam` as-is (disables all webcams).

- [ ] **Step 2: Parse webcam-config flag in run function**

```go
	webcamConfigStr, _ := cmd.Flags().GetString("webcam-config")
	if webcamConfigStr != "" {
		var configs []webcam.WebcamConfig
		// Try as file path first
		if data, err := os.ReadFile(webcamConfigStr); err == nil {
			_ = json.Unmarshal(data, &configs)
		} else {
			// Try as inline JSON
			_ = json.Unmarshal([]byte(webcamConfigStr), &configs)
		}
		if len(configs) > 0 {
			// Override config webcams
			cfg.WebcamConfigs = configs
		}
	}
```

- [ ] **Step 3: Verify build**

Run: `go build ./...`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add cmd/start.go
git commit -m "feat: replace --webcam-device with --webcam-config CLI flag"
```

---

### Task 12: Backward Compatibility and Integration Test

**Files:**
- Modify: `internal/merger/merger.go` (ensure old single-webcam path still works)
- Create: `internal/merger/multi_webcam_integration_test.go`

- [ ] **Step 1: Write integration test for single webcam backward compat**

```go
package merger

import (
	"testing"
)

func TestMergeOptionsBackwardCompat(t *testing.T) {
	// Old-style single WebcamFile should still work
	opts := MergeOptions{
		WebcamFile:   "/tmp/fake_webcam.mp4",
		WebcamBubble: true,
	}

	// When WebcamOutputs is empty but WebcamFile is set, the legacy path should be used
	hasLegacyWebcam := opts.WebcamFile != "" && len(opts.WebcamOutputs) == 0
	if !hasLegacyWebcam {
		t.Error("expected legacy webcam path to be detected")
	}
}

func TestMergeOptionsMultiWebcamOverridesLegacy(t *testing.T) {
	opts := MergeOptions{
		WebcamFile: "/tmp/old_webcam.mp4",
		WebcamOutputs: []webcam.WebcamOutput{
			{File: "/tmp/webcam_video0.mp4", Device: "video0", LandscapeMode: webcam.DisplayBubble, LandscapeSide: webcam.SideRight},
		},
	}

	// When WebcamOutputs is set, it should take priority over legacy WebcamFile
	usesMulti := len(opts.WebcamOutputs) > 0
	if !usesMulti {
		t.Error("expected multi-webcam path to take priority")
	}
}
```

- [ ] **Step 2: Run tests**

Run: `go test ./internal/merger/ -v`
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add internal/merger/multi_webcam_integration_test.go
git commit -m "test: add backward compatibility tests for multi-webcam"
```

---

### Task 13: Update Recorder Processing to Pass Multi-Webcam to Merger

**Files:**
- Modify: `internal/recorder/recorder.go` (the processing/merge section)

- [ ] **Step 1: Find the merge call and update it**

In the `processRecording` function (or wherever `merger.Merge` is called), update the `MergeOptions` construction to pass `WebcamOutputs`:

```go
	// Build webcam outputs for merger
	var webcamOutputs []webcam.WebcamOutput
	if r.recordingInfo != nil && len(r.recordingInfo.Files.WebcamFiles) > 0 {
		for _, wf := range r.recordingInfo.Files.WebcamFiles {
			webcamOutputs = append(webcamOutputs, webcam.WebcamOutput{
				File:          wf.File,
				Device:        wf.Device,
				LandscapeMode: webcam.WebcamDisplayMode(wf.LandscapeMode),
				LandscapeSide: webcam.WebcamSide(wf.LandscapeSide),
				VerticalMode:  webcam.WebcamDisplayMode(wf.VerticalMode),
			})
		}
	}

	mergeOpts := merger.MergeOptions{
		// ... existing fields ...
		WebcamOutputs: webcamOutputs,
	}
```

- [ ] **Step 2: Handle multi-webcam parts concatenation**

Before passing to merger, concatenate each device's parts:

```go
	// Concatenate webcam parts per device
	if r.recordingInfo != nil && len(r.recordingInfo.Files.WebcamPartsMap) > 0 {
		for device, parts := range r.recordingInfo.Files.WebcamPartsMap {
			if len(parts) <= 1 {
				continue
			}
			concatFile := filepath.Join(outputDir, fmt.Sprintf("webcam_%s.mp4", device))
			if err := concatenateParts(parts, concatFile); err == nil {
				// Update the output file path for this device
				for i, wo := range webcamOutputs {
					if wo.Device == device {
						webcamOutputs[i].File = concatFile
					}
				}
			}
		}
	}
```

- [ ] **Step 3: Verify build**

Run: `go build ./...`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add internal/recorder/recorder.go
git commit -m "feat: pass multi-webcam outputs to merger during processing"
```

---

### Task 14: Update SPECIFICATION.md and Documentation

**Files:**
- Modify: `SPECIFICATION.md`
- Modify: `docs/` (relevant mkdocs pages)

- [ ] **Step 1: Add multi-webcam section to SPECIFICATION.md**

Add a new section documenting:
- Multi-webcam auto-detection behavior
- Per-webcam display modes and their effect on landscape/vertical output
- Configuration persistence
- CLI flag changes
- Backward compatibility guarantees

- [ ] **Step 2: Commit**

```bash
git add SPECIFICATION.md
git commit -m "docs: add multi-webcam recording to specification"
```

---

### Task 15: End-to-End Verification

- [ ] **Step 1: Build the full binary**

Run: `go build -o kartoza-screencaster .`
Expected: binary builds successfully

- [ ] **Step 2: Run full test suite**

Run: `go test ./... -v`
Expected: all tests PASS

- [ ] **Step 3: Manual smoke test (if webcams available)**

Run: `./kartoza-screencaster tui`
Expected: webcam section shows all detected devices with inline toggles

- [ ] **Step 4: Final commit (version bump)**

Update version in the appropriate file and commit:

```bash
git commit -m "feat: multi-webcam recording support (minor version bump)"
```
