# Recording Setup

The Recording Setup screen is where you configure all aspects of your recording before going live. This is the most feature-rich screen in the application.

## Screen Preview

<div class="terminal-mockup">
<div class="terminal-header">
<div class="terminal-buttons">
<div class="terminal-button red"></div>
<div class="terminal-button yellow"></div>
<div class="terminal-button green"></div>
</div>
<div class="terminal-title">New Recording</div>
</div>
<div class="terminal-content"><span class="t-header">━━━━━━━━━━━━━━━━ New Recording ━━━━━━━━━━━━━━━━</span>
<span class="t-gray">╭──────────────────────────────────────────────────────────────────╮</span>
<span class="t-gray">│</span>
<span class="t-gray">│</span>   <span class="t-orange">Title:</span>  <span class="t-gray">╭────────────────────────────────────────────╮</span>
<span class="t-gray">│</span>           <span class="t-gray">│</span> <span class="t-white">Introduction to QGIS</span>              <span class="t-gray">│</span>
<span class="t-gray">│</span>           <span class="t-gray">╰────────────────────────────────────────────╯</span>
<span class="t-gray">│</span>
<span class="t-gray">│</span>  <span class="t-gray">Number:</span>  <span class="t-gray">╭──────────────────╮</span>
<span class="t-gray">│</span>           <span class="t-gray">│</span> <span class="t-white">042</span>              <span class="t-gray">│</span>
<span class="t-gray">│</span>           <span class="t-gray">╰──────────────────╯</span>
<span class="t-gray">│</span>
<span class="t-gray">│</span>   <span class="t-gray">Topic:</span>   <span class="t-orange-bg"> QGIS </span>  <span class="t-gray"> GIS development </span>  <span class="t-gray"> General </span>
<span class="t-gray">│</span>
<span class="t-gray">│</span>  <span class="t-gray">──────────────────────────────────────────────────────────────</span>
<span class="t-gray">│</span>
<span class="t-gray">│</span>  <span class="t-gray">Record Audio:</span>     <span class="t-green">Yes</span>  <span class="t-gray">No</span>
<span class="t-gray">│</span>  <span class="t-gray">Record Webcam:</span>    <span class="t-gray">Yes</span>  <span class="t-red">No</span>
<span class="t-gray">│</span>  <span class="t-gray">Record Screen:</span>    <span class="t-green">Yes</span>  <span class="t-gray">No</span>
<span class="t-gray">│</span>  <span class="t-gray">Monitor:</span>          ◀ <span class="t-white">DP-1 (2560x1440)</span> ▶
<span class="t-gray">│</span>
<span class="t-gray">│</span>  <span class="t-gray">──────────────────────────────────────────────────────────────</span>
<span class="t-gray">│</span>
<span class="t-gray">│</span>  <span class="t-gray">Vertical Video:</span>   <span class="t-gray">Yes</span>  <span class="t-red">No</span>
<span class="t-gray">│</span>  <span class="t-gray">Add Logos:</span>        <span class="t-green">Yes</span>  <span class="t-gray">No</span>
<span class="t-gray">│</span>
<span class="t-gray">│</span>  <span class="t-gray">──────────────────────────────────────────────────────────────</span>
<span class="t-gray">│</span>
<span class="t-gray">│</span>  <span class="t-gray">Description:</span>  <span class="t-gray">╭────────────────────────────────────────────╮</span>
<span class="t-gray">│</span>                <span class="t-gray">│</span> <span class="t-white">Learn how to get started with QGIS...</span> <span class="t-gray">│</span>
<span class="t-gray">│</span>                <span class="t-gray">│</span>                                        <span class="t-gray">│</span>
<span class="t-gray">│</span>                <span class="t-gray">╰────────────────────────────────────────────╯</span>
<span class="t-gray">│</span>  <span class="t-orange">⚠ "color" → consider "colour" (UK spelling)</span>
<span class="t-gray">│</span>
<span class="t-gray">│</span>                 <span class="t-orange-bg"> Go Live! </span>    <span class="t-gray">Cancel</span>
<span class="t-gray">│</span>
<span class="t-gray">╰──────────────────────────────────────────────────────────────────╯</span>
<span class="t-gray">━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━</span>
<span class="t-gray">Tab/↓: Next • Shift+Tab/↑: Prev • ←/→: Toggle/Select • Enter: Go Live</span>
</div>
</div>

## Field Reference

### Basic Information

#### Title

<span class="t-orange">**Title**</span> - *Text Input*

The title of your recording. This becomes:

- The video title on YouTube
- Part of the output folder name
- Displayed in the recording history

