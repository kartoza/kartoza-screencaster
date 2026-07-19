#!/usr/bin/env bash
# SPDX-FileCopyrightText: Kartoza
# SPDX-License-Identifier: MIT
#
# ksc-dev — unified developer command for Kartoza Screencaster.
# Wraps the build / run / test / docs loop and logs build metrics, aligning the
# developer experience with timlinux/qgis-dev-env (ccache + mold + Ninja).
#
# Usage: ksc-dev <command> [args]
set -euo pipefail

# --- locate the project root (dir containing CMakeLists.txt) -----------------
find_root() {
  local d="$PWD"
  while [ "$d" != "/" ]; do
    [ -f "$d/CMakeLists.txt" ] && { echo "$d"; return 0; }
    d="$(dirname "$d")"
  done
  echo "ksc-dev: not inside the project (no CMakeLists.txt found)" >&2
  return 1
}
ROOT="$(find_root)"
BUILD="$ROOT/build"
STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/kartoza-screencaster"
LOG="$STATE_DIR/build-log.tsv"

# --- ccache stats helpers (best-effort) --------------------------------------
ccache_hits()   { ccache --print-stats 2>/dev/null | awk -F'\t' '/^(direct_cache_hit|preprocessed_cache_hit)/{s+=$2} END{print s+0}'; }
ccache_misses() { ccache --print-stats 2>/dev/null | awk -F'\t' '/^cache_miss/{print $2+0; f=1} END{if(!f) print 0}'; }

log_build() { # profile  target  duration_s  hit_before misses_before
  local profile="$1" target="$2" dur="$3" hb="$4" mb="$5"
  command -v ccache >/dev/null || { hb=0; mb=0; }
  local ha mb2 dh dm rate
  ha="$(ccache_hits 2>/dev/null || echo "$hb")"
  mb2="$(ccache_misses 2>/dev/null || echo "$mb")"
  dh=$(( ha - hb )); dm=$(( mb2 - mb ))
  if [ $(( dh + dm )) -gt 0 ]; then rate=$(( 100 * dh / (dh + dm) )); else rate=""; fi
  local branch ts
  branch="$(git -C "$ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo '-')"
  ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  mkdir -p "$STATE_DIR"
  [ -f "$LOG" ] || printf 'timestamp\tbranch\tprofile\ttarget\tduration_s\tccache_hit_pct\n' > "$LOG"
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$ts" "$branch" "$profile" "${target:-all}" "$dur" "$rate" >> "$LOG"
}

do_build() { # profile  [target...]
  local profile="$1"; shift || true
  local ctype="Debug"; [ "$profile" = "release" ] && ctype="Release"
  mkdir -p "$BUILD"
  ( cd "$BUILD" && cmake .. -G Ninja -DCMAKE_BUILD_TYPE="$ctype" >/dev/null )
  local hb mb; hb="$(ccache_hits 2>/dev/null || echo 0)"; mb="$(ccache_misses 2>/dev/null || echo 0)"
  local start; start=$SECONDS
  ( cd "$BUILD" && ninja "$@" )
  local dur=$(( SECONDS - start ))
  log_build "$profile" "${*:-}" "$dur" "$hb" "$mb"
  echo "ksc-dev: build ($profile) finished in ${dur}s"
}

cmd_stats() { # [--graph]
  [ -f "$LOG" ] || { echo "No build history yet ($LOG). Run 'ksc-dev build' first."; return 0; }
  local n avg last
  n=$(( $(wc -l < "$LOG") - 1 ))
  echo "Build history: $n builds  ($LOG)"
  awk -F'\t' 'NR>1{d+=$5; c++; if($6!=""){h+=$6; hc++}}
    END{ if(c) printf "  avg build: %.1fs\n", d/c;
         if(hc) printf "  avg ccache hit: %.0f%%\n", h/hc; }' "$LOG"
  echo "  recent:"
  tail -n 5 "$LOG" | awk -F'\t' '{printf "    %s  %-16s %ss  ccache=%s%%\n", $1, $3"/"$4, $5, ($6==""?"-":$6)}'
  if [ "${1:-}" = "--graph" ]; then
    local svg="$STATE_DIR/build-durations.svg"
    awk -F'\t' 'NR>1{v[n++]=$5} END{
      w=600; h=140; max=1; for(i=0;i<n;i++) if(v[i]>max) max=v[i];
      print "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""w"\" height=\""h"\">";
      print "<rect width=\""w"\" height=\""h"\" fill=\"#F5F5F2\"/>";
      bw = (n>0? w/n : w);
      for(i=0;i<n;i++){ bh=(v[i]/max)*(h-20); x=i*bw; y=h-bh;
        printf "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" fill=\"#54A2CC\"/>\n", x, y, bw*0.8, bh; }
      print "</svg>"; }' "$LOG" > "$svg"
    echo "  graph: $svg"
  fi
}

usage() {
  cat <<'EOF'
ksc-dev — Kartoza Screencaster developer command

  ksc-dev build [target...]     Configure + build (Debug), log metrics
  ksc-dev release [target...]   Configure + build (Release)
  ksc-dev run                   Run the application
  ksc-dev test [-R name]        Run tests (ctest)
  ksc-dev configure [-D...]     Re-run cmake with extra flags
  ksc-dev format                clang-format all C++ sources
  ksc-dev clean                 Clean rebuild from scratch
  ksc-dev docs [serve|build|pdf] mkdocs serve (default) / build / branded PDF handbook
  ksc-dev stats [--graph]       Show build metrics (ccache hit rate, durations)
  ksc-dev help                  This help

Fast dev loop: ccache + mold + Ninja (auto-detected by CMake).
EOF
}

cmd="${1:-help}"; shift || true
case "$cmd" in
  build)     do_build debug "$@" ;;
  release)   do_build release "$@" ;;
  run)       "$BUILD/kartoza-screencaster" "$@" ;;
  test)      ( cd "$BUILD" && ctest --output-on-failure "$@" ) ;;
  configure) ( cd "$BUILD" && cmake .. -G Ninja "$@" ) ;;
  format)    ( cd "$ROOT" && find src tests \( -name '*.cpp' -o -name '*.h' \) -print0 | xargs -0 clang-format -i ) ;;
  clean)     rm -rf "${BUILD:?}/"* && do_build debug ;;
  docs)      case "${1:-serve}" in
               build) ( cd "$ROOT" && mkdocs build ) ;;
               pdf)   ( cd "$ROOT" && nix run .#handbook-pdf -- "${2:-}" ) ;;
               *)     ( cd "$ROOT" && mkdocs serve ) ;;
             esac ;;
  stats)     cmd_stats "$@" ;;
  help|-h|--help) usage ;;
  *) echo "ksc-dev: unknown command '$cmd'" >&2; usage; exit 2 ;;
esac
