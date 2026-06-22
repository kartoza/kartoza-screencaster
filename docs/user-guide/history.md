<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# History and playback

!!! tip "Walkthrough pending"
    Walkthrough frames will land here.

Every recording lives in `~/Videos/Screencasts/`. The **History** tab
lists them by date.

## Folder layout

```
~/Videos/Screencasts/
└── 001-recording_20260621_163533/
    ├── screen_part000.mp4       Raw screen capture parts
    ├── audio_part000.wav        Raw audio parts
    ├── webcam_part000.mp4       Raw webcam parts
    ├── room_noise.wav           5 s ambient capture for denoising
    ├── merged.mp4               Composited, denoised output
    ├── recording.json           Metadata (title, duration, parts, sync)
    └── assets/                  Copies of logos / GIFs / sound effects used
```

## Replay

Click a row to open the recording. Hit **Play** to launch the system's
default player on `merged.mp4`.

## Rename

Each recording carries a **title** and **description** in
`recording.json`. The History tab lets you edit both in place; the
folder name is regenerated to include the slugged title.

## Upload

The **Upload to YouTube** button is enabled for any recording that has
a `merged.mp4`. See [YouTube upload](youtube.md).
