# Changelog

All notable changes to Kartoza Screencaster will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

#### Multiple text boxes with per-box font, weight and colour (WYSIWYG)
The canvas previously allowed a single title text item with one global colour and
no font control. You can now add any number of independent text boxes (Add ▸ Text
Box), each with its own text, **font family**, **weight** (Light/Normal/Bold) and
**colour**, edited inline from a controls row that appears when a text box is
selected. Font **size** continues to derive from the box height (resize the box to
scale the text), and the artificial size ceiling is gone. Every text box is burned
into the final recording — in both landscape and vertical layouts — with its font,
weight, colour and position, so the output matches the preview. Font weight is
resolved to a concrete font file via fontconfig so FFmpeg renders the requested
weight. Per-box font/weight/colour persist across restarts and in presets; legacy
single-title recordings still render their title unchanged.

#### Aspect-locked handle resize with Alt-to-crop switching
Dragging a selected item's corner handles now scales it proportionally (the same
aspect-locked result as the scroll wheel), so resizing behaves like mainstream
design tools. Holding **Alt** turns the handles Kartoza blue and switches to
crop mode (the previous edge-inset behaviour); releasing Alt returns to resize.
The wheel and handle-drag now share a single `setItemWidthKeepingAspect` path so
they scale identically. Applies to screen, webcam, logo and text items.

#### Snapping to object edges and half-dimension guides
Dragging an overlay now snaps not only to the scene-frame edges but also to the
frame's half-width/half-height centre lines and to **other objects' edges and
centres**, with a guide line drawn while a snap is engaged. Holding **Shift**
disables snapping for free placement.

### Fixed

#### Webcams no longer disappear from the Add menu
Webcam enumeration was run once at startup — before the window was shown, and
possibly before devices had settled — then cached for the whole session, and it
relied on the fragile sysfs `index` attribute to pick a camera's capture node
(which dropped valid cameras on modern multi-node UVC devices). The Add ▸ Webcam
menu now re-detects every time it opens, and Linux detection probes each
`/dev/videoN` with `VIDIOC_QUERYCAP`, keeping only true `VIDEO_CAPTURE` nodes and
de-duplicating by physical device (`bus_info`). Metadata-only nodes are excluded
and unopenable nodes are skipped rather than fatal.

#### Selected preset/layer text stays legible
The Layers and Presets lists set a dark selection background but left the
selected-row text colour to Qt's palette default, rendering dark text on the dark
highlight. Both lists now keep the light text colour when selected.

#### Text boxes resize to any size, with or without aspect lock
Text boxes were effectively capped in size because handle resize scaled about the
item centre, so the reachable size was bounded by the preview. Resize now anchors
to the opposite handle and tracks the cursor, so a box can be made as large (or
small) as you like. Corner handles keep the box aspect (hold **Shift** to stretch
freely); the new **edge** handles resize a single axis, so text can be any width
or height independently.

#### Font and weight dropdowns are legible
The font-family and weight dropdown popups rendered dark text on a dark list. Both
popups now use the light-on-dark palette, matching the rest of the panel.

