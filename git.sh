#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# Push the v2.1.0 preview-pause / hidden-capture branch, open the tracking
# issue, and create the PR (closing the issue on merge via `Fixes`).
#
# Run from a session where your SSH agent and `gh` auth are available:
#   ./git.sh

BRANCH="feat/v2.1.0-preview-pause-and-hidden-capture"

# 1. Push the branch and set upstream.
git push -u origin "$BRANCH"

# 2. Create the tracking issue (assigned to you) and capture its number.
ISSUE=$(gh issue create \
  --title "Live preview keeps capturing when hidden; add pause control" \
  --label bug,enhancement \
  --assignee @me \
  --body "$(cat <<'BODY'
## User story

As a presenter, when I minimise the app or hide it to the tray, the live
screen preview must stop capturing so `grim`/`slurp` is not running unseen —
and I want a manual pause control on the preview.

```mermaid
flowchart LR
  U[User] -->|minimise / switch tab / hide to tray| MW[MainWindow.updatePreviewState]
  U -->|click preview centre| C[Canvas.togglePreviewPause]
  MW --> T[Canvas capture timer]
  C --> T
  T -->|runs only when visible & not paused| G[grim/slurp]
```

## Success criteria

- No `grim`/`slurp` process spawns while the window is minimised, hidden to
  tray, or the Record tab is not current.
- Recording is unaffected (capture stays off during recording).
- Clicking the preview centre pauses/resumes; the centre icon reflects state
  and is hidden on an actively-updating preview unless hovered.

## Job size

S
BODY
)" | grep -oE '[0-9]+$')

echo "Opened issue #${ISSUE}"

# 3. Create the PR (base main), closing the issue on merge.
gh pr create \
  --base main \
  --head "$BRANCH" \
  --assignee @me \
  --title "feat(canvas): stop background capture when hidden and add pause toggle" \
  --body "$(cat <<BODY
Fixes #${ISSUE}

## What

- **Bug:** minimising / hiding to tray / leaving the Record tab left the live
  preview capturing (\`grim\`/\`slurp\` spawned every 2s). Gating is now
  centralised in \`MainWindow::updatePreviewState()\` and driven from
  \`showEvent\`/\`hideEvent\`/a new \`changeEvent\` (catches \`WindowStateChange\`)/
  \`navigateTo\`/\`hideToTray\`/\`showFromTray\`. Recording stays covered.
- **Feature:** click-to-pause toggle in the preview centre (pause bars / play
  triangle), hover-gated so it stays out of the way; manual pause survives
  tab-switch and minimise.
- **Dev fix:** dev-shell \`QT_PLUGIN_PATH\` now includes qtbase/qtwayland/qtsvg
  (was qtmultimedia only), so dev runs render the title-bar window menu
  correctly instead of tofu. The wrapped package was never affected.
- Version bumped to 2.1.0; CHANGELOG updated.

## Test

- \`test_canvas\` and \`test_history\` pass. \`test_pipeline\` /
  \`test_merger_exhaustive\` fail only on ffprobe in the sandbox (pre-existing,
  unrelated to this change).
- Manual: minimise while previewing → no new \`grim\` (\`pgrep -a grim\`); click
  the preview → pause/resume.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
BODY
)"

echo ""
echo "PR created. After it merges, cut the release tag:"
echo ""
echo "  git checkout main && git pull"
echo "  git tag -a v2.1.0 -m 'Release v2.1.0'"
echo "  git push origin v2.1.0"
echo ""
echo "That triggers the release build."
