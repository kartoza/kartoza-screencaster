# Multi-Webcam Recording Design

## Overview

Record from all detected webcams simultaneously, with per-webcam display mode configuration that persists across sessions.

## Requirements

1. Auto-detect and record from ALL available webcam devices by default
2. Each webcam independently configurable: display mode + side
3. Configuration persists across sessions (by device path)
4. Backward compatible with existing single-webcam recordings
5. Human-readable device names shown in TUI

## Display Modes

### Per-Webcam Options

| Mode | Landscape Behavior | Vertical Behavior |
|------|-------------------|-------------------|
| Bubble | Circle overlay on main video, user picks left/right side | Circle overlay on screen area at top |
| Rectangle | Side-by-side strip below main video | N/A for vertical |
| Bottom Third | N/A for landscape | Fills middle section of vertical layout |
| Off | Not shown | Not shown |

### Layout Rules

- **Bubbles on same side**: stack vertically (bottom bubble first, next one above it)
- **Multiple rectangles (landscape)**: equal-width panels, `hstack`ed into a strip below main video
- **Multiple bottom-third (vertical)**: side by side in middle section, equal width
- **Defaults for new devices**: enabled=true, landscape_mode=bubble, landscape_side=right, vertical_mode=bubble

## Architecture

### WebcamManager (`internal/webcam/manager.go`)

New abstraction that owns N webcam recorders:

```go
type Manager struct {
    webcams    map[string]*Webcam       // keyed by device path
    configs    map[string]*WebcamConfig
    fps        int
    resolution string
}

func NewManager(configs []WebcamConfig, fps int, resolution string) *Manager
func (m *Manager) StartAll(outputDir string, partNum int) error
func (m *Manager) StopAll() error
func (m *Manager) GetOutputFiles() []WebcamOutput
func DetectAllDevices() ([]DeviceInfo, error)
```

### Data Types

```go
type WebcamDisplayMode string
const (
    DisplayOff         WebcamDisplayMode = "off"
    DisplayBubble      WebcamDisplayMode = "bubble"
    DisplayRectangle   WebcamDisplayMode = "rectangle"    // landscape only
    DisplayBottomThird WebcamDisplayMode = "bottom_third" // vertical only
)

type WebcamSide string
const (
    SideLeft  WebcamSide = "left"
    SideRight WebcamSide = "right"
)

type WebcamConfig struct {
    Device        string            `json:"device"`
    Enabled       bool              `json:"enabled"`
    LandscapeMode WebcamDisplayMode `json:"landscape_mode"`
    LandscapeSide WebcamSide        `json:"landscape_side"`
    VerticalMode  WebcamDisplayMode `json:"vertical_mode"`
}

type WebcamOutput struct {
    File          string
    Device        string
    LandscapeMode WebcamDisplayMode
    LandscapeSide WebcamSide
    VerticalMode  WebcamDisplayMode
}

type DeviceInfo struct {
    Device string // e.g. "video0"
    Name   string // e.g. "Logitech C920"
}
```

### Recorder Changes

Replace:
```go
webcam *recorderInstance  // single
```

With:
```go
webcamManager *webcam.Manager  // manages all
```

- PID tracking: `/tmp/kartoza-webcam-pids.json` (array of PIDs)
- Part files: `webcam_<device>_part000.mp4` per webcam per part
- `RecordingInfo.Files` gains `WebcamFiles []WebcamFileInfo` and `WebcamParts map[string][]string`
- Old `WebcamFile` field still read for backward compatibility

### Merger Changes

`MergeOptions` replaces `WebcamFile string` with `WebcamOutputs []WebcamOutput`.

**Landscape FFmpeg filter pipeline:**
1. Separate webcams by mode (rectangle vs bubble vs off)
2. Rectangle webcams: scale each to equal width, `hstack`, then `vstack` below main video
3. Bubble webcams: group by side, stack vertically per side using offset `(size + gap) * index`

**Vertical FFmpeg filter pipeline:**
1. Bottom-third webcams: scale to equal width, `hstack` into middle section
2. Bubble webcams: overlay on screen area, stacked per side

Each webcam's parts are concatenated independently before compositing.

### Config Persistence

Stored in `~/.config/kartoza-screencaster/config.json`:

```json
{
  "webcams": [
    {
      "device": "video0",
      "enabled": true,
      "landscape_mode": "bubble",
      "landscape_side": "right",
      "vertical_mode": "bottom_third"
    },
    {
      "device": "video2",
      "enabled": true,
      "landscape_mode": "bubble",
      "landscape_side": "left",
      "vertical_mode": "bubble"
    }
  ]
}
```

Startup behavior:
1. Detect all devices
2. Match against saved config by device path
3. New devices: enabled=true, landscape_mode=bubble, landscape_side=right, vertical_mode=bubble
4. Saved devices not currently detected: silently skipped

### Device Name Detection

**Linux:** Read `/sys/class/video4linux/videoN/name` for human-readable label, fall back to device path.

**macOS/Windows:** Parse ffmpeg device listing output.

### TUI Recording Form

Webcam section with inline toggles per detected device:

```
Webcams:
  video0 (Logitech C920)   [Bubble ▾] [Right ▾]
  video2 (Built-in Camera)  [Bubble ▾] [Left ▾]
  video4 (OBS Virtual)      [Off ▾]
```

Each row: device name + mode toggle + side toggle (side only visible when mode is Bubble).

### CLI Interface

- `--no-webcam`: disable all webcam recording
- `--webcam-config <json-or-path>`: override per-webcam config for power users
- Remove old `--webcam-device` flag (deprecated, map to first webcam if provided for backward compat)

## Backward Compatibility

- Existing recordings with single `WebcamFile` field: treated as single-webcam with bubble-right mode
- Old `--webcam-device` CLI flag: maps to first webcam config entry
- Single webcam systems: behave identically to current behavior (manager with one device)

## Testing

- Unit tests for WebcamManager with mock devices
- Unit tests for merger filter generation with multiple webcam inputs
- Integration test: verify ffmpeg filter strings are valid for 1, 2, and 3 webcam configurations
- TUI test: verify device list rendering with various detected device counts
