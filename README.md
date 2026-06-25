# Kartoza Screencaster

[![Latest release](https://img.shields.io/github/v/release/kartoza/kartoza-screencaster?label=release&color=blue)](https://github.com/kartoza/kartoza-screencaster/releases/latest)
[![Release date](https://img.shields.io/github/release-date/kartoza/kartoza-screencaster?color=informational)](https://github.com/kartoza/kartoza-screencaster/releases/latest)
[![Docs](https://img.shields.io/badge/docs-kartoza.github.io-success)](https://kartoza.github.io/kartoza-screencaster/)
[![License: MIT](https://img.shields.io/github/license/kartoza/kartoza-screencaster)](LICENSE)

A screen recording tool for Wayland compositors with multi-monitor support, audio processing, and webcam integration.

## Features

- **Multi-monitor screen recording** with automatic cursor detection
- **Separate audio recording** with background noise reduction
- **Webcam recording** with real-time 60fps capture
- **Audio normalization** using EBU R128 loudness standards
- **Vertical video creation** with webcam overlay (perfect for social media)
- **Hardware acceleration** support (optional VAAPI encoding)
- **Desktop notifications** for recording status

## Requirements

- Wayland compositor (Hyprland, Sway, etc.)
- `wl-screenrec` - Wayland screen recorder
- `ffmpeg` - Video/audio processing
- `pw-record` (PipeWire) - Audio capture
- `notify-send` - Desktop notifications

## Installation

### From Source

```bash
# Clone the repository
git clone https://github.com/kartoza/kartoza-screencaster.git
cd kartoza-screencaster

# Build
make build

# Install
sudo make install
```

### Using Nix Flakes

```bash
# Run directly
nix run github:kartoza/kartoza-screencaster

# Install to profile
nix profile install github:kartoza/kartoza-screencaster
```

### Development

```bash
# Enter development shell
nix develop

# Or use direnv
direnv allow
```

## Usage

### TUI Mode

Launch the interactive terminal interface:

```bash
kartoza-screencaster
```

Press `Space` or `Enter` to toggle recording.

### System Tray Mode

Run as a background system tray applet for quick recording access:

```bash
kartoza-screencaster systray
```

**Controls:**
- **Left-click**: Toggle recording (start if idle, stop if recording)
- **Right-click**: Open menu with Pause/Resume, Open TUI, Quit options

**Workflow:**
1. Left-click the tray icon to start recording immediately (no setup needed)
2. The icon rotates while recording is active
3. Right-click and select "Pause" to pause, or left-click to stop
4. When you stop, the TUI opens automatically so you can add title and description
5. After saving metadata, the video is processed automatically

This mode is ideal for spontaneous recordings where you want to capture something quickly and add metadata afterwards.

**Autostart (Hyprland):**
```conf
exec-once = kartoza-screencaster systray
```

### Terminal Recording Mode

Record terminal sessions using asciinema (ideal for CLI tutorials or terminal-only environments):

```bash
kartoza-screencaster terminal
kartoza-screencaster terminal --title "My CLI Tutorial"
```

This will:
1. Start an asciinema recording of your terminal session
2. When you exit (Ctrl+D or `exit`), convert the recording to GIF and MP4

**Options:**
- `--title, -t`: Set the recording title
- `--idle-limit`: Maximum idle time in seconds (default: 5)
- `--font-size`: Font size for video rendering (default: 16)
- `--convert`: Convert an existing .cast file to video

**Dependencies:**
- `asciinema` - for recording terminal sessions
- `agg` - for converting cast files to GIF

### CLI Mode

```bash
# Toggle recording
kartoza-screencaster toggle

# Start recording
kartoza-screencaster start

# Start with options
kartoza-screencaster start --monitor DP-1 --no-webcam --hw-accel

# Stop recording
kartoza-screencaster stop

# Check status
kartoza-screencaster status

# List monitors
kartoza-screencaster monitors
```

### CLI Options

```
Flags:
  -m, --monitor string        Monitor name to record (default: monitor with cursor)
      --no-audio              Disable audio recording
      --no-webcam             Disable webcam recording
      --hw-accel              Enable hardware acceleration (VAAPI)
  -o, --output string         Output directory (default: ~/Videos/Screencasts)
      --webcam-device string  Webcam device (default: auto-detect)
      --webcam-fps int        Webcam framerate (default: 60)
      --audio-device string   PipeWire audio device (default: @DEFAULT_SOURCE@)
```

## Output Files

Recordings are saved to `~/Videos/Screencasts/` with the following files:

- `screenrecording-{monitor}-{timestamp}.mp4` - Raw screen capture
- `screenrecording-{monitor}-{timestamp}.wav` - Raw audio
- `screenrecording-{monitor}-{timestamp}-merged.mp4` - Final video with audio
- `screenrecording-{monitor}-{timestamp}-vertical.mp4` - Vertical video with webcam (if available)

## Audio Processing

The tool automatically processes audio with:

1. **Noise reduction** - Removes background noise using FFT-based denoising
2. **Highpass filter** - Removes low-frequency rumble (< 200 Hz)
3. **Two-pass loudness normalization** - EBU R128 compliant
   - Target loudness: -14 LUFS (louder than broadcast, perfect for screen recordings)
   - True peak: -1.5 dB (prevents clipping)
   - Loudness range: 11 LU (preserves dynamic range)

## Configuration

Configuration is stored in `~/.config/kartoza-screencaster/config.json`:

```json
{
  "output_dir": "/home/user/Videos/Screencasts",
  "default_options": {
    "no_audio": false,
    "no_webcam": false,
    "hw_accel": false,
    "webcam_fps": 60,
    "audio_device": "@DEFAULT_SOURCE@"
  },
  "audio_processing": {
    "denoise_enabled": true,
    "highpass_freq": 200,
    "normalize_enabled": true,
    "target_loudness": -14.0
  }
}
```

## Keybindings (TUI)

| Key | Action |
|-----|--------|
| `Space` / `Enter` | Toggle recording |
| `q` | Quit |
| `?` | Toggle help |

## Hyprland Integration

Add to your Hyprland config:

```conf
# Toggle screen recording
bind = $mainMod, R, exec, kartoza-screencaster toggle
```

## Building

```bash
# Build for current platform
make build

# Build static binary
make static

# Run tests
make test

# Run linter
make lint

# Build all platforms
make release
```

## License

MIT License - see [LICENSE](LICENSE) for details.

## Author

Tim Sutton - [Kartoza](https://kartoza.com)
