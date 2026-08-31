<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Capture pipelines

Kartoza Screencaster has three screen-capture paths. Which one runs is
decided at runtime by `Platform::compositor()` and
`Platform::supportsWlrCapture()`.

## wlroots (Hyprland, Sway, Wayfire, River, Niri, …)

```text
wl-screenrec --output <name> --filename <path>
```

`wl-screenrec` reads frames over the `wlr-screencopy` protocol. Single
process, hardware-accelerated encode where supported, near-zero CPU on
desktop GPUs. If it exits early (e.g. a VAAPI init failure) the recorder
falls back once to `wf-recorder` with software libx264.

Both tools require `wlr-screencopy-unstable-v1`, so this path is strictly
for wlroots-family compositors. **COSMIC is not one of them** despite its
lineage — it implements its own capture protocol, and `wf-recorder` fails
there with `compositor doesn't support wlr-screencopy-unstable-v1`. COSMIC
uses the portal path below.

## GNOME / KDE / COSMIC Wayland — xdg-desktop-portal + PipeWire

```text
gst-launch-1.0 -e \
    pipewiresrc path=<node> fd=<portal-fd> do-timestamp=true \
    ! videoconvert \
    ! openh264enc bitrate=8000000 complexity=medium rate-control=bitrate \
    ! h264parse \
    ! mp4mux fragment-duration=1000 \
    ! filesink location=<path>
```

Needs a portal backend for the desktop: `xdg-desktop-portal-gnome`,
`xdg-desktop-portal-kde` or `xdg-desktop-portal-cosmic`. Because the portal
picks a single source, only the primary monitor is captured — additional
monitor insets are dropped on this path.

The portal flow runs CreateSession → SelectSources → Start →
OpenPipeWireRemote asynchronously. The PipeWire node id and the
private connection FD come back from Start's response and the
OpenPipeWireRemote call respectively. The FD is `dup`'d, CLOEXEC
cleared, and `setChildProcessModifier` `dup2`'s it onto a fixed slot
(23) in the forked `gst-launch` child before `exec()` — that's how
`pipewiresrc fd=23` reaches the stream.

## X11

```text
ffmpeg -y -f x11grab \
    -framerate 30 \
    -video_size <WxH> -i <display>+<x>,<y> \
    -c:v libx264 -preset ultrafast -crf 18 -pix_fmt yuv420p \
    <path>
```

Classic `x11grab`. Monitor geometry comes from `xrandr` (or `xdpyinfo`
as a fallback).

## Stop semantics

All three pipelines react to `SIGINT` for a clean shutdown — the
`gst-launch -e` flag is what makes the EOS propagate so `mp4mux`
finalises the moov atom. The `Recorder::stopProcess` helper sends
`SIGINT` first, escalates to `terminate()` after 5 seconds, then
`kill()` after another 3.
