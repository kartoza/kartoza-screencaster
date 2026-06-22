---
hide:
  - navigation
  - toc
---
<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

<div class="kz-hero" markdown>

<span class="kz-eyebrow">KARTOZA · SCREENCASTER</span>

# A Qt screen recorder for Linux, macOS and Windows

Compose your shot on a WYSIWYG canvas. Drop in a webcam, logos, an intro
sound. Hit record. Ship straight to YouTube.

<div class="kz-cta" markdown>
[:material-rocket-launch: Get started](getting-started/index.md){ .kz-cta__primary }
[:material-book-open-page-variant: User guide](user-guide/index.md){ .kz-cta__secondary }
[:simple-github: GitHub](https://github.com/kartoza/kartoza-screencaster){ .kz-cta__secondary }
</div>

</div>

## What it is

Kartoza Screencaster is a desktop screen recorder built around a
**what-you-see-is-what-you-get canvas**. Lay out your screen, your
webcam, intro and outro sounds, your logos and reactions — then record
the composed view in one pass. No post-production crop, no manual
overlay tracks.

The recorder captures:

- **Screen** — via wlroots (`wl-screenrec`), xdg-desktop-portal
  (PipeWire) on GNOME / KDE Wayland, or `ffmpeg x11grab` on X11.
- **Audio** — PulseAudio / PipeWire on Linux, system audio on macOS,
  WASAPI on Windows.
- **Webcam** — V4L2 on Linux, AVFoundation on macOS, DirectShow on
  Windows.

And produces a single, encoded MP4 ready to share — with one-click
upload to YouTube once you've authorised an account.

## Download

<div class="grid cards" markdown>

-   :material-debian:{ .lg .middle } __Debian / Ubuntu__

    ---

    `.deb` package — Ubuntu 24.04 amd64 and derivatives.

    [:octicons-download-24: Latest release](https://github.com/kartoza/kartoza-screencaster/releases/latest)

-   :material-redhat:{ .lg .middle } __Fedora / RHEL__

    ---

    `.rpm` package — Fedora 39+, RHEL 9+ and derivatives.

    [:octicons-download-24: Latest release](https://github.com/kartoza/kartoza-screencaster/releases/latest)

-   :material-linux:{ .lg .middle } __Linux tarball__

    ---

    `.tar.gz` — distribution-neutral, requires Qt 6 and ffmpeg.

    [:octicons-download-24: Latest release](https://github.com/kartoza/kartoza-screencaster/releases/latest)

-   :material-apple:{ .lg .middle } __macOS__

    ---

    `.tar.gz` — arm64 universal binary.

    [:octicons-download-24: Latest release](https://github.com/kartoza/kartoza-screencaster/releases/latest)

-   :material-microsoft-windows:{ .lg .middle } __Windows__

    ---

    `.zip` — x86_64, MSVC build.

    [:octicons-download-24: Latest release](https://github.com/kartoza/kartoza-screencaster/releases/latest)

-   :material-snowflake:{ .lg .middle } __Nix / NixOS__

    ---

    Run straight from the flake: `nix run github:kartoza/kartoza-screencaster`.

    [:octicons-arrow-right-24: Dev shell](developer-guide/dev-shell.md)

</div>

## What's in the box

<div class="grid cards" markdown>

-   :material-view-quilt:{ .lg .middle } __WYSIWYG canvas__

    ---

    Drag your screen, webcam, logos, GIFs and sounds onto the same
    canvas as the recording. What you compose is exactly what comes
    out the other end.

    [:octicons-arrow-right-24: Canvas editor](user-guide/canvas.md)

-   :material-record-circle:{ .lg .middle } __Multi-compositor capture__

    ---

    Recording works on wlroots Wayland, GNOME and KDE Wayland (via the
    xdg-desktop-portal), and X11. The recorder picks the right path
    automatically from your session.

    [:octicons-arrow-right-24: Capture pipelines](developer-guide/capture-pipelines.md)

-   :material-webcam:{ .lg .middle } __Webcam overlay__

    ---

    Round, square or rectangle webcam thumbnail with live preview on
    the canvas before you start recording. Position anywhere.

    [:octicons-arrow-right-24: Webcam](user-guide/webcam.md)

-   :material-image-multiple:{ .lg .middle } __Logos and GIFs__

    ---

    Multiple overlays. Animated GIFs with loop limits. Brand it once,
    reuse the canvas across every recording.

    [:octicons-arrow-right-24: Logos and GIFs](user-guide/overlays.md)

-   :material-history:{ .lg .middle } __Local history + playback__

    ---

    Every recording lands in `~/Videos/Screencasts/` with metadata.
    Browse, replay, rename or upload later from the History tab.

    [:octicons-arrow-right-24: History](user-guide/history.md)

-   :material-youtube:{ .lg .middle } __One-click YouTube upload__

    ---

    OAuth your YouTube account once, then upload from any recording
    with a title, description, privacy and category — straight from
    the app.

    [:octicons-arrow-right-24: YouTube upload](user-guide/youtube.md)

-   :material-code-tags:{ .lg .middle } __C++ API reference__

    ---

    Full Doxygen-generated browser of every class, function and macro
    the codebase exposes. Published alongside this site.

    [:octicons-arrow-right-24: API reference](developer-guide/api.md)

</div>

## QA status

[![CI](https://github.com/kartoza/kartoza-screencaster/actions/workflows/ci.yml/badge.svg)](https://github.com/kartoza/kartoza-screencaster/actions/workflows/ci.yml)
[![Docs](https://github.com/kartoza/kartoza-screencaster/actions/workflows/Docs.yml/badge.svg)](https://github.com/kartoza/kartoza-screencaster/actions/workflows/Docs.yml)
[![Release](https://github.com/kartoza/kartoza-screencaster/actions/workflows/release.yml/badge.svg)](https://github.com/kartoza/kartoza-screencaster/actions/workflows/release.yml)

<div class="kz-footer-credits" markdown>
Made with 💗 by [Kartoza](https://kartoza.com) &middot;
[Sponsor on GitHub](https://github.com/sponsors/kartoza) &middot;
[Repository](https://github.com/kartoza/kartoza-screencaster)
</div>
