---
hide:
  - navigation
  - toc
---
<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

<div class="kz-hero" markdown>

<span class="kz-eyebrow">KARTOZA · SCREENCASTER</span>

[![Latest release](https://img.shields.io/github/v/release/kartoza/kartoza-screencaster?label=release&color=blue)](https://github.com/kartoza/kartoza-screencaster/releases/latest)
[![Release date](https://img.shields.io/github/release-date/kartoza/kartoza-screencaster?color=informational)](https://github.com/kartoza/kartoza-screencaster/releases/latest)
[![License: MIT](https://img.shields.io/github/license/kartoza/kartoza-screencaster)](https://github.com/kartoza/kartoza-screencaster/blob/main/LICENSE)

# Record. Don't edit.

A screen recorder that takes the post-production out of the loop.
Compose the whole shot before you press record — screen, webcam,
logos, intro music — and what you see is what gets saved.

<div class="kz-cta" markdown>
[:material-rocket-launch: Get started](getting-started/index.md){ .kz-cta__primary }
[:material-book-open-page-variant: User guide](user-guide/index.md){ .kz-cta__secondary }
[:simple-github: GitHub](https://github.com/kartoza/kartoza-screencaster){ .kz-cta__secondary }
</div>

</div>

## Why it exists

Most screen recorders give you a raw clip and leave the rest to a
video editor. That editing step is where projects stall — open the
NLE, set up tracks, crop, position the webcam, add the lower-third,
sync the intro music, fade in, fade out, export, upload.

Kartoza Screencaster collapses that whole loop into the **moment
before you press record**. Lay your screen, your webcam, your logo
and your intro/outro sound out on a canvas, choose whether you're
recording in **landscape** for YouTube or **vertical** for Shorts /
Reels / TikTok, then hit Record. When you stop, you have an MP4
that's ready to ship.

If you need an editor afterward, the raw streams are all there too.
But the common case — recording a tutorial, a quick demo, a piece of
explanation — should never need one.

## What you can do with it

- **Works on every common desktop**: Linux, macOS, and Windows. On
  Linux that includes both modern GNOME and KDE sessions as well as
  the more traditional setups.
- **Record vertical or horizontal** by switching layouts in one click.
  Vertical layouts are designed for Shorts, Reels and TikTok; you
  don't need to re-crop afterward.
- **Compose live**: the canvas shows your final shot before you've
  recorded anything. Move the webcam, resize the logo, swap the
  intro sting — what you arrange is what you get.
- **Stop, upload, done**: when the recording finishes, one click
  publishes it to YouTube with the title and description you've
  already set.

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

    `.tar.gz` — distribution-neutral build for advanced users.

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

-   :material-view-quilt:{ .lg .middle } __Compose before you record__

    ---

    Lay out your screen, webcam, logos and sound effects on a canvas
    and arrange them exactly how you want. The preview is your
    finished video — no surprises after you press stop.

    [:octicons-arrow-right-24: Canvas editor](user-guide/canvas.md)

-   :material-phone-rotate-portrait:{ .lg .middle } __Landscape and vertical__

    ---

    Switch between a wide YouTube layout and a tall Shorts / Reels /
    TikTok layout with a single click. Recordings come out the right
    shape and resolution for the platform you're publishing to.

    [:octicons-arrow-right-24: Canvas editor](user-guide/canvas.md)

-   :material-webcam:{ .lg .middle } __Webcam, your way__

    ---

    Drop a round, square or rectangle webcam thumbnail anywhere on
    the canvas and resize to taste. Live preview before you record
    means no awkward "oh wait, my face is cut off."

    [:octicons-arrow-right-24: Webcam](user-guide/webcam.md)

-   :material-image-multiple:{ .lg .middle } __Brand it once__

    ---

    Place your logo, animated GIF reactions, and lower-thirds, then
    save the arrangement as a preset. Every future recording starts
    fully branded.

    [:octicons-arrow-right-24: Logos and GIFs](user-guide/overlays.md)

-   :material-history:{ .lg .middle } __Local history + replay__

    ---

    Every recording lives on your computer with a title, description,
    and one-click playback. Rename, reorganise, or upload later —
    nothing leaves your machine unless you choose.

    [:octicons-arrow-right-24: History](user-guide/history.md)

-   :material-youtube:{ .lg .middle } __Stop, click, published__

    ---

    Link your YouTube account once. From then on, any recording can
    be on YouTube with one click — title, description, privacy and
    category already filled in.

    [:octicons-arrow-right-24: YouTube upload](user-guide/youtube.md)

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
