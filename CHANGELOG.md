# Changelog

All notable changes to Kartoza Screencaster will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.8.1] - 2026-06-07

### Fixed

#### Webcam no longer opens when app launches into the tray
- The application launches with the main window hidden (tray-only). Previously, restoring the saved canvas state at startup would open the V4L2 webcam device for the live preview, even though no window was visible and no recording was in progress. The webcam indicator LED would stay on as a result.
- `Canvas::addWebcam` now defers starting the ffmpeg preview capture when the canvas widget is not visible. The existing show-event chain (`MainWindow::showEvent` → `RecordPage::resumePreviews` → `Canvas::resumePreviews` → `startAllWebcamPreviews`) starts the preview the first time the window is actually shown.
- The same guard avoids wasted captures if a webcam is added while the window is minimized to tray.

## [0.7.4] - 2026-01-25

### Added

#### Version Display in Header
- Version number now shown in header bar: "Kartoza Video Processor vX.X.X - Page Title"
- Development builds show next version with -dev suffix (e.g., 0.7.5-dev)

### Changed

#### Desktop Integration Improvements
- Desktop launcher now starts in systray mode (`kartoza-screencaster systray`)
- Custom application icon (icon_ready.svg) replaces generic video icon
- Terminal=false since systray mode runs in background
- All packaging formats updated: Nix flake, Debian, RPM, Snap, Flatpak
- Icons installed to hicolor theme (`share/icons/hicolor/scalable/apps/`)

## [0.7.3] - 2026-01-25

### Fixed

#### System Tray CGO Support
- **Linux amd64 now has full systray support** - Binary built with CGO enabled
- Fixed flake.nix to properly enable CGO for native Linux builds
- Added GTK3, glib, and libayatana-appindicator as build dependencies
- Split package builders: `mkNativePackage` (CGO) and `mkCrossPackage` (no CGO)

#### Build System Improvements
- Added `proxyVendor` and proper `vendorHash` for reproducible Nix builds
- CI now installs CGO dependencies for Linux systray builds
- Debian package includes runtime deps: `libgtk-3-0`, `libayatana-appindicator3-1`
- RPM package includes runtime deps: `gtk3`, `libayatana-appindicator-gtk3`
- Fixed Windows build: split syscall code into platform-specific files

### Platform Notes
| Platform | Systray Support | Notes |
|----------|----------------|-------|
| Linux amd64 | ✅ Full | CGO enabled, requires GTK3/AppIndicator |
| Linux arm64 | ❌ TUI only | CGO cross-compile not supported |
| macOS | ❌ TUI only | CGO cross-compile not supported |
| Windows | ❌ TUI only | CGO cross-compile not supported |

**Note**: All platforms support the full TUI interface. Systray mode (`kartoza-screencaster systray`) requires CGO which is only available on Linux amd64 builds.

## [0.7.1] - 2026-01-25

### Changed
- Renamed project from kartoza-video-processor to kartoza-screencaster
- Updated module path to github.com/kartoza/kartoza-screencaster

## [0.7.0] - 2026-01-24

### Added

#### System Tray Mode
A new background system tray applet for quick recording access without opening the full TUI:

- **New command**: `kartoza-screencaster systray`
- **Left-click**: Start recording (when idle) or stop recording (when active)
- **Double-click**: Pause/resume recording while active
- **Right-click**: Context menu with Pause/Resume, Open TUI, Quit options
- **State-specific icons**: Different icons for ready, recording, and paused states
- **Processing animation**: Spinning icon while video is being processed
- **Auto-launch TUI**: Opens TUI automatically after stopping to enter title/description
- **Tooltip updates**: Shows recording duration and status in real-time

Ideal for:
- Quick, spontaneous recordings
- Users who prefer desktop integration over terminal
- Adding metadata after recording instead of before

#### Terminal Recording Mode
Record terminal sessions using asciinema with automatic video conversion:

- **New command**: `kartoza-screencaster terminal`
- Records terminal sessions as asciinema cast files
- Automatic conversion to GIF (using `agg`) and MP4 (using `ffmpeg`)
- Configurable options:
  - `--title, -t`: Set recording title
  - `--idle-limit`: Maximum idle time in seconds (default: 5)
  - `--font-size`: Font size for video rendering (default: 16)
  - `--convert`: Convert existing .cast file without recording
- Works in terminal-only environments (no graphical display required)
- New config section `terminal_recording` for persistent settings

Ideal for:
- CLI tutorials and demonstrations
- Headless/SSH environments
- Terminal-focused content creation

