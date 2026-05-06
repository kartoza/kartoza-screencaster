#!/usr/bin/env bash
set -euo pipefail

BRANCH="feat/v1.4.0-playback-aspect-tray"

# Create feature branch
git checkout -b "$BRANCH"

# Stage all changed files
git add \
  CMakeLists.txt \
  flake.nix \
  src/main.cpp \
  src/gui/mainwindow.cpp \
  src/gui/canvas.h \
  src/gui/canvas.cpp \
  src/gui/historypage.h \
  src/gui/historypage.cpp \
  src/recorder/recorder.h \
  src/recorder/recorder.cpp \
  tests/test_canvas.cpp \
  tests/test_history.cpp \
  git.sh

# Commit
git commit -m "$(cat <<'EOF'
feat: history playback, aspect ratio enforcement, start-to-tray

- Fix history page video playback by using QVideoWidget and configuring
  GStreamer plugin paths in flake.nix devShell
- Store vertical output files in recording.json; findBestVideo priority:
  merged > vertical > screen with fallback directory scans
- Add "Open Folder" button to history page
- Canvas maintains 16:9 aspect ratio with letterboxing on window resize
- Logo overlays preserve source image aspect ratio during wheel resize,
  mode changes, and canvas resize
- App starts to tray only; close button hides to tray; quit from tray
  menu or sidebar
- Add test_history (17 tests) and extend test_canvas (18 new tests)
- Bump version to 1.4.0

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"

# Push feature branch
git push -u origin "$BRANCH"

# Create PR
gh pr create \
  --title "feat: v1.4.0 — playback, aspect ratio, start-to-tray" \
  --body "$(cat <<'EOF'
## Summary

- **History playback fixed**: Replaced broken QVideoSink+QLabel with QVideoWidget; configured GStreamer plugin paths in flake.nix so Qt multimedia backend is found at runtime
- **Vertical file tracking**: Vertical output files now stored in `m_verticalFile` and written to `recording.json`; `findBestVideo` checks merged > vertical > screen
- **Canvas aspect ratio**: Canvas maintains 16:9 with letterboxing/pillarboxing; logo overlays preserve source image aspect ratio during all resize operations
- **Start-to-tray**: App launches with only systray icon; close button hides to tray; quit available from tray menu and new sidebar Quit button
- **Open Folder button**: New button in history page opens recording directory in file manager

## Tests

- `test_history`: 17 new tests for `findBestVideo` priority logic and recording directory scenarios
- `test_canvas`: 18 new tests for logo aspect ratio preservation (wheel, resize, mode change) and canvas 16:9 enforcement

## Test plan

- [ ] Build succeeds: `cb`
- [ ] All tests pass: `QT_QPA_PLATFORM=offscreen ct`
- [ ] App starts with only tray icon visible
- [ ] Clicking X hides window to tray
- [ ] Quit from tray menu and sidebar both exit fully
- [ ] History page: selecting a recording and pressing Play shows video
- [ ] History page: Open Folder button opens directory in file manager
- [ ] Canvas maintains 16:9 when window is resized to non-16:9 dimensions
- [ ] Logo aspect ratio preserved when scrolling to resize
EOF
)"

echo ""
echo "PR created for branch $BRANCH."
echo "Once CI passes, merge with: gh pr merge --merge"
