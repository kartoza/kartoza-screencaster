# Kartoza Screencaster Specification

This document provides a complete specification of the Kartoza Screencaster application, including user stories, business rules, and functional requirements.

## Overview

Kartoza Screencaster is a screen recording and video processing application for Linux (Wayland) that allows users to create professional-quality video recordings with webcam overlays, audio, and post-processing features.

## User Stories

### US-001: Start a Recording
**As a** content creator
**I want to** start a screen recording by clicking on the systray icon
**So that** I can quickly capture my screen without opening the full TUI

**Acceptance Criteria:**
- Single-clicking the systray icon when idle starts a 5-second countdown
- Countdown shows visual feedback (numbered icons 5→4→3→2→1)
- Audio beeps play during countdown
- Recording starts automatically after countdown completes
- Clicking during countdown cancels it

### US-002: Pause a Recording
**As a** content creator
**I want to** pause my recording by single-clicking the systray icon
**So that** I can take breaks without creating multiple video files

**Acceptance Criteria:**
- Single-clicking the systray icon while recording pauses the recording
- Single-clicking while paused resumes the recording
- The systray icon changes to indicate paused state
- The right-click context menu shows "Resume" when paused

### US-003: Stop a Recording
**As a** content creator
**I want to** stop my recording by double-clicking the systray icon
**So that** I can finalize the recording and move to processing

**Acceptance Criteria:**
- Double-clicking the systray icon while recording or paused stops the recording
- Right-clicking and selecting "Stop Recording" also works
- After stopping, the room noise capture phase begins (if audio was enabled)

### US-004: Room Noise Calibration
**As a** content creator
**I want the** application to capture room noise after I stop recording
**So that** the audio processing can remove background noise effectively

**Acceptance Criteria:**
- After recording stops (if audio was enabled), the app enters room noise capture mode
- The systray icon changes to indicate room noise capture (spinning in reverse at 2x speed)
- A tooltip displays "Recording room noise - Please keep quiet!"
- User clicks on the systray are rejected during this phase
- The room noise capture lasts 30 seconds
- After completion, the TUI opens for metadata entry and processing

### US-005: Set Recording Metadata
**As a** content creator
**I want to** set a title and description for my recording
**So that** the output files and folder are named appropriately

**Acceptance Criteria:**
- After stopping a recording, the TUI opens to the history/edit screen
- User can enter a title, description, presenter name, and topic
- The recording folder is renamed to `NNN-recording_name` format when saved
- The metadata is stored in `recording.json` within the folder

### US-006: Process Recording with Options
**As a** content creator
**I want to** choose processing options (vertical video, logos, etc.)
**So that** I can customize the output videos

**Acceptance Criteria:**
- User can enable/disable: vertical video, logos (left, right, bottom)
- User can select logo files from the configured logo directory
- User can choose title color for lower-third overlay
- Processing creates standard and vertical versions as configured

### US-006a: Text Box Overlays
**As a** content creator
**I want to** add any number of text boxes with my choice of font, weight and colour
**So that** I can caption and brand my recording, seeing the exact result in the preview

**Acceptance Criteria:**
- User can add multiple independent text boxes to the canvas (Add ▸ Text Box)
- Each text box has its own text content, font family, weight (Light/Normal/Bold) and colour
- Font size is derived from the text box height (resize to scale); there is no fixed size cap
- Text boxes can be moved, resized (aspect-locked) and cropped like other canvas items
- Each text box is burned into the final landscape and vertical outputs at its position with its font, weight and colour (WYSIWYG)
- Text box properties persist across restarts and within presets
- Legacy recordings carrying only a single title still render that title unchanged

### US-006b: Canvas Item Manipulation
**As a** content creator
**I want** intuitive resize, crop and alignment of canvas items
**So that** composing a layout is fast and predictable

**Acceptance Criteria:**
- Dragging a selected item's corner handles scales it proportionally (aspect-locked), matching the scroll wheel
- Holding **Alt** turns the handles Kartoza blue and switches them to crop mode; dragging an **edge** crops one side and dragging a **corner** crops two sides at once; releasing Alt returns to resize
- Cropping a webcam trims the region that is recorded (the crop is applied to the webcam source in the output), so the recording matches the preview
- Dragging an item snaps it to the scene-frame edges, the frame's half-width/half-height centre lines, and other objects' edges and centres, with a guide line shown while a snap is engaged
- Holding **Shift** disables snapping for free placement
- Items and additional monitors are constrained to the recording frame so the entire recorded area stays visible and nothing is dragged off it

### US-006c: Multiple Monitors on One Canvas
**As a** content creator with more than one display
**I want** to add several monitors to a single layout
**So that** I can compose a multi-screen scene

**Acceptance Criteria:**
- Add ▸ Screen can add more than one monitor; the first is the primary background and each additional monitor is a movable/resizable/croppable inset
- Each screen layer previews live and independently (grim per output on wlroots, `QScreen` per display on X11)
- The recording captures each monitor separately and composites the additional ones as insets over the primary in the landscape output, at their canvas placement and crop (additional captures use `wl-screenrec` on wlroots / `ffmpeg x11grab` on X11)
- On GNOME/KDE Wayland (portal capture) only the primary monitor is captured; the vertical/split output uses the primary screen
- Re-selecting a monitor already on the canvas does not create a duplicate layer

