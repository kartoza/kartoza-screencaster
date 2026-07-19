<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Dev shell

The repo ships a Nix flake that provisions everything CI uses, pinned
to the same versions.

## Entering the shell

```bash
nix develop
```

You land in a shell with `cmake`, `ninja`, `ccache`, `mold`, `gcc`, Qt 6,
GStreamer + plugins, ffmpeg, `mkdocs`, `clang-format`, `gdb`, `valgrind`, and a
handful of project helpers.

## Helper aliases

| Alias    | What it does                              |
| -------- | ----------------------------------------- |
| `cb`     | Configure + build (Debug).                |
| `cbr`    | Configure + build (Release, stripped).    |
| `ct`     | Run all tests.                            |
| `ctr`    | Run merger tests + play renders for review.|
| `cr`     | Run the application.                      |
| `cclean` | Clean rebuild from scratch.               |
| `cf`     | Format all C++ code (clang-format).       |
| `docs`   | Serve mkdocs (`localhost:8000`).          |

## Unified command: `ksc-dev`

Prefer one entry point? `ksc-dev` wraps the whole loop and records build
metrics, mirroring `qgis-dev` from
[`qgis-dev-env`](https://github.com/timlinux/qgis-dev-env):

```bash
ksc-dev build            # configure + build (Debug), log duration + ccache hit rate
ksc-dev release          # Release build
ksc-dev run              # run the app
ksc-dev test -R canvas   # ctest (optionally filtered)
ksc-dev format           # clang-format all sources
ksc-dev clean            # clean rebuild
ksc-dev docs [build]     # mkdocs serve (default) or build
ksc-dev stats [--graph]  # build history: avg time, ccache hit rate, recent builds
ksc-dev help
```

Each `ksc-dev build` appends a row to
`${XDG_STATE_HOME:-~/.local/state}/kartoza-screencaster/build-log.tsv`
(timestamp, branch, profile, target, duration, ccache hit %). `ksc-dev stats`
summarises it; `--graph` also writes a small SVG of recent build durations. The
single-letter aliases above (`cb`, `ct`, …) remain for muscle memory.

## Build acceleration (ccache + mold + Ninja)

The dev loop is tuned for fast iterative builds. Three tools do the heavy
lifting, and all three are wired up automatically — you don't configure
anything:

| Tool       | Role                          | How it's wired |
| ---------- | ----------------------------- | -------------- |
| **Ninja**  | Build system / job scheduler  | `cmake -G Ninja` (used by every `cb`/`cbr`). Runs compile/link jobs in parallel with minimal overhead — far faster incremental builds than Make. |
| **ccache** | Compiler cache                | CMake sets `CMAKE_CXX_COMPILER_LAUNCHER=ccache` when `ccache` is found. Unchanged translation units are served from cache, so re-compiles after a branch switch or a header touch are near-instant. |
| **mold**   | Linker                        | CMake adds `-fuse-ld=mold` when `mold` is found. Linking a Qt/C++ target is often the slowest step of an incremental build; mold links several times faster than GNU `ld`/`gold`. |

Together these give the *"edit → build → run"* loop the same shape as
[`qgis-dev-env`](https://github.com/timlinux/qgis-dev-env): **ccache + mold +
Ninja**.

Both ccache and mold are **optional and auto-detected** — CMake prints
`Using ccache: …` / `Using mold linker: …` at configure time when they're
present, and silently falls back to the default compiler/linker when they're
not. So a build outside the Nix shell still works; it's just slower.

Useful checks:

```bash
ccache -s      # cache hit/miss statistics
mold --version # confirm the linker is available
```

A clean `cclean` rebuild warms the ccache; subsequent incremental builds should
show a high ccache hit rate and sub-second link times via mold.

## Without Nix

You can build without Nix — install Qt 6, ffmpeg, GStreamer + plugins
from your distro and follow [Build](build.md). The flake just removes
the per-developer setup variance.
