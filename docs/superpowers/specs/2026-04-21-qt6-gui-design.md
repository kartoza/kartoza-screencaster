# Qt6 GUI Migration Design

## Overview

Replace the Bubble Tea TUI and fyne.io/systray with a full Qt6 GUI using mappu/miqt Go bindings. Single binary, sidebar navigation, enhanced UX with live previews, drag-drop, built-in video player, and visual overlay editor.

## Toolkit

**mappu/miqt** — MIT-licensed Go bindings for Qt6. Actively maintained (last commit March 2026). Uses CGo (project already accepts CGo for systray). Provides access to Qt6 Widgets and Multimedia modules.

## Binary & CLI

Single `kartoza-screencaster` binary:
- Default launch (no args): opens Qt6 GUI with system tray
- CLI subcommands remain unchanged: `start`, `stop`, `status`, `pause`, `resume`, `process`
- No TUI mode — removed entirely

## Package Structure

```
internal/gui/                     — all Qt6 GUI code
internal/gui/mainwindow.go        — main window with sidebar navigation
internal/gui/tray.go              — QSystemTrayIcon (replaces fyne.io/systray)
internal/gui/pages/               — one file per page
internal/gui/pages/record.go      — recording setup + live preview + controls
internal/gui/pages/history.go     — recording browser with preview panel
internal/gui/pages/settings.go    — configuration form
internal/gui/pages/processing.go  — step-by-step progress with thumbnail
internal/gui/pages/player.go      — built-in video player
internal/gui/widgets/             — reusable custom widgets
internal/gui/widgets/webcam_preview.go    — live webcam feed widget
internal/gui/widgets/monitor_picker.go    — visual monitor selector
internal/gui/widgets/logo_picker.go       — drag-drop logo zones
internal/gui/widgets/overlay_editor.go    — draggable webcam position editor
internal/gui/widgets/waveform.go          — audio level waveform
internal/gui/resources/           — icons, stylesheets, assets
```

## What Gets Removed

- `internal/tui/` — entire package
- `internal/systray/` — entire package
- `fyne.io/systray` dependency
- `cmd/tui.go` — TUI subcommand
- Terminal emulator detection code in systray

## What Stays Unchanged

- `internal/recorder/` — recording engine
- `internal/merger/` — video processing pipeline
- `internal/webcam/` — webcam detection and recording
- `internal/audio/` — audio processing
- `internal/config/` — configuration persistence
- `internal/models/` — data models
- `internal/monitor/` — monitor detection
- `internal/deps/` — dependency detection
- `internal/notify/` — desktop notifications
- `cmd/start.go`, `cmd/stop.go`, etc. — CLI subcommands

## Main Window Layout

Fixed minimum size 1000x700, resizable. Title: "Kartoza Screencaster".

```
+------------------+------------------------------------------+
|                  |                                          |
|   [app icon]     |                                          |
|   Screencaster   |                                          |
|                  |                                          |
|   [ Record   ]   |         Main Content Area               |
|   [ History  ]   |         (changes per page)               |
|   [ Settings ]   |                                          |
|                  |                                          |
|                  |                                          |
|                  |                                          |
|                  |                                          |
|                  |                                          |
|                  +------------------------------------------+
|                  | Status Bar: recording state, elapsed     |
+------------------+------------------------------------------+
|  Made with love by Kartoza | Donate! | GitHub               |
+-------------------------------------------------------------+
```

- Sidebar: ~200px wide, dark themed, navigation buttons with icons, active page highlighted
- Status bar: current state (Idle / Recording 00:05:23 / Processing Step 3/5), pinned to bottom of content area
- Footer: "Made with love by Kartoza | Donate! | GitHub" pinned to bottom of window

## Record Page

Two-column layout: setup on left, previews on right.

### Left Column — Recording Setup
- Title text input
- Recording number input
- Presenter text input
- Topic dropdown
- Monitor: visual picker widget (see Monitor Picker Widget)
- Webcams: per-device rows with mode dropdown (Bubble/Rectangle/Off), side dropdown (Left/Right), inline controls
- Output toggles: vertical video, left split, add logos
- Logo drop zones: three drag-drop areas (left, right, bottom) with image previews
- Title color dropdown
- Description textarea