### US-006d: Per-Element Preview Pause
**As a** content creator
**I want** to pause the live preview of a single element
**So that** I can freeze one feed without affecting the others

**Acceptance Criteria:**
- Hovering a monitor or webcam reveals a pause/continue button centred on that element
- Clicking the button freezes only that element's live preview; other elements keep updating
- A paused element keeps its button visible so the frozen state is evident
- Pausing the preview does not affect the recording

### US-006e: Multiple Webcams
**As a** content creator with more than one camera
**I want** to add several webcams to the scene
**So that** I can show multiple angles at once

**Acceptance Criteria:**
- More than one webcam can be added; the recording captures each webcam separately and composites them all at their canvas placement, shape and crop
- The first webcam uses the existing single-webcam path; additional cameras are captured to their own files and composited as overlays
- An additional camera that fails to open drops its overlay without failing the recording

### US-006f: Overlay Image Formats
**As a** content creator
**I want** logo/overlay images to always appear in the recording
**So that** my branding is not silently dropped

**Acceptance Criteria:**
- The logo chooser and asset gallery offer raster formats and GIF, not SVG (FFmpeg cannot decode SVG)
- An SVG that reaches a recording via drag-drop, a saved preset or reprocessing is rasterised to PNG before the merge
- A merge that produces no output is reported as **failed** (not "completed")

### US-007: Output File Naming
**As a** content creator
**I want my** output files to be named with a sequence number and title
**So that** I can easily organize and identify my recordings

**Acceptance Criteria:**
- Folder format: `NNN-recording_name` (e.g., `005-my_tutorial`)
- Main video: `NNN-recording_name.mp4` (e.g., `005-my_tutorial.mp4`)
- Vertical video: `NNN-recording_name_vertical.mp4` (e.g., `005-my_tutorial_vertical.mp4`)
- NNN is a zero-padded 3-digit sequence number
- Recording name uses underscores, is lowercase, special characters removed

## Business Rules

### BR-001: Systray Click Behavior
| State | Single Click | Double Click |
|-------|--------------|--------------|
| Idle | Start countdown | (ignored) |
| Countdown | Cancel countdown | Cancel countdown |
| Recording | Pause | Stop |
| Paused | Resume | Stop |
| Room Noise | (blocked) | (blocked) |
| Processing | (ignored) | (ignored) |

### BR-002: Recording States
The recording can be in the following states:
- `recording` - Actively recording screen/audio/webcam
- `paused` - Recording is paused, can be resumed
- `needs_metadata` - Recording stopped via systray, awaiting title/description
- `processing` - Video processing is in progress
- `completed` - Processing finished successfully
- `failed` - Processing encountered an error

### BR-003: File Organization
- All recordings are stored in `~/Videos/Screencasts/` by default, or in the
  configured output directory. The recorder, the history view and the
  recording-number allocator all resolve this through a single
  `Config::recordingsDir()` so they cannot disagree
- Each recording has its own folder
- Temporary folder name during recording: `recording-YYYYMMDD-HHMMSS`
- Final folder name after metadata entry: `NNN-recording_name`
- All source files (video parts, audio parts, webcam parts) are kept in the folder

### BR-004: Pause/Resume Support
- When paused, current recording segments are finalized
- When resumed, new segments are created with incrementing part numbers
- Part files are named: `screen_part000.mp4`, `screen_part001.mp4`, etc.
- During processing, all parts are concatenated losslessly

### BR-005: Room Noise Duration
- Room noise is captured for 30 seconds after recording stops
- Room noise is only captured if audio recording was enabled
- The room noise file (`room_noise.wav`) is used for audio calibration during processing

## Functional Requirements

### FR-001: Systray Integration
- Application runs as a systray icon
- Icon states: Idle (green), Recording (red), Paused (yellow), Processing (spinning), Room Noise (spinning reverse 2x)
- Tooltip shows current status and elapsed time when recording
- Context menu provides: Start/Stop Recording, Pause/Resume, Status, Open TUI, Quit

### FR-002: Recording Capabilities
- Screen recording via one of three backends, selected at runtime by
  `Platform::supportsWlrCapture()`:
  - **wlroots** (Hyprland, Sway, Wayfire, …): `wl-screenrec`, falling back once
    to `wf-recorder` (software libx264) if it exits early
  - **GNOME, KDE, COSMIC**: `xdg-desktop-portal` ScreenCast + PipeWire +
    `gst-launch-1.0`. These compositors do not implement
    `wlr-screencopy-unstable-v1`, so the wlroots tools cannot capture there.
    The portal picks a single source, so additional-monitor insets are dropped
  - **X11**: `ffmpeg x11grab`
- A recording that was asked to capture the screen but produced no usable video
  is reported as `failed`, never `completed`
