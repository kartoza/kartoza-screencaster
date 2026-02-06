# Audio Package

The `audio` package handles audio capture and post-processing, including professional-grade normalization via [Jivetalking](https://github.com/linuxmatters/jivetalking).

## Package Location

```
internal/audio/
```

## Responsibility

- Capture audio from microphone
- Process and normalize audio levels
- Adaptive noise removal and compression
- Detect audio devices
- Platform-specific audio handling

## Key Files

| File | Purpose |
|------|---------|
| `audio.go` | Core audio logic and processing |
| `audio_linux.go` | Linux-specific (PipeWire) |
| `audio_darwin.go` | macOS-specific (CoreAudio) |
| `audio_windows.go` | Windows-specific (WASAPI) |

## Key Types

### Processor

Manages audio post-processing:

```go
type Processor struct {
    options models.AudioProcessingOptions
}
```

### AudioProcessingOptions

Configuration for audio processing:

```go
type AudioProcessingOptions struct {
    NormalizeEnabled    bool    // Enable full processing pipeline
    TargetLoudness      float64 // Target LUFS (-18 for podcast standard)
    TruePeak            float64 // Max true peak in dBTP (-1.5 standard)
    LoudnessRange       float64 // Target LRA in LU (11 preserves dynamics)
    NoiseRemovalEnabled bool    // Enable adaptive noise removal
    CompressionEnabled  bool    // Enable LA-2A style compression
    DeesserEnabled      bool    // Enable adaptive de-essing
}
```

### AudioRecorder

Manages audio capture:

```go
type Recorder struct {
    cmd        *exec.Cmd
    outputFile string
    device     string
}
```

## Audio Processing

### Jivetalking Integration

When [Jivetalking](https://github.com/linuxmatters/jivetalking) is installed, Kartoza Screencaster uses its professional 4-pass audio processing pipeline:

```
┌─────────────────────────────────────────────────────────────┐
│                    Jivetalking Pipeline                      │
├─────────────────────────────────────────────────────────────┤
│ Pass 1: Analysis                                             │
│   • Loudness measurement (LUFS, LRA, true peak)             │
│   • Noise floor detection                                    │
│   • Speech characteristics analysis                          │
│   • Spectral analysis                                        │
├─────────────────────────────────────────────────────────────┤
│ Pass 2: Adaptive Processing                                  │
│   • DS201-style high-pass filter (removes rumble)           │
│   • DS201-style low-pass filter (removes ultrasonic noise)  │
│   • anlmdn + compand noise removal                          │
│   • DS201-style soft gate (inter-speech cleanup)            │
│   • LA-2A optical compression (evens dynamics)              │
│   • Adaptive de-esser (reduces sibilance)                   │
├─────────────────────────────────────────────────────────────┤
│ Pass 3: Loudnorm Measurement                                 │
│   • EBU R128 loudness measurement for calibration           │
├─────────────────────────────────────────────────────────────┤
│ Pass 4: Loudnorm Application                                 │
│   • Two-pass linear loudnorm                                 │
│   • Optional pre-limiting (Volumax-inspired)                │
│   • Click/pop repair (adeclick)                             │
│   • Final measurement validation                             │
└─────────────────────────────────────────────────────────────┘
```

### FFmpeg Fallback

When Jivetalking is not installed, the system falls back to basic FFmpeg two-pass loudnorm:

```bash
# Pass 1: Analyze
ffmpeg -i input.wav -af loudnorm=I=-18:TP=-1.5:LRA=11:print_format=json -f null -

# Pass 2: Apply with measured values
ffmpeg -i input.wav -af loudnorm=I=-18:TP=-1.5:LRA=11:measured_I=...:linear=true output.wav
```

## Core Functions

### Process

Process audio with full pipeline:

```go
func (p *Processor) Process(inputFile string, progressCallback ProgressCallback) (string, error)
```

Returns the path to the processed audio file.

### Start (Recorder)

Begin audio capture:

```go
func (r *Recorder) Start(outputFile string) error
```

### Stop (Recorder)

End audio capture:

```go
func (r *Recorder) Stop() error
```

## Platform Implementation

### Linux (PipeWire)

```go
//go:build linux

func NewRecorder(device string) *Recorder {
    return &Recorder{device: device}
}

func (r *Recorder) Start(output string) error {
    r.cmd = exec.Command("pw-record",
        "--target", r.device,
        "--format", "s16",
        "--rate", "48000",
        "--channels", "1",
        output)
    return r.cmd.Start()
}
```

### macOS (CoreAudio)

```go
//go:build darwin

func (r *Recorder) Start(output string) error {
    r.cmd = exec.Command("ffmpeg",
        "-f", "avfoundation",
        "-i", ":"+r.device,
        "-c:a", "pcm_s16le",
        output)
    return r.cmd.Start()
}
```

### Windows (WASAPI)

```go
//go:build windows

func (r *Recorder) Start(output string) error {
    r.cmd = exec.Command("ffmpeg",
        "-f", "dshow",
        "-i", "audio="+r.device,
        "-c:a", "pcm_s16le",
        output)
    return r.cmd.Start()
}
```

## Processing Options

### Default Configuration

```go
func DefaultAudioProcessingOptions() AudioProcessingOptions {
    return AudioProcessingOptions{
        NormalizeEnabled:    true,
        TargetLoudness:      -18.0, // Podcast standard (EBU R128)
        TruePeak:            -1.5,  // Prevents clipping
        LoudnessRange:       11.0,  // Preserves dynamic range
        NoiseRemovalEnabled: true,  // Enable noise removal
        CompressionEnabled:  true,  // Enable compression
        DeesserEnabled:      true,  // Enable de-esser
    }
}
```

### Loudness Targets

| Target | Value | Standard |
|--------|-------|----------|
| Integrated Loudness | -18 LUFS | EBU R128 / Podcast |
| True Peak | -1.5 dBTP | Prevents inter-sample peaks |
| Loudness Range | 11 LU | Preserves natural dynamics |

## Error Handling

### Fallback Strategy

```go
func (p *Processor) Process(inputFile string, callback ProgressCallback) (string, error) {
    // Try Jivetalking first
    jivetalking, err := exec.LookPath("jivetalking")
    if err != nil {
        // Fall back to FFmpeg-based normalization
        return p.processFallback(inputFile, callback)
    }

    // Use Jivetalking...
}
```

### Device Detection

```go
func detectAudioDevices() ([]string, error) {
    // Linux: pw-cli list-objects
    // macOS: ffmpeg -f avfoundation -list_devices true
    // Windows: ffmpeg -f dshow -list_devices true
}
```

## Usage Example

```go
// Create processor with options
opts := models.DefaultAudioProcessingOptions()
processor := audio.NewProcessor(opts)

// Process with progress callback
outputPath, err := processor.Process("/tmp/audio.wav", func(pass int, name string, progress float64) {
    fmt.Printf("Pass %d (%s): %.0f%%\n", pass, name, progress*100)
})

if err != nil {
    log.Printf("Processing failed: %v", err)
}

fmt.Printf("Processed audio: %s\n", outputPath)
```

## Configuration

### Sample Rates

| Quality | Rate | Use Case |
|---------|------|----------|
| Standard | 44100 Hz | General use |
| Professional | 48000 Hz | Video production |

### Channels

- **Mono** - Single channel (recommended for speech)
- **Stereo** - Two channels (music/ambience)

## Related Packages

- **recorder** - Uses audio for capture
- **merger** - Merges processed audio with video
- **models** - Contains AudioProcessingOptions

## External Dependencies

- **Jivetalking** - Professional audio processing (optional, recommended)
- **FFmpeg** - Fallback processing and format conversion
- **PipeWire** - Audio capture on Linux