### Right Column — Preview & Controls
- Live webcam feed: grid of enabled webcam feeds, rendered via ffmpeg frame piping into QLabel
- Monitor preview: static screenshot/thumbnail of selected monitor
- Overlay preview: proportional screen frame showing bubble/rectangle positions. Clickable to open the Overlay Editor
- Start button: prominent, triggers 5-4-3-2-1 countdown then begins recording
- During recording: transforms to show elapsed time, pause button, stop button, audio level waveform

## History Page

Two-panel layout: recording list on left, preview panel on right.

### Left Panel — Recording List
- Search bar: text search across titles
- Filter dropdown: All, Completed, Processing, Failed
- Scrollable list of recordings showing: number, title, date, duration, sources (icons), file size
- Click to select, double-click to play

### Right Panel — Preview
- Video thumbnail (first frame via ffprobe/ffmpeg)
- Metadata display: title, duration, file sizes, settings used
- Action buttons: Play, Reprocess, Upload (YouTube), Delete
- Delete requires confirmation dialog

## Settings Page

Single scrollable column form. All changes auto-save on value change (Qt signals connected to config.Save).

### Sections
- **Recording Defaults:** output directory (with browse button), default presenter, webcam FPS, audio device dropdown
- **Audio Processing:** normalize toggle, denoise toggle, room noise file (browse + record-in-place button for 5-second capture)
- **Logo Library:** logo directory (with browse), thumbnail grid of available logos, title color dropdown, background color dropdown, GIF loop mode dropdown
- **YouTube:** connection status, connect/disconnect/re-authorize buttons
- **Topics:** editable list with add/remove buttons

## Built-in Video Player

Uses Qt6 Multimedia (`QMediaPlayer` + `QVideoWidget`).

```
+-------------------------------------------------------------+
|  < Back to History                          [Landscape v]    |
|  +-------------------------------------------------------+  |
|  |                  Video Playback                       |  |
|  +-------------------------------------------------------+  |
|  |  [|<] [<<]  [ > / || ]  [>>] [>|]   00:05:23/15:23  |  |
|  |  [==================|----------------------] volume   |  |
|  +-------------------------------------------------------+  |
|  Toggle: [Merged] [Vertical] [Raw Screen] [Raw Webcam]      |
+-------------------------------------------------------------+
```

- Standard playback controls: play/pause, seek, skip, volume
- Source toggle: switch between merged, vertical, raw screen, raw webcam files
- Aspect ratio dropdown: landscape/vertical preview
- Double-click for fullscreen
- Back button returns to history with same recording selected

## Visual Webcam Overlay Position Editor

Opens from the Record page overlay preview area.

```
+-------------------------------------------------------------+
|  Overlay Editor                              [Done]          |
|  +-------------------------------------------------------+  |
|  |              Screen Frame (proportional)              |  |
|  |                                    +-----+            |  |
|  |                                    | cam |            |  |
|  |                                    |  0  |            |  |
|  |                                    +-----+            |  |
|  |          +-----+                                      |  |
|  |          | cam |                                      |  |
|  |          |  2  |                                      |  |
|  |          +-----+                                      |  |
|  +-------------------------------------------------------+  |
|  Snap: [x] Corners  [x] Edges  [ ] Free position            |
|  cam0: 250px [- / +]    cam2: 200px [- / +]                 |
+-------------------------------------------------------------+
```

- Draggable webcam overlays on proportional screen frame
- Circles show live webcam feed thumbnails when cameras are active
- Snap to corners/edges by default, free positioning optional
- Per-webcam size controls (diameter for bubbles, width for rectangles)
- Positions saved to config, translated to FFmpeg overlay coordinates by merger

### WebcamConfig Extension

