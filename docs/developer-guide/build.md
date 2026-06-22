<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Build

CMake-based. Out-of-source builds in `build/`.

## Quick start

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The output binary lands at `build/kartoza-screencaster`. Inside the
dev shell, `cb` does all of the above.

## Tests

```bash
cd build && ctest --output-on-failure
```

CI runs the same command on Ubuntu 24.04 / macOS / Windows.

## Release build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
strip build/kartoza-screencaster
```

The `release.yml` GitHub workflow runs this for every tag and uploads
artefacts to the release page.

## Toggles

| CMake variable                  | Effect                                              |
| ------------------------------- | --------------------------------------------------- |
| `CMAKE_BUILD_TYPE=Debug`        | Assertions on, optimisation off, symbols.           |
| `CMAKE_BUILD_TYPE=Release`      | Optimised, no debug symbols (strip after).          |
| `CMAKE_BUILD_TYPE=RelWithDebInfo` | Optimised, symbols kept.                          |

D-Bus / portal is gated by `find_package(Qt6 COMPONENTS DBus)` on
Linux only — the Windows and macOS builds do not compile the portal
module.