**Spell Check:** The title is automatically checked for UK English spelling and grammar issues as you type. Warnings appear below the input field with ⚠ indicators.

!!! tip "Best Practices"
    Use descriptive titles that explain what the video covers. Avoid special characters that may cause filesystem issues.

---

#### Episode Number

<span class="t-blue">**Episode #**</span> - *Number Input*

The episode number for this recording series. Auto-incremented from the last recording in the same topic.

**Behavior:**

- Automatically increments when you start a new recording
- Can be manually overridden
- Resets to 1 for new topics

---

#### Topic

<span class="t-blue">**Topic**</span> - *Selection*

The category/topic for this recording. Topics help organize your recordings and can be managed in [Options](options.md).

**Default Topics:**

- QGIS sketches
- GIS development tutorials
- Open source workflows
- General tutorials

Use ++left++ / ++right++ to cycle through available topics.

---

### Recording Options

These toggles control what gets captured during recording.

#### Record Audio

<span class="t-green">[✓]</span> **Record Audio**

Captures audio from your default microphone.

| Setting | Result |
|---------|--------|
| Enabled | Audio captured and merged into final video |
| Disabled | Silent video output |

!!! note "Audio Normalization"
    Audio is automatically normalized during post-processing to ensure consistent volume levels.

---

#### Record Webcam

<span class="t-gray">[○]</span> **Record Webcam**

Captures your webcam feed as a picture-in-picture overlay.

| Setting | Result |
|---------|--------|
| Enabled | Webcam overlay in bottom-right corner |
| Disabled | Screen-only recording |

!!! warning "Performance Impact"
    Enabling webcam increases CPU usage. Ensure your system can handle simultaneous capture.

---

#### Record Screen

<span class="t-green">[✓]</span> **Record Screen**

Captures the selected monitor.

| Setting | Result |
|---------|--------|
| Enabled | Screen is captured |
| Disabled | Audio-only recording (rare use case) |

---

#### Vertical Video

<span class="t-gray">[○]</span> **Vertical Video**

Generates an additional vertical (9:16) version of your recording, optimized for YouTube Shorts.

| Setting | Result |
|---------|--------|
| Enabled | Creates `-vertical.mp4` in output |
| Disabled | Only horizontal video created |

**Webcam Support:**

- **With webcam**: Creates a stacked layout (screen + webcam + branding) or left-split layout with circular webcam overlay
- **Without webcam**: Creates a screen-only vertical video with the screen filling the top portion and branding at the bottom (or left-split mode fills the entire frame)

!!! tip "Social Media"
    Enable this for YouTube Shorts, TikTok, Instagram Reels, or other vertical video platforms. Works with screen-only recordings too!

---

#### Left Split

<span class="t-green">[✓]</span> **Left Split**

*Only visible when "Vertical Video" is enabled*

When enabled, the vertical video is rendered by cropping and scaling the left half of your screen recording to fill the entire 9:16 frame. This is ideal for presentations where your content is on the left side of the screen.

| Setting | Result |
|---------|--------|
| Enabled (default) | Left half of screen fills the vertical frame |
| Disabled | Traditional layout with screen on top, webcam in middle, branding at bottom |

**Left Split Layout (with webcam):**

```
┌─────────────────┐
│ [Left]  [Right] │  ← Logos in top corners
│                 │
│  Left half of   │
│  screen video   │
│  (fills 1080px  │
│   width edge    │
│   to edge)      │
│           [cam] │  ← Circular webcam bottom-right
├─────────────────┤
│    [Banner]     │  ← Branding area (if video
│                 │     doesn't fill full height)
└─────────────────┘
```

**Left Split Layout (screen only, no webcam):**

```
┌─────────────────┐
│ [Left]  [Right] │  ← Logos in top corners
│                 │
│  Left half of   │
│  screen video   │
│  (fills 1080px  │
│   width edge    │
│   to edge)      │
├─────────────────┤
│    [Banner]     │  ← Branding area (if video
│                 │     doesn't fill full height)
└─────────────────┘
```

The left half of your screen is scaled to fill the full 1080px width corner-to-corner. If the aspect ratio means the scaled video doesn't fill the full 1920px height, the remaining space is filled with the configured background color.

**Traditional Layout (when disabled, with webcam):**

```
┌─────────────────┐
│   Screen video  │
│   (top third)   │
├─────────────────┤
│  Webcam video   │
│ (middle third)  │
├─────────────────┤
│ [Left] [Right]  │
│    [Banner]     │  ← Branding area
│     Title       │
└─────────────────┘
```