#### Text overlays with special characters in the font name render correctly
The FFmpeg `drawtext` filter interpolated the font family and font-file path
without escaping, so a family or path containing `:`, `'` or `\` corrupted the
filtergraph and the render failed. All values are now escaped consistently.

#### Reopening the app restores crop and screen placement
The startup state restore silently dropped item crops, the screen's saved
position/size, and start/end sounds — so a layout that looked correct when saved
came back subtly different after a restart (whereas loading it as a preset
restored everything). Startup restore and preset load now share one code path,
so a reopened session matches exactly what was saved.

### Changed

#### Closing the window hides it to the system tray
Clicking the window's close (X) button now fully hides the window to the system
tray (the app keeps running and is restored from the tray icon) when a tray is
available, instead of only minimising. Without a system tray it falls back to
minimising so there is always a way back.

#### Faster developer builds (ccache + mold + Ninja)
The dev shell now ships the **mold** linker and CMake uses it automatically
(`-fuse-ld=mold` when found), alongside the existing ccache and Ninja, matching
the fast incremental-build loop used by `qgis-dev-env`.

#### Branded documentation hero
The docs landing-page hero now uses the Kartoza slant background under a
translucent overlay (light and dark variants), aligning it with the
`qgis-dev-env` documentation styling instead of a flat fill.

#### Unified `ksc-dev` developer command with build metrics
The dev shell adds a single `ksc-dev` entry point (`build`, `release`, `run`,
`test`, `configure`, `format`, `clean`, `docs`, `stats`) alongside the existing
short aliases. `ksc-dev build` logs the build duration and ccache hit rate to a
TSV, and `ksc-dev stats [--graph]` summarises it — mirroring the `qgis-dev`
workflow. Documented in the Dev shell guide.

### Internal

A code-quality pass fixed a `QMovie` use-after-free on window close, moved the
webcam frame buffer off the heap, closed a `m_monitorName` data race in the
threaded screen-capture path, stopped the canvas repainting every 2 s while idle,
cached `fc-match` font resolution, and de-duplicated the frame-rescale logic
(`setMode`/`resizeEvent`) and single-item export paths — all covered by new
regression tests.

_Work toward 2.2.0._

## [2.1.0] - 2026-07-18

### Fixed

#### Live screen preview no longer captures in the background when hidden
The record layout's live preview drives a `slurp`/`grim` (or portal) screen capture on a 2-second `QTimer` in `Canvas`. Suspension of that loop previously hung off `QMainWindow::hideEvent`, but minimising a window — via the compositor, the titlebar button, or `showMinimized()` when hiding to the systray — does **not** fire a hide event (`isVisible()` stays true for a minimised window), and `hideToTray()` explicitly skipped suspension while recording. The net effect: a minimised or tray-docked window kept spawning a `grim` process every 2 seconds indefinitely, wasting CPU and issuing screen captures the user could not see.

Preview gating is now centralised in `MainWindow::updatePreviewState()`, the single authority for whether the capture loop may run. The loop runs only when **all** of these hold: the window is shown, **not minimised**, not hidden-to-tray, and the Record page is the current tab. It is invoked from `showEvent`, `hideEvent`, the new `changeEvent` override (which catches `QEvent::WindowStateChange` — the minimise/restore signal that was being missed), `navigateTo`, `hideToTray`, and `showFromTray`. Recording remains covered because `RecordPage::resumePreviews()` is a no-op while recording, so the slurp loop stays off for the entire recording regardless of window state.

Internally, `Canvas` now separates app-driven suspension (`m_suspended`) from user-driven pause (`m_userPaused`); a single `updateTimerState()` starts the timer only when neither is set, so the capture truly stops rather than firing into a no-op.

#### Title-bar window menu renders correctly when running from the dev shell
The `nix develop` shell exported `QT_PLUGIN_PATH` pointing only at `qtmultimedia`, so an unwrapped dev binary (`cr`, `./build/kartoza-screencaster`) loaded the host system's Qt for the Wayland platform and client-side **decoration** plugins. On a machine whose system Qt differs from nixpkgs', the title-bar right-click window menu rendered as missing-glyph blocks (tofu). The dev shell now points `QT_PLUGIN_PATH` at the nixpkgs `qtbase`, `qtwayland`, `qtsvg`, and `qtmultimedia` plugin directories — the same set the `wrapQtAppsHook`-wrapped package already uses — so dev runs match the installed app. The packaged build was never affected.

### Added

#### Click-to-pause toggle on the live screen preview
The screen preview now has a circular pause/continue button in its centre. Clicking it freezes or resumes the live capture on demand — pause bars indicate a running preview (click to pause), a play triangle over a darkened backdrop indicates a paused preview (click to resume), so a frozen preview always reads as intentional rather than broken. The manual pause survives tab switches and minimise/restore cycles. To keep the preview uncluttered, the toggle is only drawn while the cursor is over the canvas or while the preview is paused; it fades out when the mouse leaves an actively-updating preview.

## [2.0.3] - 2026-06-25

### Changed

#### Single source of truth for the project version
The version number used to live in `CMakeLists.txt` and was re-derived in several places by regex (`flake.nix`, `release.yml`), each one a separate failure mode if a future edit broke the syntax. Other tools that should have read it — `Doxyfile`'s `PROJECT_NUMBER`, `mkdocs.yml`, `README.md` — drifted instead (the Doxygen API reference was still labelled 1.9.0 at the 2.0.2 release).

This release consolidates the version into a single plain-text `VERSION` file at the repo root. Every consumer reads it directly:

- `CMakeLists.txt` reads `VERSION` via `file(STRINGS)` and feeds it into `project(... VERSION ...)`, so `APP_VERSION` and `CPACK_PACKAGE_VERSION` follow automatically.
- `flake.nix` reads it via `builtins.readFile ./VERSION` (replacing the previous CMakeLists.txt regex).
- `.github/workflows/release.yml`'s tag-check job reads it via `cat VERSION`.
- `Doxyfile` `PROJECT_NUMBER` is now `$(KSC_VERSION)`; the three `nix run .#docs-*` apps that invoke Doxygen all `export KSC_VERSION="$(cat VERSION)"` first, so the generated C++ API reference always matches the repo's version.
- `README.md` and `docs/index.md` gained live `shields.io` badges that read from the GitHub releases API, so no rebuild is needed when a new release ships.

