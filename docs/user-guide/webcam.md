<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Webcam overlay

!!! tip "Walkthrough pending"
    Walkthrough frames will land here.

Drop a webcam onto the canvas to compose a live thumbnail into the
recording. Shape options: **round bubble**, **square**, or
**rectangle**. Drag to position; corner-handle to resize.

The preview is live — what you see on the canvas before pressing
Record is exactly what lands in the MP4.

## Performance

The recorder uses `ffmpeg` with the appropriate platform backend
(`v4l2` on Linux, `avfoundation` on macOS, `dshow` on Windows). The
preview is sampled at 5 FPS to keep the UI responsive; the final
recording captures at 30 FPS.

## Privacy

The webcam is only opened when the canvas widget is visible. Launching
straight into the tray with `--tray` leaves the device closed (and
the indicator LED off) until you open the main window.
