#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Kartoza (Pty) Ltd <tim@kartoza.com>
# SPDX-License-Identifier: MIT
#
# Build a single Kartoza-branded PDF handbook from the mkdocs docs, using pandoc
# + pdflatex with docs/pdf/{cover,preamble}.tex. Mirrors the qgis-dev-env
# approach. Requires: pandoc, a TeX Live with the packages in preamble.tex, and
# rsvg-convert (librsvg) for vector figures.
#
# Usage: docs-pdf.sh [output.pdf]     (default: kartoza-screencaster-handbook.pdf)
set -euo pipefail
# Ensure sed/awk/pandoc treat the docs as UTF-8 (else multibyte glyphs corrupt
# and pandoc falls back to latin1).
export LC_ALL=C.UTF-8 LANG=C.UTF-8

find_root() {
  local d="$PWD"
  while [ "$d" != "/" ]; do
    [ -f "$d/mkdocs.yml" ] && { echo "$d"; return 0; }
    d="$(dirname "$d")"
  done
  echo "docs-pdf: run inside the project (no mkdocs.yml found)" >&2
  return 1
}
ROOT="$(find_root)"
DOCS="$ROOT/docs"
PDFDIR="$DOCS/pdf"
OUT="${1:-kartoza-screencaster-handbook.pdf}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Pages in reading order (subset of mkdocs nav that makes sense in print).
PAGES=(
  index.md
  getting-started/index.md getting-started/install.md getting-started/first-recording.md
  user-guide/index.md user-guide/canvas.md user-guide/recording.md user-guide/webcam.md
  user-guide/overlays.md user-guide/audio.md user-guide/history.md user-guide/youtube.md
  admin-guide/index.md admin-guide/packaging.md admin-guide/dependencies.md
  developer-guide/index.md developer-guide/dev-shell.md developer-guide/build.md
  developer-guide/architecture.md developer-guide/capture-pipelines.md
  developer-guide/contributing.md
  about/index.md about/changelog.md about/license.md about/sponsors.md
)

# Per-page cleanup so mkdocs-material markdown survives pdflatex.
transform() {
  awk '
    BEGIN { fm=0; mer=0 }
    # Strip YAML front-matter (--- ... --- at the very top).
    NR==1 && $0=="---" { fm=1; next }
    fm==1 && $0=="---" { fm=0; next }
    fm==1 { next }
    # Drop mermaid fenced blocks (pdflatex cannot render them).
    /^```mermaid/ { mer=1; print "*[diagram — see the online handbook]*"; next }
    mer==1 && /^```/ { mer=0; next }
    mer==1 { next }
    { print }
  ' \
  | sed -E \
      -e 's/^!!![[:space:]]+[a-z-]+[[:space:]]*"([^"]*)".*/**\1**/' \
      -e 's/^!!![[:space:]]+([a-z-]+).*/**\u\1**/' \
      -e 's/<\/?div[^>]*>//g' \
      -e 's/<\/?span[^>]*>//g' \
      -e 's/:material-[a-z0-9-]+:[[:space:]]*//g' \
      -e 's/:simple-[a-z0-9-]+:[[:space:]]*//g' \
      -e 's/:fontawesome-[a-z0-9-]+:[[:space:]]*//g' \
      -e 's#!\[[^]]*\]\(https?://[^)]*\)##g' \
      -e 's/\{[[:space:]]*\.[a-z][^}]*\}//g' \
      -e 's/[▸►]/>/g' -e 's/✓/[x]/g' -e 's/✗/[ ]/g' \
      -e 's/[─━┈┄╌]/-/g' -e 's/[│┃┊┆╎]/|/g' -e 's/[├└┌┐┘┬┴┼┤╭╮╯╰]/+/g' \
      -e 's/💗/love/g' -e 's/🎬//g' -e 's/🤖//g'
}

# Assemble the combined markdown.
COMBINED="$WORK/combined.md"
: > "$COMBINED"
for p in "${PAGES[@]}"; do
  f="$DOCS/$p"
  [ -f "$f" ] || { echo "docs-pdf: skip missing $p" >&2; continue; }
  transform < "$f" >> "$COMBINED"
  printf '\n\n\\newpage\n\n' >> "$COMBINED"
done

# Bulletproof against any remaining exotic glyph in code blocks (box-drawing,
# emoji, symbols) that pdflatex can't typeset: transliterate to ASCII. Prose
# punctuation (em-dashes, curly quotes) degrades gracefully to ASCII equivalents.
iconv -f UTF-8 -t ASCII//TRANSLIT "$COMBINED" 2>/dev/null > "$COMBINED.clean" && mv "$COMBINED.clean" "$COMBINED" || true

# Pre-render any SVGs referenced from the docs to PDF for crisp vector embedding.
if command -v rsvg-convert >/dev/null; then
  while IFS= read -r -d '' svg; do
    rsvg-convert -f pdf -o "${svg%.svg}.pdf" "$svg" 2>/dev/null || true
  done < <(find "$DOCS/assets" -name '*.svg' -print0 2>/dev/null)
fi

echo "docs-pdf: rendering $OUT …"
pandoc "$COMBINED" \
  --pdf-engine=pdflatex \
  --include-in-header="$PDFDIR/preamble.tex" \
  --include-before-body="$PDFDIR/cover.tex" \
  --toc --toc-depth=2 \
  --resource-path="$WORK:$DOCS:$DOCS/assets:$DOCS/assets/brand" \
  -V fontfamily=lato -V fontfamilyoptions=default \
  -V geometry:margin=2.5cm \
  -V colorlinks=true -V linkcolor=kartozablue -V urlcolor=kartozablue \
  -V documentclass=article \
  -o "$OUT"

echo "docs-pdf: wrote $OUT"
