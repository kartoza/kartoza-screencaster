#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# Push the branch
git push origin v1.7.0

# Create PR
gh pr create \
  --base main \
  --head v1.7.0 \
  --title "v1.7.0: Fix X11 support, Qt-based monitor detection" \
  --body "$(cat <<'BODY'
## Summary

- **Qt QScreen fallback for monitor detection** — works on X11, macOS, Windows without needing xrandr, hyprctl, or any external tools. Detects `Virtual-1` in QEMU/KVM VMs correctly.
- **Qt-based screen preview on X11** — uses `QScreen::grabWindow()` instead of shelling out to ffmpeg for canvas preview screenshots. No external dependencies needed.
- **Fixed xrandr regex** — handles extra text (rotation/reflection) between "connected" and geometry that Ubuntu 24.04 VMs produce.
- **X11 recording fix** — removed guard that required non-empty monitor name, added stderr logging for ffmpeg x11grab debugging.
- **Deb packaging** — Wayland tools (grim, wl-screenrec) moved to Suggests, not required.

## Test plan

- [ ] Build and install .deb on Ubuntu 24.04 X11 VM
- [ ] Verify screen appears in Add Element > Screen menu with name (e.g. "Virtual-1")
- [ ] Verify canvas shows live screen preview
- [ ] Verify recording works on X11
- [ ] Verify existing Wayland functionality still works

## Includes all v1.6.0 changes

- YouTube integration (OAuth2, upload with metadata, playlist selection)
- Kartoza colour scheme (replaced Catppuccin)
- History page redesign (tree view, thumbnails, editable titles, rename)
- Recording in-progress page with stop circle
- Countdown numbers in systray icon
- Preset rename by double-click
- Cancel processing button
- Consistent icon buttons throughout

BODY
)"

echo ""
echo "PR created! Merge it, then run:"
echo ""
echo "  git checkout main && git pull"
echo "  git tag -a v1.7.0 -m 'Release v1.7.0'"
echo "  git push origin v1.7.0"
echo ""
echo "That will trigger the release build."