Bumping the version is now a one-line edit to `VERSION`. The `release.yml` `version-check` job still enforces tag/file consistency.

### Removed

#### Orphan Go-era packaging files
`nfpm.yaml`, `packaging/snap/`, and `packaging/flatpak/` were leftovers from the pre-rewrite Go codebase (last touched at 0.7.6 / 0.8.2). `release.yml` doesn't build any of them and they had drifted out of sync with the project for ten releases. Deleted to eliminate the drift surface.

`packaging/debian/` and `packaging/rpm/` are also Go-era (0.8.2) and not consumed by `release.yml` either, but they look like in-tree templates for a future `dpkg-buildpackage` / `rpmbuild` source-package path, so they survive this round. Worth a separate decision on whether to modernise them against the current Qt 6 build or remove them.

## [2.0.2] - 2026-06-25

### Fixed

#### Screen-preview pane is no longer blank when launched from the Ubuntu desktop GUI
On Ubuntu 26.04 (and any other GNOME Wayland session where gnome-shell does not propagate `WAYLAND_DISPLAY` / `XDG_SESSION_TYPE` to the systemd-user environment), launching the app from the dock, app overview, or any `.desktop` entry produced a permanently blank screen-preview pane — even though launching the same binary from a terminal in the same desktop session worked fine. The recording itself was not affected because that path doesn't sit in the same code branch.

The root cause was `Platform::displayServer()` sniffing only `WAYLAND_DISPLAY` and `XDG_SESSION_TYPE`. When both env vars were empty (the stripped systemd-user env handed to `.desktop` launches on Ubuntu), the function returned `Unknown`, `Canvas::captureScreen()` skipped the Wayland branch entirely, and fell through to the X11/macOS fallback `QScreen::grabWindow(0)` — which on a Wayland session returns an empty pixmap. Launching from the terminal worked only because bash/profile happened to inherit or re-set those env vars.

`Platform::displayServer()` now consults `QGuiApplication::platformName()` first — `"wayland"` or `"xcb"` is the authoritative answer for what Qt actually loaded at startup, set by Qt itself from compiled-in platform plugins and not subject to the systemd-user env propagation quirks. The env-var sniffing is preserved as a fallback for unusual platform names (`offscreen`, `eglfs`, …) and pre-`QGuiApplication` callers.

The portal-screenshot failure handler in `Canvas` also escalated from `qDebug` to `qWarning` so failures from `.desktop` launches show up in `journalctl --user` by default. They were previously filtered out at the default log level, leaving silent failures invisible.

## [2.0.1] - 2026-06-25

### Fixed

#### `.deb` package is now installable on Ubuntu 26.04 (and 22.04, Debian 12)
The 2.0.0 `.deb` hard-pinned the `t64`-suffixed Qt library names
(`libqt6gui6t64`, `libqt6widgets6t64`, `libqt6dbus6t64`,
`libqt6network6t64`, `libqt6core6t64`) because the GitHub Actions
runner that builds the package is Ubuntu 24.04, where those names
were in force during the in-flight time_t 64-bit transition. On
Ubuntu 26.04 the transition is complete and those packages have been
renamed back to the originals (`libqt6gui6`, …); on Ubuntu 22.04 LTS
and Debian 12 the suffixed names never existed. In both cases
`sudo apt install ./kartoza-screencaster_2.0.0_amd64.deb` failed with
`Depends: libqt6gui6t64 but it is not installable`.

The release workflow now declares every transitioned Qt dependency
as a Debian alternative (`libqt6gui6 | libqt6gui6t64`, etc.) so the
same `.deb` resolves cleanly across Ubuntu 22.04, 24.04, 26.04, and
Debian 12/13.