- Audio recording via FFmpeg with PulseAudio/PipeWire
- Webcam recording via FFmpeg with v4l2
- Multi-monitor support with automatic detection
- Support for any monitor resolution

### FR-003: Video Processing Pipeline
1. **Stop recorders** - Finalize all recording processes
2. **Analyze audio** - Detect audio levels and characteristics
3. **Normalize audio** - Apply loudness normalization
4. **Merge video & audio** - Combine screen, audio, and webcam (with overlay)
5. **Create vertical** - Generate 9:16 vertical video with layouts

### FR-004: Vertical Video Options
- Standard layout: Screen video with webcam circle overlay (bottom-right)
- Left-split layout: Left half of screen fills frame, with webcam overlay options:
  - **Webcam Bubble (default)**: Circular webcam overlay in the bottom-right corner
  - **Webcam Rectangle**: Full-width rectangular webcam positioned at the top of the lower third, beneath the screen recording, leaving space at the bottom for title/banner
- Logo overlays: Product logos (top corners), company logo with title (lower third)
- Configurable background color for letterboxing

### FR-005: TUI Screens
- Main Menu - Access all features
- Recording Setup - Configure new recording (monitor, audio, webcam, etc.)
- Recording Status - Show elapsed time, pause/stop buttons
- Processing Progress - Step-by-step progress with elapsed times
- History - Browse, edit, reprocess, delete past recordings
- Options - Configure presets, output directory, logos, YouTube, etc.
- YouTube Setup - OAuth flow for YouTube API
- YouTube Upload - Upload processed videos to YouTube

### FR-006: Presets
- First-run detection - Open presets config if not configured
- Recording presets: Record screen, audio, webcam, vertical video, left split, webcam bubble, logos
- Presets are saved and used for subsequent systray-initiated recordings

### FR-007: YouTube Integration
- OAuth 2.0 authentication with YouTube Data API v3
- Upload videos with title, description, privacy settings
- Create and manage playlists
- Track upload status in recording metadata

## Technical Requirements

### TR-001: Platform Support
- Linux only (Wayland compositor required)
- Tested compositors: Hyprland, GNOME, KDE, COSMIC
- Dependencies: FFmpeg, PulseAudio/PipeWire, plus either `wl-screenrec` +
  `wf-recorder` (wlroots) or a matching `xdg-desktop-portal` backend and
  GStreamer with `pipewiresrc` and an H.264 encoder (GNOME/KDE/COSMIC)

### TR-002: Configuration Storage
- Config file: `~/.config/kartoza-screencaster/config.json`
- Recording metadata: `{folder}/recording.json`
- State files: `/tmp/kartoza-screencaster/`

### TR-003: Logging
- Log file: `~/.config/kartoza-screencaster/app.log`
- FFmpeg commands logged for debugging
- Error details stored in recording.json for failed recordings

## Appendix: File Formats

### recording.json Schema
```json
{
  "status": "completed",
  "metadata": {
    "title": "Recording Title",
    "description": "Description text",
    "presenter": "Name",
    "topic": "Topic",
    "number": 5,
    "folder_name": "005-recording_title"
  },
  "start_time": "2024-01-15T10:00:00Z",
  "end_time": "2024-01-15T10:30:00Z",
  "duration": 1800000000000,
  "environment": {
    "os": "linux",
    "monitor": "DP-1",
    "monitor_resolution": "2560x1440"
  },
  "files": {
    "folder_path": "/home/user/Videos/Screencasts/005-recording_title",
    "video_parts": ["screen_part000.mp4"],
    "audio_parts": ["audio_part000.wav"],
    "webcam_parts": ["webcam_part000.mp4"],
    "merged_file": "005-recording_title.mp4",
    "vertical_file": "005-recording_title_vertical.mp4"
  },
  "settings": {
    "screen_enabled": true,
    "audio_enabled": true,
    "webcam_enabled": true,
    "vertical_enabled": true,
    "left_split_enabled": true,
    "webcam_bubble_enabled": true,
    "logos_enabled": true,
    "logos": [
      {
        "path": "/path/to/logo.png",
        "gif_loop": 2,
        "gif_loop_max": 3,
        "rel_x": 0.01,
        "rel_y": 0.01,
        "rel_w": 0.15,
        "rel_h": 0.15
      }
    ]
  },
  "processing": {
    "processed_at": "2024-01-15T10:35:00Z",
    "normalize_applied": true,
    "vertical_created": true
  }
}
```

### config.json Schema
```json
{
  "output_dir": "~/Videos/Screencasts",
  "logo_directory": "~/Pictures/logos",
  "next_recording_number": 6,
  "presets_configured": true,
  "recording_presets": {
    "record_screen": true,
    "record_audio": true,
    "record_webcam": true,
    "vertical_video": true,
    "left_split": true,
    "webcam_bubble": true,
    "add_logos": true
  },
  "youtube": {
    "access_token": "...",
    "refresh_token": "...",
    "auto_prompt_upload": true
  }
}
```
