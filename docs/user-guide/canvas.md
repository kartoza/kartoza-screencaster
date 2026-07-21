<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Canvas editor

!!! tip "Walkthrough pending"
    This page will be expanded with frames from the walkthrough video.
    Use the `kz-screenshot` class to keep the brand-flat border treatment.

The canvas is the heart of Kartoza Screencaster — a WYSIWYG composer
that shows you, in real time, exactly what your recording will look
like. Drop in a screen, a webcam, a logo, an animated GIF, an intro
sting; arrange them; record.

## Adding elements

- **Screen** — pick a monitor from the dropdown. You can add **more than one
  monitor**: the first becomes the primary background, and each additional
  monitor drops in as a movable inset you can place anywhere (for example
  side-by-side). Every screen previews live and independently.
- **Webcam** — pick the camera you want to use. Add as many as you like; each
  camera is captured separately and composited at its own position, shape and crop.
- **Logo** — drop a raster image (PNG, JPG, WebP, BMP) or an animated GIF. SVG
  isn't offered because FFmpeg can't render it into the recording; if you do drop
  an SVG it's rasterised to PNG automatically.
- **GIF** — drop an animated GIF with an optional loop limit.
- **Intro / outro sound** — drop a WAV or MP3 to play once at the
  start / end of the recording.

!!! note "Recording multiple monitors"
    Each monitor is captured separately and the additional ones are composited as
    insets over the primary in the **landscape** recording, at their canvas
    positions and crop. On GNOME/KDE Wayland (portal capture) only the primary
    monitor is captured; the vertical/split output uses the primary screen.

## Resizing and cropping

Select an element to reveal its handles:

- **Resize** — drag a **corner** handle to scale proportionally, or an **edge**
  handle to stretch one axis. The scroll wheel scales too. Hold **Shift** on a
  text box to stretch it freely.
- **Crop** — hold **Alt**. The handles turn Kartoza blue and now trim the
  element instead of scaling it: drag an **edge** to crop one side, or a
  **corner** to crop two sides at once. Cropping a webcam trims the region that
  is actually recorded, matching what you see.
- **Aim a webcam bubble** — crop a round webcam down to a smaller circle, then
  hold **Alt** and **drag the bubble body** to pan the crop window across the
  camera feed until it frames exactly the part you want to show.

As you drag, elements **snap** to the frame edges, the frame centre lines, and
other objects' edges and centres, with a guide line shown while a snap is
engaged. Hold **Shift** to place freely without snapping. Everything stays within
the recording frame, so nothing is ever dragged off the recorded area.

## Pausing a live preview

Hover a monitor or webcam and a **pause/continue button** appears centred on it.
Click it to freeze just that element's live preview — the others keep running.
A paused element keeps its button visible so you always know it is frozen. This
only affects the preview; it does not change the recording.

## Layout modes

- **Landscape 16:9** — classic horizontal layout.
- **Vertical 9:16** — for Shorts / Reels / TikTok.
- **9:16 split (left / right)** — vertical with screen on one side and
  webcam-heavy column on the other.

## Presets

Save the current canvas layout as a preset and load it on demand. Each
preset captures every item position, size and per-item setting.
