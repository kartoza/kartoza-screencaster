#!/usr/bin/env bash
set -euo pipefail

VERSION="v1.4.0"

# 3. Tag and push
git tag -a "$VERSION" -m "Release $VERSION"
git push origin "$VERSION"

# 4. Create GitHub release (triggers release.yml to build all OS binaries)
gh release create "$VERSION" \
  --title "Kartoza Screencaster $VERSION" \
  --notes "$(
    cat <<'NOTES'
## What's New in 1.4.0

### History Page Playback
- **Video playback now works** — replaced broken QVideoSink+QLabel with QVideoWidget and configured GStreamer plugin paths for NixOS
- Vertical recording output files are now tracked in `recording.json` and discoverable by the history page
- Video discovery priority: merged > vertical > raw screen capture, with fallback directory scanning
- New **Open Folder** button opens the recording directory in your file manager

### Canvas Aspect Ratio Enforcement
- Canvas maintains **16:9 aspect ratio** with letterboxing/pillarboxing when the window is resized to non-16:9 dimensions
- **Logo overlays preserve their source image aspect ratio** during mouse wheel resize, mode changes, and canvas resize events
- Webcam overlays continue to enforce 1:1 (round) and 4:3 (rect/square) aspect ratios

### Start-to-Tray
- Application **starts with only the system tray icon** visible (no main window on launch)
- Clicking the window close button (X) **hides to tray** instead of quitting
- **Quit** available from both the tray menu and a new sidebar button
- Full quit closes the application including the system tray

### Testing
- Added `test_history` suite (17 tests) covering video discovery logic
- Added 18 new canvas tests for logo/canvas aspect ratio preservation
- All tests pass on Linux, macOS, and Windows CI

---
Made with :heart: by [Kartoza](https://kartoza.com) | [Donate](https://github.com/sponsors/kartoza) | [GitHub](https://github.com/kartoza/kartoza-screencaster)
NOTES
  )"

echo ""
echo "Release $VERSION created!"
echo "CI will build and attach Linux/macOS/Windows/deb/rpm packages (~10 min)."
echo ""
echo "Monitor: https://github.com/kartoza/kartoza-screencaster/actions"
echo "Release: https://github.com/kartoza/kartoza-screencaster/releases/tag/$VERSION"