**Traditional Layout (when disabled, screen only):**

```
┌─────────────────┐
│                 │
│   Screen video  │
│  (top 2/3rds)   │
│                 │
├─────────────────┤
│ [Left] [Right]  │
│    [Banner]     │  ← Branding area
│     Title       │
└─────────────────┘
```

---

#### Add Logo Overlays

<span class="t-green">[✓]</span> **Add Logo Overlays**

Enables professional branding overlays on your video.

When enabled, additional configuration options appear:

- Left logo position
- Right logo position
- Bottom logo position
- Title text color
- GIF animation mode

---

### Logo Configuration

*Only visible when "Add Logo Overlays" is enabled*

#### Logo Positions

<span class="t-blue">**Left Logo**</span> / <span class="t-blue">**Right Logo**</span> / <span class="t-blue">**Bottom Logo**</span>

Select logo files from your configured logo directory. Use ++left++ / ++right++ to cycle through available logos.

**Supported Formats:**

- PNG (static images)
- GIF (animated logos)
- SVG (vector graphics)

**Position Reference:**

```
┌────────────────────────────────────┐
│ [Left]                    [Right]  │
│                                    │
│                                    │
│                                    │
│             [Bottom]               │
└────────────────────────────────────┘
```

---

#### Title Color

<span class="t-blue">**Title Color**</span> - *Selection*

The color used for the title text overlay.

**Available Colors:**

| Color | Hex Code |
|-------|----------|
| White | `#FFFFFF` |
| Orange | `#FF9500` |
| Blue | `#0066CC` |
| Green | `#00CC66` |
| Red | `#FF4444` |
| Yellow | `#FFCC00` |

---

#### GIF Loop Mode

<span class="t-blue">**GIF Mode**</span> - *Selection*

How animated GIF logos are displayed.

| Mode | Behavior |
|------|----------|
| **Continuous** | Loop GIF throughout the video |
| **Once at Start** | Play GIF once, then show last frame |
| **Once at End** | Show first frame, play GIF at video end |

---

### Monitor Selection

<span class="t-header">**Monitor Selection**</span>

Choose which monitor to record. All connected monitors are listed with their resolution.

**Information Displayed:**

- Monitor name (e.g., `DP-1`, `HDMI-1`)
- Resolution (e.g., `2560×1440`)
- Primary indicator

Use ++up++ / ++down++ to select, or navigate with ++tab++.

---

### Description

<span class="t-header">**Description**</span> - *Text Area*

A multi-line description for your recording. This becomes the video description on YouTube.

**Spell Check:** The description is automatically checked for UK English spelling and grammar issues as you type:

- **UK English**: US spellings are flagged with UK alternatives (e.g., "color" → "colour")
- **Grammar**: Common grammar issues are detected (e.g., "a" vs "an", double spaces)
- **GIS Terms**: Common GIS and QGIS terminology is recognized and not flagged

Warnings appear as ⚠ messages below the field. Up to 3 issues are shown at a time.

**Tips:**

- Include relevant keywords
- Add timestamps if applicable
- Credit any resources used

---

### Action Buttons

#### Go Live

<span class="t-green">[ Go Live ]</span>

Starts the recording. Navigates to the [Countdown](countdown.md) screen.

**Requirements:**

- At least one recording option must be enabled
- A monitor must be selected (if screen recording enabled)

---

#### Cancel

<span class="t-gray">[ Cancel ]</span>

Returns to the [Main Menu](main-menu.md) without starting a recording.

---

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| ++tab++ | Next field |
| ++shift+tab++ | Previous field |
| ++space++ / ++enter++ | Toggle option / Select |
| ++left++ / ++right++ | Change selection (topics, logos, colors) |
| ++up++ / ++down++ | Navigate options or monitors |
| ++esc++ | Cancel and return to menu |

## Workflow Position

<div class="workflow-step">
<div class="workflow-step-number">1</div>
<div>
<strong>Previous:</strong> <a href="main-menu.md">Main Menu</a> → Select "New Recording"
</div>
</div>

<div class="workflow-step">
<div class="workflow-step-number">2</div>
<div>
<strong>Current:</strong> Recording Setup (this screen)
</div>
</div>

<div class="workflow-step">
<div class="workflow-step-number">3</div>
<div>
<strong>Next:</strong> <a href="countdown.md">Countdown</a> → After pressing "Go Live"
</div>
</div>

## Related Pages

- **[Countdown](countdown.md)** - What happens after Go Live
- **[Recording](recording.md)** - Managing active recordings
- **[Options](options.md)** - Managing topics and logo directory
