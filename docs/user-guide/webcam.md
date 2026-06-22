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

## Smooth output

The preview on the canvas updates a few times a second to keep the
interface snappy. The actual recording captures at full frame rate —
your audience won't see the preview rate, just the smooth final
video.

## Privacy

The webcam is only switched on when you can see it — when the main
window is open and showing the canvas. If you launched the app
straight into the system tray, the camera stays off (and its
indicator light stays dark) until you bring the window up.