#### New Dependencies
- `fyne.io/systray` v1.12.0 - Cross-platform system tray support
- Optional: `asciinema` and `agg` for terminal recording

### Changed
- Recording status now includes `needs_metadata` state for systray-initiated recordings
- History screen shows "Edit" status for recordings awaiting metadata entry
- Recordings from systray auto-open in edit mode when selected in history

## [0.6.1] - 2026-01-22

### Improved

#### History Screen
- Dynamic help text based on available video files (shows "v: Play" when only merged exists, "v: Vertical" when vertical exists)
- New video indicator icons in recording list:
  - 🎬 (clapper) shows when a processed video (vertical or merged) exists
  - 📺 (TV) shows when uploaded to YouTube

## [0.6.0] - 2026-01-22

### Added

#### Multi-Platform Syndication System
Announce your YouTube video uploads across 8 social media and communication platforms with a single action:

- **Mastodon** - Federated social network with OAuth2 authentication, supports any instance
- **Bluesky** - Decentralized AT Protocol network with app password authentication
- **LinkedIn** - Professional networking with OAuth2 and rich post previews
- **Telegram** - Bot-based posting to channels and groups with Markdown support
- **Signal** - End-to-end encrypted messaging via signal-cli integration
- **ntfy.sh** - Push notifications with click-through actions (self-hosted option)
- **Google Chat** - Workspace integration via incoming webhooks
- **WordPress** - Blog posts via REST API with app passwords

Key syndication features:
- Multi-account support for each platform
- Enable/disable individual accounts
- Platform-specific post formatting with character limits
- Automatic thumbnail upload where supported
- OAuth2 token refresh and session management
- Comprehensive setup documentation with step-by-step guides

#### Multi-Account YouTube Support
- Manage multiple YouTube accounts directly within the TUI
- Add, edit, and delete YouTube OAuth credentials
- Switch between accounts when uploading
- In-app account management (no manual JSON editing required)

#### History Screen Improvements
- New status column showing recording state (Processing, Ready, Uploaded, etc.)
- Error tracking with visual indicators for failed operations
- Media playback keybindings:
  - `p` - Play merged video
  - `v` - Play vertical video
  - `a` - Play audio file
  - `s` - Play screen recording

#### Recording Setup Enhancements
- Real-time spell checking for titles and descriptions
- Improved form styling with better visual feedback
- Enhanced text input handling

#### Documentation
- Comprehensive MkDocs documentation site
- Detailed setup guides for all syndication platforms
- Screen-by-screen user documentation
- Developer architecture guides

### Fixed
- All linting issues resolved
- Text input handling in form fields
- Layout consistency across all TUI screens

## [0.5.0] - 2026-01-17

### Added
- Experimental cross-platform support for macOS and Windows
- Platform-specific implementations for screen recording

## [0.4.1] - 2026-01-16

### Fixed
- Pause/resume/stop functionality bugs
- YouTube upload progress display

## [0.4.0] - 2026-01-15

### Added
- YouTube upload integration
- Playlist management
- Recording history with metadata

### Fixed
- Stop-start-stop processing bug
- Reprocess feature for failed recordings

## [0.3.0] - 2026-01-12

### Added
- Options screen with configurable settings
- Recording setup form with title/description
- Countdown timer before recording

## [0.2.0] - 2026-01-08

### Added
- Processing screen with progress indicators
- Audio normalization (EBU R128)
- Vertical video generation with webcam overlay

## [0.1.0] - 2026-01-05

### Added
- Initial release
- Multi-monitor screen recording
- Webcam capture at 60fps
- Audio recording with noise reduction
- Beautiful TUI interface
- CLI mode for scripting

[0.7.3]: https://github.com/kartoza/kartoza-screencaster/compare/v0.7.1...v0.7.3
[0.7.1]: https://github.com/kartoza/kartoza-screencaster/compare/v0.7.0...v0.7.1
[0.7.0]: https://github.com/kartoza/kartoza-screencaster/compare/v0.6.1...v0.7.0
[0.6.1]: https://github.com/kartoza/kartoza-screencaster/compare/v0.6.0...v0.6.1
[0.6.0]: https://github.com/kartoza/kartoza-screencaster/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/kartoza/kartoza-screencaster/compare/v0.4.1...v0.5.0
[0.4.1]: https://github.com/kartoza/kartoza-screencaster/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/kartoza/kartoza-screencaster/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/kartoza/kartoza-screencaster/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/kartoza/kartoza-screencaster/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/kartoza/kartoza-screencaster/releases/tag/v0.1.0
