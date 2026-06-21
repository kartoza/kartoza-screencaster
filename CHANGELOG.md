# Changelog

All notable changes to Kartoza Screencaster will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.9.0] - 2026-06-21

### Added

#### xdg-desktop-portal capture path for GNOME and KDE Wayland
Screen preview and recording now work on Wayland compositors that do not implement the `wlr-screencopy` protocol — most notably GNOME (Mutter) and KDE (KWin). Previously these compositors produced a blank canvas preview and an empty screen recording because the shipped `grim` and `wl-screenrec` tools are wlroots-only.

- New `src/platform/platform.{h,cpp}` helpers `Platform::compositor()` and `Platform::supportsWlrCapture()` based on `XDG_CURRENT_DESKTOP` / `XDG_SESSION_DESKTOP`.
- New `src/portal/portal.{h,cpp}` module wraps the `org.freedesktop.portal.Screenshot` and `org.freedesktop.portal.ScreenCast` D-Bus interfaces over QtDBus. The API is fully asynchronous (`requestScreenshot()` / `requestScreenCast()` return immediately and emit `screenshotReady` / `screenCastReady` signals when the portal eventually responds) so the UI never blocks on a portal dialog that may take many seconds.
- `Canvas::captureScreen()` falls back to the Screenshot portal on GNOME/KDE; the existing `grim` path is preserved unchanged for wlroots.
- `Recorder::startScreenRecorder()` opens a ScreenCast session and pipes the PipeWire stream through `gst-launch-1.0 pipewiresrc path=<node> fd=<portal-fd> ! videoconvert ! openh264enc ! h264parse ! mp4mux ! filesink`. The portal FD from `OpenPipeWireRemote` is `dup`'d, passed via `QProcess::setChildProcessModifier` to the forked gst-launch on a fixed slot (23), and used by `pipewiresrc` to read the screencast stream over its private permission-bound connection. The `restore_token` is persisted to QSettings so the source-picker dialog only appears on first run.
- `flake.nix` runtime deps gained GStreamer + plugins-base/good/bad/ugly + gst-libav + pipewire (for the `pipewiresrc` element) and `xorg.xrandr`. `GST_PLUGIN_SYSTEM_PATH_1_0` is set both by `wrapProgram` for the installed binary and by the dev shell hook so the `cr` command works without re-entering the shell.
- New `diagnose.sh` script prints a full system-capture diagnostic (display server, portal status, PipeWire status, GStreamer plugin paths and element presence, ffmpeg encoders, audio sources, V4L2 devices, fallback tools) for quick triage on user-reported recording failures.

#### Workaround for Qt 6.10 / Mutter ScreenCast Start demarshalling
The portal's Start response carries `streams: a(ua{sv})` with `(ii)` structs for `position` and `size` in the inner props dict. Qt 6.10's `QDBusMetaType::signatureToMetaType("(ii)")` returns a metatype whose demarshaller calls `dbus_message_iter_get_basic` while libdbus is at a struct iter position, aborting the process with `type struct 114 not a basic type`. Working around this required hand-rolled QDBus types at every nested layer:

- `IntPair` registered for signature `(ii)` so the inner props dict walks cleanly.
- `StreamEntry` registered for signature `(ua{sv})` so each stream element demarshalls cleanly.
- `StreamList = QList<StreamEntry>` registered for signature `a(ua{sv})` so Qt's auto-walk routes the outer streams field through the correct path.

With these three registrations in place, `onStartResponse(uint, QVariantMap)` reads the streams field as a real `QList<StreamEntry>` from the auto-walked dict — no manual demarshalling, no `QDBusMessage` slot, no crash.

### Fixed

#### Qt 6.10 `-Wunused-result` warnings in `test_canvas`
- `QFile::open()` is `[[nodiscard]]` in Qt 6.10. Wrapped the nine short-form open calls in the sound-related canvas tests with `QVERIFY`.

### Known limitations

- **OpenH264 encoder quality**: nixpkgs builds `gst-libav` without libx264, so we use Cisco's OpenH264 (`openh264enc`) which is lower quality than `x264enc` at the same bitrate. Acceptable for screen recording; revisit if/when gst-plugins-ugly's `x264enc` becomes routinely available.
- **No screen preview during portal source-picker first run**: while the user is choosing a screen in GNOME's portal dialog, the canvas preview can't refresh (the portal handshake holds the only screenshot-grant path). Once authorisation completes the preview resumes; subsequent runs are unaffected.

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
