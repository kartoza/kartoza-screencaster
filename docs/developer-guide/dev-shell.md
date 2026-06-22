<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Dev shell

The repo ships a Nix flake that provisions everything CI uses, pinned
to the same versions.

## Entering the shell

```bash
nix develop
```

You land in a shell with `cmake`, `ninja`, `gcc`, Qt 6, GStreamer +
plugins, ffmpeg, `mkdocs`, `clang-format`, `gdb`, `valgrind`, and a
handful of project helpers.

## Helper aliases

| Alias    | What it does                              |
| -------- | ----------------------------------------- |
| `cb`     | Configure + build (Debug).                |
| `cbr`    | Configure + build (Release, stripped).    |
| `ct`     | Run all tests.                            |
| `cr`     | Run the application.                      |
| `cclean` | Clean rebuild from scratch.               |
| `cf`     | Format all C++ code (clang-format).       |
| `docs`   | Serve mkdocs (`localhost:8000`).          |

## Without Nix

You can build without Nix — install Qt 6, ffmpeg, GStreamer + plugins
from your distro and follow [Build](build.md). The flake just removes
the per-developer setup variance.
