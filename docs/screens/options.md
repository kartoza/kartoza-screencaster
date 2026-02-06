# Options

The Options screen allows you to configure application-wide settings including media folder (output directory), topics, default presenter, logo directory, YouTube integration, and syndication.

## Screen Preview

<div class="terminal-mockup">
<div class="terminal-header">
<div class="terminal-buttons">
<div class="terminal-button red"></div>
<div class="terminal-button yellow"></div>
<div class="terminal-button green"></div>
</div>
<div class="terminal-title">Options</div>
</div>
<div class="terminal-content"><span class="t-header">━━━━━━━━━━━━━━━━━━ Options ━━━━━━━━━━━━━━━━━━</span>

<span class="t-header">Media Folder</span>
<span class="t-blue">Save to:</span>           <span class="t-cyan">/home/user/Videos/Screencasts</span>
                    <span class="t-gray">press enter to browse, c to reset</span>

<span class="t-header">Topics</span>
<span class="t-selected"><span class="t-orange">→</span> <span class="t-white">QGIS sketches</span></span>
  <span class="t-blue">GIS development tutorials</span>
  <span class="t-blue">Open source workflows</span>
  <span class="t-blue">General tutorials</span>

<span class="t-gray">New topic:</span> <span class="t-white">█</span>
<span class="t-gray">[ Add ] [ Remove Selected ]</span>

<span class="t-header">Presenter</span>
<span class="t-blue">Default:</span>           <span class="t-white">Tim Sketcher</span>

<span class="t-header">Logos</span>
<span class="t-blue">Directory:</span>         <span class="t-cyan">/home/user/Pictures/logos</span>
                    <span class="t-gray">logos selected per-recording</span>

<span class="t-header">YouTube</span>
<span class="t-blue">Status:</span>            <span class="t-green">Connected</span>

<span class="t-header">Syndication</span>
<span class="t-blue">Accounts:</span>          <span class="t-green">2 enabled of 3</span>

  <span class="t-green">[ Save ]</span>

<span class="t-gray">━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━</span>
<span class="t-gray">tab/↓: next • shift+tab/↑: prev • enter: select • esc: back</span>
</div>
</div>

## Configuration Sections

### Media Folder

<span class="t-blue">**Save to:**</span> *Path / File Browser*

Specifies the root directory where all recordings will be saved. Each recording creates a subfolder within this directory.

**Default Location:**

```
~/Videos/Screencasts
```

**Changing the Media Folder:**

1. Navigate to the "Save to" field
2. Press ++enter++ to open the directory browser
3. Navigate to your desired folder
4. Press ++s++ to select the current directory
5. Save your settings

**Reset to Default:**

Press ++c++ while on this field to reset to the default location.

---

### Topics Management

Topics are categories for organizing your recordings. Each topic creates a separate folder in your output directory.

#### Topic List

<span class="t-header">**Topics**</span>

Displays all configured topics. Use ++up++ / ++down++ to navigate.

**Default Topics:**

- QGIS sketches
- GIS development tutorials
- Open source workflows
- General tutorials

---

#### Add Topic

<span class="t-gray">**New topic:**</span> *Text Input*

Enter a new topic name and press ++enter++ or select **[ Add ]** to add it.

**Rules:**

- Maximum 50 characters
- Must be unique
- Becomes a folder name (avoid special characters)

---

#### Remove Topic

<span class="t-gray">**[ Remove Selected ]**</span>

Removes the currently selected topic from the list.

!!! warning "Recordings Not Deleted"
    Removing a topic only removes it from the configuration. Existing recordings in that topic's folder are preserved.

---

### Default Presenter

<span class="t-blue">**Default Presenter:**</span> *Text Input*

Sets the default presenter name used in recording metadata and YouTube uploads.

**Used in:**

- Video metadata
- YouTube video description
- Recording history display

---

### Logo Directory

<span class="t-blue">**Logo Directory:**</span> *Path / File Browser*

Specifies the folder containing logo images for video overlays.

**Selecting a Directory:**

1. Press ++enter++ on **[ Browse... ]**
2. Navigate using the file browser
3. Select the directory containing your logos
4. Press ++enter++ to confirm

#### File Browser

<div class="terminal-mockup">
<div class="terminal-header">
<div class="terminal-buttons">
<div class="terminal-button red"></div>
<div class="terminal-button yellow"></div>
<div class="terminal-button green"></div>
</div>
<div class="terminal-title">Select Logo Directory</div>
</div>
<div class="terminal-content"><span class="t-header">Select Directory</span>

<span class="t-gray">Path:</span> <span class="t-cyan">/home/user/Pictures/logos</span>

<span class="t-blue">📁</span> <span class="t-gray">..</span>
<span class="t-selected"><span class="t-orange">→</span> <span class="t-blue">📁</span> <span class="t-white">logos</span></span>
<span class="t-blue">📁</span> <span class="t-gray">screenshots</span>
<span class="t-blue">📁</span> <span class="t-gray">wallpapers</span>
<span class="t-gray">📄</span> <span class="t-gray">profile.png</span>

<span class="t-gray">━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━</span>
<span class="t-gray">↑/↓: navigate • enter: select/open • esc: cancel</span>
</div>
</div>

