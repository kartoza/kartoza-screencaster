<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# System dependencies

Kartoza Screencaster needs Qt 6, ffmpeg, and one of the screen-capture
backends for your session type.

## Always required

- **Qt 6** — Core, Widgets, Multimedia, Svg, Network, Concurrent. DBus
  on Linux.
- **ffmpeg** — used for audio recording, post-processing, denoising,
  merging, and the X11 capture path.

## Linux Wayland

Pick one based on the compositor:

| Compositor family                  | Tool                       |
| ---------------------------------- | -------------------------- |
| wlroots (Hyprland, Sway, COSMIC)   | `wl-screenrec`             |
| GNOME (Mutter), KDE (KWin)         | xdg-desktop-portal + PipeWire + GStreamer |

For the portal path you also need GStreamer with `pipewiresrc`
(provided by the `pipewire` GStreamer plugin) and an H.264 encoder
(`openh264enc` from `gst-plugins-bad` is recommended; `x264enc` from
`gst-plugins-ugly` is an alternative).

## Linux X11

Just ffmpeg's `x11grab` muxer (bundled with any standard ffmpeg) plus
`xrandr` for monitor enumeration.

## macOS

ffmpeg's `avfoundation` input device — bundled with the brew package.

## Windows

ffmpeg's `gdigrab` input device — bundled with the Qt-installer
ffmpeg or with any standard Windows ffmpeg distribution.

## Diagnostics

Run `diagnose.sh` (shipped in the repo) for a one-shot report on what's
installed, which capture path the app will pick, and which
GStreamer / ffmpeg encoders / muxers are reachable on the current
session.
