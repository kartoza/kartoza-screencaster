#!/usr/bin/env bash
set -euo pipefail

git add tests/test_canvas.cpp git.sh

git commit -m "$(cat <<'EOF'
fix: canvas resize tests need show() before resize in offscreen mode

resizeEvent only fires when the widget is visible. Add c.show() before
c.resize() in all canvas aspect ratio tests so they work in CI with
QT_QPA_PLATFORM=offscreen.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"

git push

echo "Fix pushed to PR branch."
