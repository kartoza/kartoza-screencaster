<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# System dependencies

Kartoza Screencaster needs Qt 6, ffmpeg, and one of the screen-capture
backends for your session type. The `.deb` and `.rpm` packages declare
all of these as hard dependencies so apt/dnf will pull them in for you;
this page is for the cases where you're rolling a fleet image, building
from the tarball, or working out why the package manager can't satisfy
the dependency.

## Always required

- **Qt 6** — Core, Widgets, Multimedia, Svg, Network, Concurrent. DBus
  on Linux.
- **ffmpeg** — used for audio recording, post-processing, denoising,
  merging, and the X11 capture path.
- **libnotify** — desktop notifications when a recording starts /
  stops / finishes encoding.

## Linux Wayland

Pick one based on the compositor:

| Compositor family                  | Capture path                                |
| ---------------------------------- | ------------------------------------------- |
| wlroots (Hyprland, Sway, Wayfire, …) | `wl-screenrec`, falling back to `wf-recorder` |
| GNOME (Mutter), KDE (KWin), COSMIC   | `xdg-desktop-portal` + PipeWire + GStreamer   |

For the portal path you also need GStreamer with `pipewiresrc`
(provided by the `pipewire` GStreamer plugin) and an H.264 encoder
(`openh264enc` from `gst-plugins-bad` is recommended; `x264enc` from
`gst-plugins-ugly` is an alternative). You also need a portal backend
matching your desktop — `xdg-desktop-portal-gnome` on GNOME,
`xdg-desktop-portal-kde` on KDE Plasma, `xdg-desktop-portal-cosmic` on
COSMIC.

COSMIC belongs on the portal row despite its wlroots-adjacent lineage: it
does not implement `wlr-screencopy-unstable-v1`, so `wl-screenrec` and
`wf-recorder` cannot capture there.

The Qt Wayland platform plugin (`qt6-wayland` on apt / `qt6-qtwayland`
on dnf / `qt6-wayland` on pacman) is required for the GUI to render on
a pure-Wayland session. Without it Qt falls back to XCB and needs
XWayland to be running.

## Linux X11

Just ffmpeg's `x11grab` muxer (bundled with any standard ffmpeg) plus
`xrandr` for monitor enumeration (`x11-xserver-utils` on apt,
`xorg-x11-server-utils` on dnf, `xorg-xrandr` on pacman).

## Concrete install commands

### Debian / Ubuntu

```bash
sudo apt install \
    ffmpeg \
    libqt6core6 libqt6gui6 libqt6widgets6 \
    libqt6multimedia6 libqt6dbus6 libqt6svg6 libqt6network6 \
    qt6-wayland \
    xdg-desktop-portal xdg-desktop-portal-gnome \
    pipewire pipewire-pulse \
    gstreamer1.0-tools \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-libav \
    gstreamer1.0-pipewire \
    x11-xserver-utils libnotify-bin

# wlroots compositors (Hyprland, Sway, Wayfire, …):
sudo apt install wl-screenrec wf-recorder grim

# COSMIC uses the portal path, not wl-screenrec:
sudo apt install xdg-desktop-portal-cosmic

# KDE Plasma:
sudo apt install xdg-desktop-portal-kde
```

On Ubuntu 24.04, 24.10, 25.04 and 25.10 the Qt library names carry a
`t64` suffix (`libqt6gui6t64`, …) because of the in-flight time_t
64-bit transition. Both names are virtual-package aliases on the
transition releases; on 22.04 LTS and 26.04+ the suffix is gone. The
release `.deb` declares them as `libqt6X6 | libqt6X6t64` alternatives
so a single artefact installs cleanly across the whole range.

### Fedora / RHEL

```bash
sudo dnf install \
    ffmpeg-free \
    qt6-qtbase qt6-qtbase-gui \
    qt6-qtmultimedia qt6-qtsvg qt6-qtnetworkauth \
    qt6-qtwayland \
    xdg-desktop-portal xdg-desktop-portal-gnome \
    pipewire pipewire-gstreamer \
    gstreamer1 gstreamer1-plugins-base \
    gstreamer1-plugins-good gstreamer1-plugins-bad-free \
    gstreamer1-libav \
    xorg-x11-server-utils libnotify
```

Use the `ffmpeg` package from RPM Fusion instead of `ffmpeg-free` if
you need the non-free codec set.

### Arch Linux

```bash
sudo pacman -S \
    ffmpeg \
    qt6-base qt6-multimedia qt6-svg qt6-wayland \
    xdg-desktop-portal xdg-desktop-portal-gnome \
    pipewire pipewire-pulse \
    gstreamer gst-plugins-base gst-plugins-good \
    gst-plugins-bad gst-libav gst-plugin-pipewire \
    xorg-xrandr libnotify
```

### Nix

Use the flake's dev shell or `nix run`. Every dependency above is
wrapped into the binary's `PATH` and `GST_PLUGIN_SYSTEM_PATH_1_0`,
so nothing has to be installed system-wide.

## macOS

ffmpeg's `avfoundation` input device — bundled with the Homebrew
`ffmpeg` formula. The recording binary itself needs *System Settings →
Privacy & Security → Screen Recording* to be granted at first launch.

## Windows

ffmpeg's `gdigrab` input device — bundled with the Qt-installer
ffmpeg or with any standard Windows ffmpeg distribution. The release
zip is self-contained.

## Diagnostics

Run `diagnose.sh` (shipped in the repo) for a one-shot report on
what's installed, which capture path the app will pick, and which
GStreamer / ffmpeg encoders / muxers are reachable on the current
session.