#### `.deb` now declares the Qt Wayland platform plugin
`qt6-wayland` was missing from the dependency list. The app would
fall back to XCB on pure-Wayland sessions, which only works if
XWayland is running. The plugin is now a hard dependency.

### Documentation

- `docs/getting-started/install.md` rewritten with concrete per-distro
  install commands (Ubuntu / Debian, Fedora, Arch), a clear note on
  which Ubuntu releases use the `t64`-suffixed names, and a manual
  `dpkg -i --ignore-depends` workaround for users stuck on 2.0.0 on
  Ubuntu 26.04 until 2.0.1 ships.
- `docs/admin-guide/dependencies.md` extended with full `apt` / `dnf`
  / `pacman` commands for every runtime dependency so fleet operators
  can install dependencies by hand without going through the `.deb`
  or `.rpm`.
- Removed the orphan `docs/getting-started/installation.md` (a stale
  Go-era page from before the rewrite).

## [2.0.0] - 2026-06-24

### Added

#### MkDocs documentation site with integrated Doxygen API browser
A full project documentation site is now built and published to GitHub Pages at https://kartoza.github.io/kartoza-screencaster/. The site combines a Material-themed MkDocs handbook (project overview, user guide, administrator guide, developer guide) with the Doxygen-generated C++ API reference mounted under `/api/`.

- New `mkdocs.yml` and `docs/` tree following the Kartoza brand pack (Nunito / JetBrains Mono, tokenised palette in `docs/stylesheets/kartoza-tokens.css`).
- `Doxyfile` rewired to emit to `build/doxygen/` so the API tree can be symlinked into the MkDocs `site/api/` output without polluting the docs source.
- `flake.nix` `mkdocsEnv` package bundles `mkdocs-material`, `mkdocs-glightbox`, `mkdocs-git-revision-date-localized-plugin` and Doxygen + Graphviz so docs build entirely from nixpkgs — no `pip install` step anywhere in dev or CI.
- New `nix run` commands surfaced in the dev shell help text:
  - `nix run .#docs-serve` — MkDocs only, fastest iteration loop.
  - `nix run .#docs-build` — MkDocs only, strict mode.
  - `nix run .#docs-doxygen` — Doxygen only.
  - `nix run .#docs-full-build` — MkDocs + Doxygen combined into a single `site/` tree.
  - `nix run .#docs-full-serve` — combined build served on `http://127.0.0.1:8000` so the `/api/` link resolves locally exactly as it does on GitHub Pages.
- `.github/workflows/Docs.yml` rewritten to use `DeterminateSystems/nix-installer-action` and `nix run .#docs-full-build` — no `pip` or `apt` on the docs CI path. Publishes to `gh-pages` via `actions/deploy-pages`.
- Landing page (`docs/index.md`) rewritten around end-user value ("Record. Don't edit.") — no Qt/portal/PipeWire/V4L2 references on user-facing pages, all that detail lives under the developer guide.

### Fixed

#### `.deb` and `.rpm` packages now pull the portal capture runtime stack
The 1.9.0 Debian and RPM packages declared only the basic Qt libraries and `ffmpeg`. That was enough to launch the app, but the canvas preview stayed blank and recording produced no file on the default Ubuntu (GNOME Wayland) and Fedora (GNOME Wayland) configurations, because the `xdg-desktop-portal` + PipeWire + GStreamer stack the portal capture path depends on at runtime was not installed alongside the binary. (The flake build was unaffected because every dependency is wrapped into the binary's `PATH` and `GST_PLUGIN_SYSTEM_PATH_1_0` at install time.)

This release adds the missing runtime dependencies to both packaging formats:

- **Required**: `libqt6dbus6t64`, `libqt6svg6`, `libqt6network6t64`, `xdg-desktop-portal`, `pipewire`, `gstreamer1.0-tools`, `gstreamer1.0-plugins-{base,good,bad}`, `gstreamer1.0-libav`, `gstreamer1.0-pipewire`, `x11-xserver-utils` (for `xrandr`). RPM counterparts added in the `.rpm` spec.
- **Recommended**: `wl-screenrec`, `grim`, and one of `xdg-desktop-portal-gnome` / `xdg-desktop-portal-kde`.

Users who installed the 1.9.0 `.deb` and saw a blank preview can either upgrade to 2.0.0 or manually install the same package list to fix their existing install.

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
