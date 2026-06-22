<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Your first recording

!!! tip "Walkthrough pending"
    This page will be rebuilt from a screen-recording walkthrough.
    Frames will live under `docs/assets/walkthrough/` and be referenced
    here with the `kz-screenshot` class for the brand-styled border.

## In thirty seconds

1. Launch **Kartoza Screencaster**.
2. Click **Add screen** and pick the monitor you want to capture.
3. Optionally drag in a webcam, a logo, an intro sound.
4. Click the red **Record** button.
5. When you're done, click **Stop**.

Your recording is now under `~/Videos/Screencasts/` with a timestamped
folder name. Open the **History** tab to play it back, rename it, or
upload it to YouTube.

## What just happened

The recorder picked one of three capture pipelines based on your
session — `wl-screenrec` on wlroots, the xdg-desktop-portal +
PipeWire path on GNOME/KDE Wayland, or `ffmpeg x11grab` on X11. See
[Developer guide → Capture pipelines](../developer-guide/capture-pipelines.md)
for the gory details.