```go
type WebcamConfig struct {
    Device        string            `json:"device"`
    Enabled       bool              `json:"enabled"`
    LandscapeMode WebcamDisplayMode `json:"landscape_mode"`
    LandscapeSide WebcamSide        `json:"landscape_side"`
    VerticalMode  WebcamDisplayMode `json:"vertical_mode"`
    OverlayX      int               `json:"overlay_x,omitempty"`    // custom X position (-1 = auto)
    OverlayY      int               `json:"overlay_y,omitempty"`    // custom Y position (-1 = auto)
    OverlaySize   int               `json:"overlay_size,omitempty"` // diameter/width in px (0 = default 250)
}
```

## Processing Page

Shown during post-recording processing.

```
+-------------------------------------------------------------+
|  Processing: #003 - My Tutorial                             |
|  +-------------------------------------------------------+  |
|  |         Video Thumbnail Preview                       |  |
|  |         (updates at each processing step)             |  |
|  +-------------------------------------------------------+  |
|  Step 1: Analyzing audio          [==============] Done      |
|  Step 2: Normalizing audio        [=========     ] 65%       |
|  Step 3: Merging video & audio    [              ] Pending   |
|  Step 4: Creating vertical video  [              ] Pending   |
|  Elapsed: 01:23    Estimated remaining: ~02:00               |
|  [Cancel]                                                    |
+-------------------------------------------------------------+
```

- QProgressBar per processing step, connected to existing ProgressUpdate channel
- Video thumbnail updates when each step completes (frame extracted via ffmpeg)
- Elapsed time and estimated remaining based on progress percentage
- Cancel button aborts ffmpeg processes and cleans up partial files
- Auto-navigates to player page when processing completes

## System Tray (Qt6)

Replaces fyne.io/systray with QSystemTrayIcon.

### Click Behavior
- **Single click (idle):** Toggle main window visibility
- **Single click (recording):** Pause/resume
- **Double click (recording):** Stop recording
- **Right-click menu:**
  - Idle: "Start Recording", "Open Window", separator, "Quit"
  - Recording: "Pause/Resume", "Stop Recording", "Open Window", separator, "Quit"
  - Processing: "Open Window", separator, "Quit"

### Icons
Same rotating/countdown icon system as current implementation, rendered via QIcon and QPixmap.

## Live Webcam Preview Widget

Captures frames from webcam devices and renders them in the GUI.

**Implementation approach:**
- Use ffmpeg to capture frames from each enabled webcam: `ffmpeg -f v4l2 -i /dev/videoN -vf fps=15 -f rawvideo -pix_fmt rgb24 pipe:1`
- Read raw frames from stdout pipe in a goroutine
- Convert to QImage and display in QLabel
- 15 FPS for preview (lower than recording FPS to reduce CPU)
- Start/stop preview independently of recording
- Grid layout: arrange webcam feeds in a grid (1x1, 1x2, 2x2 depending on count)

## Visual Monitor Picker Widget

Shows a proportional diagram of detected monitors.

**Implementation:**
- Query monitor positions/resolutions via existing `monitor.ListMonitors()`
- Draw proportional rectangles in a QWidget using QPainter
- Label each rectangle with monitor name and resolution
- Click to select; selected monitor gets highlighted border
- Updates on monitor hotplug (periodic refresh or signal-based)

## Drag-Drop Logo Picker Widget

Three drop zones for left, right, and bottom logos.

**Implementation:**
- QLabel with border styling as drop zone
- Accept file drops (QDragEnterEvent, QDropEvent) for image files
- Show thumbnail preview of dropped image
- Click to open QFileDialog for browsing
- Clear button (x) to remove logo
- Logo library in Settings provides a grid of thumbnails that can be dragged

## Testing

- Unit tests for widget logic (config binding, data flow) — no GUI rendering in tests
- Integration tests for config round-trip (set values in GUI model, verify config.json)
- Manual testing for visual correctness and interaction
- Test matrix: single monitor, multi-monitor, 0/1/2+ webcams, with/without audio device

## Build & Packaging

- miqt requires Qt6 development libraries at build time
- Add Qt6 dependencies to flake.nix dev shell
- Docker-based builds via miqt-docker for cross-platform packaging
- Update nfpm config for deb/rpm to depend on Qt6 runtime libraries
- AppImage packaging with bundled Qt6 libs
