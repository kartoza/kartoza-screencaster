<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Capture pipelines

Kartoza Screencaster has three screen-capture paths. Which one runs is
decided at runtime by `Platform::compositor()` and
`Platform::supportsWlrCapture()`.

## wlroots (Hyprland, Sway, COSMIC, Wayfire, …)

```text
wl-screenrec --output <name> --filename <path>
```

`wl-screenrec` reads frames over the `wlr-screencopy` protocol. Single
process, hardware-accelerated encode where supported, near-zero CPU on
desktop GPUs.

## GNOME / KDE Wayland — xdg-desktop-portal + PipeWire

```text
gst-launch-1.0 -e \
    pipewiresrc path=<node> fd=<portal-fd> do-timestamp=true \
    ! videoconvert \
    ! openh264enc bitrate=8000000 complexity=medium rate-control=bitrate \
    ! h264parse \
    ! mp4mux fragment-duration=1000 \
    ! filesink location=<path>
```

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
