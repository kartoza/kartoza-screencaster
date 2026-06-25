<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Install

Kartoza Screencaster ships pre-built artefacts for every release. Pick
the one that matches your platform; each section below also lists the
runtime dependencies you'll need so you can install them by hand if
the package manager doesn't pull them in for you.

The Linux app is a Qt 6 desktop application that records the screen
through `wl-screenrec` (wlroots compositors), `xdg-desktop-portal`
(GNOME, KDE), or `ffmpeg x11grab` (X11) depending on your session.
You need a working Qt 6 runtime and the matching capture backend.

## Debian / Ubuntu (and derivatives)

### 1. Enable `universe` and `multiverse`

Most of the runtime dependencies live in `universe`. If you've never
enabled the extra Ubuntu repositories, do that first:

```bash
sudo add-apt-repository universe
sudo add-apt-repository multiverse
sudo apt update
```

On Debian the equivalent components are already enabled by default in
`/etc/apt/sources.list`.

### 2. Install the `.deb`

```bash
# Download the latest .deb from the release page, then:
sudo apt install ./kartoza-screencaster_<version>_amd64.deb
```

`apt install ./file.deb` (note the leading `./`) makes apt resolve
the dependencies from your repositories — `dpkg -i` on its own will
not do that and will leave you with broken dependencies.

### 3. If the .deb won't resolve (2.0.0 on Ubuntu 26.04+)

The 2.0.0 `.deb` was built on Ubuntu 24.04 during the time_t 64-bit
transition and hard-pins the `t64`-suffixed Qt library names
(`libqt6gui6t64`, `libqt6widgets6t64`, …). On Ubuntu 26.04 the
transition is complete and those names have been reverted to the
originals (`libqt6gui6`, `libqt6widgets6`), so `apt` cannot satisfy
the hard dependencies and you'll see:

```
kartoza-screencaster : Depends: libqt6gui6t64 but it is not installable
                       Depends: libqt6widgets6t64 but it is not installable
                       …
```

The fix is shipped in 2.0.1. Until you can grab that release, install
the dependencies by hand and then force-install the package:

```bash
# 1. Install every runtime dependency by name.
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

# Optional: wlroots-only recording helpers (Hyprland, Sway, COSMIC).
sudo apt install wl-screenrec grim

# Optional: pick the KDE portal backend if you're running KDE Plasma.
sudo apt install xdg-desktop-portal-kde

# 2. Force-install the .deb ignoring the obsolete t64 names.
sudo dpkg -i --ignore-depends=libqt6core6t64,libqt6gui6t64,libqt6widgets6t64,libqt6dbus6t64,libqt6network6t64 \
    ./kartoza-screencaster_2.0.0_amd64.deb
```

The 2.0.1 release uses `libqt6gui6 | libqt6gui6t64` alternatives so
either name satisfies the dependency, and the workaround above is
no longer needed.

### Per-distro version notes

| Release                          | Qt6 package names                | Notes |
| -------------------------------- | -------------------------------- | ----- |
| Ubuntu 22.04 LTS, Debian 12      | `libqt6gui6`, `libqt6widgets6`   | `wl-screenrec` is not in the default repos — install from source if you need wlroots capture. |
| Ubuntu 24.04 LTS                 | `libqt6gui6t64`, …               | `t64` transition is in flight; both names may coexist via virtual packages. |
| Ubuntu 24.10, 25.04, 25.10       | `libqt6gui6t64`, …               | Same as 24.04. |
| Ubuntu 26.04, Debian 13+         | `libqt6gui6`, `libqt6widgets6`   | `t64` transition complete; the suffixed names are gone. This is the case that breaks the 2.0.0 `.deb` — use 2.0.1 or follow the manual steps above. |

## Fedora / RHEL

```bash
sudo dnf install ./kartoza-screencaster-<version>-1.x86_64.rpm
```

If you'd rather install the dependencies manually (e.g. to do a
sandboxed install with the tarball), the package list is:

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

The `ffmpeg-free` package is the Fedora-blessed build; if you need the
full ffmpeg (with non-free codecs) enable RPM Fusion and use `ffmpeg`
instead.

## Arch Linux

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

A binary `.tar.gz` will then run as `./kartoza-screencaster`.

## Linux tarball (distribution-neutral)

For systems where the `.deb` or `.rpm` isn't a fit:

```bash
tar -xzf kartoza-screencaster-linux-x86_64.tar.gz
sudo install -m 755 kartoza-screencaster /usr/local/bin/
```

Install the runtime dependencies for your distribution from the
sections above. If anything's missing on first launch the app prints a
diagnostic message naming the missing tool.

## macOS

```bash
tar -xzf kartoza-screencaster-macos-arm64.tar.gz
mv kartoza-screencaster /Applications/
```

Screen recording on macOS uses `ffmpeg`'s `avfoundation` input device.
Install ffmpeg via Homebrew if you don't already have it:

```bash
brew install ffmpeg
```

Grant the app screen-recording permission the first time you launch
it: *System Settings → Privacy & Security → Screen Recording*.

## Windows

Unzip `kartoza-screencaster-windows-x86_64.zip` and run
`kartoza-screencaster.exe`. The Qt runtime and ffmpeg are bundled
in the zip — no extra install steps.

## Nix flake

```bash
nix run github:kartoza/kartoza-screencaster
```

Every runtime dependency (Qt 6, ffmpeg, the GStreamer stack,
PipeWire, the portal backends) is wrapped into the binary's
`PATH` and `GST_PLUGIN_SYSTEM_PATH_1_0` by the flake, so there is
nothing to install at the system level. See the
[Developer guide → Dev shell](../developer-guide/dev-shell.md) for
the full reproducible toolchain.

## Verifying the install

The repo ships `diagnose.sh`, a one-shot report that prints which
capture path the app will pick on this session, which GStreamer /
ffmpeg encoders are reachable, and what's missing if anything:

```bash
git clone https://github.com/kartoza/kartoza-screencaster.git
cd kartoza-screencaster
./diagnose.sh
```

You don't need a full build — the script only reads system state.

System administrators preparing a fleet image: the full annotated
dependency list lives in the
[Administrator Guide → System dependencies](../admin-guide/dependencies.md).
