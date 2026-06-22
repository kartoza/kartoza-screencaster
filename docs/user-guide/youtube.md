<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# YouTube upload

!!! tip "Walkthrough pending"
    Walkthrough frames will land here.

## First-time setup

1. **Settings → YouTube → Add account.**
2. The app opens your browser to Google's OAuth consent screen.
3. Approve **YouTube — manage your videos**.
4. The token is stored in `~/.config/Kartoza/Kartoza Screencaster.conf`
   under the `youtube/<account>` namespace.

You can register multiple accounts and pick one per upload.

## Uploading

From the History tab, hit **Upload to YouTube** on any recording with a
`merged.mp4`. The form pre-fills with the recording's title and
description; you can override:

- **Title** — up to 100 characters.
- **Description** — full markdown supported by YouTube.
- **Privacy** — public, unlisted, or private.
- **Category** — YouTube's category ID list.

The upload runs in the background. Progress shows in the row's status
column; once complete, the YouTube video URL is stored in
`recording.json` for future reference.

## Quota

YouTube's API allocates each OAuth client a per-day quota. The default
free tier is enough for a few uploads per day; for heavier use you'll
want to register your own Google Cloud project and set its credentials
in `Settings → YouTube → Advanced`.