**Supported Logo Formats:**

| Format | Extension | Notes |
|--------|-----------|-------|
| PNG | `.png` | Recommended for static logos |
| GIF | `.gif` | Animated logos supported |
| SVG | `.svg` | Vector graphics |
| JPEG | `.jpg`, `.jpeg` | Not recommended (no transparency) |

---

### Background Color

<span class="t-blue">**Background:**</span> *Color Selector*

Sets the background color for the lower third branding area in vertical videos. Use ++left++ / ++right++ or ++enter++ / ++space++ to cycle through available colors.

**Available Colors:**

`white` (default) · `black` · `#62A4C7` · `yellow` · `orange` · `red` · `green` · `blue` · `cyan` · `magenta`

This color is saved per-recording so that reprocessing uses the same setting.

---

### YouTube Integration

<span class="t-blue">**YouTube:**</span> *Status / Configuration*

Shows YouTube connection status and provides access to setup.

**Status Indicators:**

| Status | Meaning |
|--------|---------|
| <span class="t-green">● Connected</span> | YouTube API configured and authenticated |
| <span class="t-yellow">● Not configured</span> | API credentials not set |
| <span class="t-red">● Auth expired</span> | Need to re-authenticate |

Press ++enter++ on **[ Configure YouTube ]** to open the [YouTube Setup](youtube-setup.md) screen.

---

### Recording Presets

<span class="t-header">**Recording Presets**</span>

Configures the default recording settings used by systray quick-record. These toggles control which recording sources and features are enabled when starting a recording from the system tray icon.

| Toggle | Description |
|--------|-------------|
| **Audio** | Record microphone audio |
| **Webcam** | Record webcam for picture-in-picture |
| **Screen** | Record monitor screen capture |
| **Vertical** | Create vertical (9:16) version (requires webcam or screen) |
| **Left Split** | Use left half of screen for vertical video (requires vertical) |
| **Logos** | Add logo overlays to the recording |

Each toggle shows **Yes** (green/orange when focused) or **No** (gray/orange when focused). Press ++enter++ or ++space++ to toggle.

!!! note "Vertical Video Constraint"
    The "Vertical" toggle is disabled (greyed out) when both Webcam and Screen are turned off, since vertical video requires at least one video source.

!!! note "Left Split Constraint"
    The "Left Split" toggle is disabled (greyed out) when Vertical Video is turned off.

!!! info "Systray First-Run"
    The first time you attempt to start a recording from the system tray, the application opens directly to this section so you can configure your preferred defaults before recording. After saving, the TUI automatically closes and subsequent systray recordings use your saved presets.

---

### Save Button

<span class="t-green">**[ Save ]**</span>

Saves all configuration changes to disk, including recording presets.

**Configuration File Location:**

```
~/.config/kartoza-screencaster/config.json
```

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| ++tab++ / ++down++ | Next field |
| ++shift+tab++ / ++up++ | Previous field |
| ++j++ / ++k++ | Navigate topic list |
| ++enter++ / ++space++ | Select / Confirm / Toggle |
| ++c++ | Clear/reset directory (on media folder or logo directory) |
| ++d++ / ++delete++ / ++backspace++ | Remove selected topic |
| ++esc++ | Cancel / Back |

## Field Navigation Order

1. Media folder (output directory)
2. Topic list
3. Add topic input
4. Remove button
5. Default presenter input
6. Logo directory browse
7. Background color selector
8. YouTube setup
9. Syndication setup
10. Preset: Record Audio
11. Preset: Record Webcam
12. Preset: Record Screen
13. Preset: Vertical Video
14. Preset: Left Split
15. Preset: Add Logos
16. Save button

## Configuration File

Settings are stored in JSON format:

```json
{
  "output_dir": "/home/user/Videos/Screencasts",
  "topics": [
    {"id": "qgis-sketches", "name": "QGIS sketches"},
    {"id": "gis-dev", "name": "GIS development tutorials"},
    {"id": "open-source", "name": "Open source workflows"},
    {"id": "general", "name": "General tutorials"}
  ],
  "default_presenter": "Tim Sketcher",
  "logo_directory": "/home/user/Pictures/logos",
  "bg_color": "white",
  "recording_presets": {
    "record_audio": true,
    "record_webcam": true,
    "record_screen": true,
    "vertical_video": true,
    "left_split": true,
    "add_logos": true
  },
  "presets_configured": true,
  "youtube": {
    "client_id": "...",
    "client_secret": "..."
  },
  "syndication": {
    "accounts": []
  }
}
```

## Workflow Position

This screen is accessed from:

- **[Main Menu](main-menu.md)** → Select "Options"
- **System tray** → First recording attempt (opens directly to Recording Presets)
- **CLI** → `kartoza-screencaster --presets` (opens directly to Recording Presets)

From here you can navigate to:

- **[YouTube Setup](youtube-setup.md)** → Configure YouTube API
- **[Main Menu](main-menu.md)** → Save and return

## Related Pages

- **[Main Menu](main-menu.md)** - Return to navigation
- **[YouTube Setup](youtube-setup.md)** - Configure YouTube credentials
- **[Recording Setup](recording-setup.md)** - Uses topics and logos
