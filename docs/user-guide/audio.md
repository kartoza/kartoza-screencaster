<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Audio

!!! tip "Walkthrough pending"
    Walkthrough frames will land here.

## Microphone

Pick an input device in **Settings → Audio**. The recorder uses the
platform-native source:

- **Linux** — PulseAudio / PipeWire via `ffmpeg -f pulse`.
- **macOS** — Core Audio via `ffmpeg -f avfoundation`.
- **Windows** — WASAPI via `ffmpeg -f dshow`.

## Intro / outro stings

Drop a WAV or MP3 onto the canvas to play it once at the start (intro)
or end (outro) of the recording. The merger mixes the sting over the
main audio track at -6 dB during post-processing.

## Denoise

After Stop, the recorder captures **5 seconds of room noise** with the
microphone open but no input. The merger then runs `afftdn` against
that noise profile to remove constant background hum.

If denoise fails (graph link error, missing input, etc.) the unprocessed
audio is used and a warning is logged.
