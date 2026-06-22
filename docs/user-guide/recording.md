<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Recording

!!! tip "Walkthrough pending"
    This page will be expanded with frames from the walkthrough video.

## Start, pause, resume, stop

Click the red **Record** button to start. A 5-second countdown gives
you a moment to compose yourself. While recording you can:

- **Pause** — captures freeze; the file is split into a new part on
  resume.
- **Resume** — continues into a new part. The merger stitches parts
  back into a single file at the end.
- **Stop** — finalises the file and kicks off post-processing.

## Tray mode

Launching with `--tray` (or via the desktop entry) starts the app in
the system tray. Left-click the tray icon to start / stop; right-click
for the full menu.

## Multi-part recording

Pause / resume creates `_part000`, `_part001`, … files that the merger
re-encodes into a single timeline. Sync timestamps are stored in
`recording.json` so audio and webcam stay aligned with the screen.

## Post-processing

When you click Stop, the recorder kicks off:

1. **Room noise capture** — 5 seconds of ambient audio for the noise
   profile.
2. **Denoise** — applies `afftdn` against the noise profile.
3. **Sound effects** — mixes intro / outro stings into the audio track.
4. **Merge** — stitches all parts into a single MP4 with overlays
   composited in.

The merged file appears in the **History** tab.
