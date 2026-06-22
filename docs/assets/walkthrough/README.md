<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Walkthrough frames

This directory holds PNG / JPG frames extracted from the maintainer's
narrated walkthrough video and used to illustrate the User Guide pages.

## How frames land here

For each new walkthrough recording:

```bash
# Extract one frame per second from the source video.
ffmpeg -i walkthrough.mp4 -vf fps=1 docs/assets/walkthrough/frame-%04d.png

# Pick the keeper frames and rename to the section they illustrate.
mv frame-0042.png docs/assets/walkthrough/canvas-add-screen.png
```

## How to reference them in a page

```markdown
![Adding a screen to the canvas](../assets/walkthrough/canvas-add-screen.png){ .kz-screenshot }
```

The `kz-screenshot` class applies the brand-flat border treatment from
`docs/stylesheets/extra.css`.
