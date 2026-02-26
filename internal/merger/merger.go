package merger

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/kartoza/kartoza-screencaster/internal/audio"
	"github.com/kartoza/kartoza-screencaster/internal/config"
	"github.com/kartoza/kartoza-screencaster/internal/models"
	"github.com/kartoza/kartoza-screencaster/internal/notify"
	"github.com/kartoza/kartoza-screencaster/internal/webcam"
)

// ProcessingStep represents a step in the processing pipeline
type ProcessingStep int

const (
	StepAnalyzingAudio ProcessingStep = iota
	StepNormalizing
	StepMerging
	StepCreatingVertical
)

// ProgressCallback is called when a processing step starts or completes
type ProgressCallback func(step ProcessingStep, completed bool, skipped bool, err error)

// PercentCallback is called to report progress percentage during a step
type PercentCallback func(step ProcessingStep, percent float64)

// Merger handles merging of video, audio, and webcam recordings
type Merger struct {
	audioOpts  models.AudioProcessingOptions
	onProgress ProgressCallback
	onPercent  PercentCallback
}

// New creates a new Merger
func New(audioOpts models.AudioProcessingOptions) *Merger {
	return &Merger{audioOpts: audioOpts}
}

// SetProgressCallback sets the callback for progress updates
func (m *Merger) SetProgressCallback(cb ProgressCallback) {
	m.onProgress = cb
}

// SetPercentCallback sets the callback for percentage progress updates
func (m *Merger) SetPercentCallback(cb PercentCallback) {
	m.onPercent = cb
}

// reportProgress reports progress if callback is set
func (m *Merger) reportProgress(step ProcessingStep, completed bool, skipped bool, err error) {
	if m.onProgress != nil {
		m.onProgress(step, completed, skipped, err)
	}
}

// reportPercent reports percentage progress if callback is set
func (m *Merger) reportPercent(step ProcessingStep, percent float64) {
	if m.onPercent != nil {
		m.onPercent(step, percent)
	}
}

// runFFmpegWithProgress runs an FFmpeg command and reports progress
// durationUs is the expected duration in microseconds for calculating percentage
func (m *Merger) runFFmpegWithProgress(step ProcessingStep, durationUs int64, args ...string) error {
	// Add progress pipe and stats period to args for frequent updates
	// -stats_period 0.5 outputs progress every 0.5 seconds
	progressArgs := append([]string{"-progress", "pipe:1", "-stats_period", "0.5", "-nostats"}, args...)

	cmd := exec.Command("ffmpeg", progressArgs...)

	// Capture stdout for progress
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return fmt.Errorf("failed to create stdout pipe: %w", err)
	}

	// Capture stderr for errors
	var stderrBuf strings.Builder
	cmd.Stderr = &stderrBuf

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("failed to start ffmpeg: %w", err)
	}

	// Report initial progress
	m.reportPercent(step, 0)

	// Parse progress output
	// FFmpeg outputs progress in key=value format, one per line
	// We're looking for out_time_us (microseconds)
	// Note: out_time_us can be "N/A" at the start, which we skip
	scanner := bufio.NewScanner(stdout)
	for scanner.Scan() {
		line := scanner.Text()

		// Parse out_time_us (microseconds)
		if strings.HasPrefix(line, "out_time_us=") {
			timeStr := strings.TrimPrefix(line, "out_time_us=")
			// Skip N/A values
			if timeStr == "N/A" {
				continue
			}
			if timeUs, err := strconv.ParseInt(timeStr, 10, 64); err == nil && durationUs > 0 && timeUs >= 0 {
				percent := float64(timeUs) / float64(durationUs) * 100
				if percent > 100 {
					percent = 100
				}
				m.reportPercent(step, percent)
			}
		}
	}

	if err := cmd.Wait(); err != nil {
		return fmt.Errorf("ffmpeg failed: %w, stderr: %s", err, stderrBuf.String())
	}

	return nil
}

// getVideoDurationUs returns the duration of a video in microseconds
func getVideoDurationUs(filepath string) int64 {
	meta, err := webcam.GetFullVideoInfo(filepath)
	if err != nil {
		return 0
	}
	return int64(meta.Duration * 1000000) // Convert seconds to microseconds
}

// MergeOptions contains options for merging recordings
type MergeOptions struct {
	VideoFile      string
	AudioFile      string
	WebcamFile     string
	RoomNoiseFile  string             // Room noise file for audio processing calibration
	CreateVertical bool
	LeftSplit      bool               // Use left half of screen for vertical video
	AddLogos       bool               // Whether to add logo overlays
	ProductLogo1   string             // Path to product logo 1 (top-left)
	ProductLogo2   string             // Path to product logo 2 (top-right)
	CompanyLogo    string             // Path to company logo (lower third)
	VideoTitle     string             // Title for lower third overlay
	VideoNumber    int                // Recording number for output filename
	TitleColor     string             // Color for title text (e.g., "white", "black", "yellow")
	BgColor        string             // Background color for vertical video lower third
	GifLoopMode    config.GifLoopMode // How to loop animated GIFs
	OutputDir      string             // Directory for output files

	// Part files for pause/resume support (if set, these override single file options)
	VideoParts  []string
	AudioParts  []string
	WebcamParts []string
}

// MergeResult contains the paths to merged files and processing info
type MergeResult struct {
	MergedFile       string
	VerticalFile     string
	NormalizeApplied bool
	VerticalError    error // Non-nil if vertical video creation was attempted but failed
}

// generateOutputFilename creates output filename in format: NNN-recording_name.mp4
// If title or number are not provided, falls back to the base video filename
func generateOutputFilename(outputDir, title string, number int, suffix string) string {
	if title == "" || number <= 0 || outputDir == "" {
		return "" // Return empty to signal fallback to old behavior
	}

	// Sanitize title: replace spaces and special chars with underscores
	sanitized := strings.ToLower(title)
	// Replace common separators with underscores
	for _, char := range []string{" ", "-", ".", ",", ":", ";", "!", "?", "'", "\"", "/", "\\"} {
		sanitized = strings.ReplaceAll(sanitized, char, "_")
	}
	// Remove consecutive underscores
	for strings.Contains(sanitized, "__") {
		sanitized = strings.ReplaceAll(sanitized, "__", "_")
	}
	// Trim leading/trailing underscores
	sanitized = strings.Trim(sanitized, "_")

	// Format: NNN-recording_name.mp4 or NNN-recording_name_vertical.mp4
	filename := fmt.Sprintf("%03d-%s%s.mp4", number, sanitized, suffix)
	return filepath.Join(outputDir, filename)
}

// concatenateParts concatenates multiple video or audio parts into a single file
// Uses FFmpeg's concat demuxer for lossless concatenation
func concatenateParts(parts []string, outputFile string) error {
	if len(parts) == 0 {
		return fmt.Errorf("no parts to concatenate")
	}

	if len(parts) == 1 {
		// Only one part, just copy/rename it
		return copyFile(parts[0], outputFile)
	}

	// Filter to only existing parts
	var existingParts []string
	for _, part := range parts {
		if fileExists(part) {
			existingParts = append(existingParts, part)
		}
	}

	if len(existingParts) == 0 {
		return fmt.Errorf("no existing parts found")
	}

	if len(existingParts) == 1 {
		return copyFile(existingParts[0], outputFile)
	}

	// Create a temporary file list for FFmpeg concat demuxer
	listFile := outputFile + ".txt"
	f, err := os.Create(listFile)
	if err != nil {
		return fmt.Errorf("failed to create concat list: %w", err)
	}

	for _, part := range existingParts {
		// FFmpeg concat format: file 'path'
		// Need to escape single quotes in path
		escapedPath := strings.ReplaceAll(part, "'", "'\\''")
		_, _ = fmt.Fprintf(f, "file '%s'\n", escapedPath)
	}
	_ = f.Close()
	defer func() { _ = os.Remove(listFile) }()

	// Run FFmpeg to concatenate
	cmd := exec.Command("ffmpeg",
		"-y",
		"-f", "concat",
		"-safe", "0",
		"-i", listFile,
		"-c", "copy",
		outputFile,
	)

	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("ffmpeg concat failed: %w\nOutput: %s", err, string(output))
	}

	return nil
}

// copyFile copies a file from src to dst
func copyFile(src, dst string) error {
	source, err := os.Open(src)
	if err != nil {
		return err
	}
	defer func() { _ = source.Close() }()

	destination, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer func() { _ = destination.Close() }()

	_, err = io.Copy(destination, source)
	return err
}

// Merge merges video and audio recordings
// Handles all combinations of missing inputs gracefully:
// - Video only: copies video to output
// - Video + audio: merges them
// - Video + webcam: creates vertical video without audio
// - Video + webcam + audio: full merge with vertical video
// - Audio only: copies audio (no video output)
// - Webcam only: copies webcam to output
// - Webcam + audio: merges webcam with audio
func (m *Merger) Merge(opts MergeOptions) (*MergeResult, error) {
	result := &MergeResult{}

	// If we have multiple parts, concatenate them first
	if len(opts.VideoParts) > 1 {
		concatVideo := filepath.Join(opts.OutputDir, "screen.mp4")
		if err := concatenateParts(opts.VideoParts, concatVideo); err != nil {
			return result, fmt.Errorf("failed to concatenate video parts: %w", err)
		}
		opts.VideoFile = concatVideo
	} else if len(opts.VideoParts) == 1 && fileExists(opts.VideoParts[0]) {
		opts.VideoFile = opts.VideoParts[0]
	}

	if len(opts.AudioParts) > 1 {
		concatAudio := filepath.Join(opts.OutputDir, "audio.wav")
		if err := concatenateParts(opts.AudioParts, concatAudio); err != nil {
			return result, fmt.Errorf("failed to concatenate audio parts: %w", err)
		}
		opts.AudioFile = concatAudio
	} else if len(opts.AudioParts) == 1 && fileExists(opts.AudioParts[0]) {
		opts.AudioFile = opts.AudioParts[0]
	}

	if len(opts.WebcamParts) > 1 {
		concatWebcam := filepath.Join(opts.OutputDir, "webcam.mp4")
		if err := concatenateParts(opts.WebcamParts, concatWebcam); err != nil {
			return result, fmt.Errorf("failed to concatenate webcam parts: %w", err)
		}
		opts.WebcamFile = concatWebcam
	} else if len(opts.WebcamParts) == 1 && fileExists(opts.WebcamParts[0]) {
		opts.WebcamFile = opts.WebcamParts[0]
	}

	// Check what inputs we have
	hasVideo := opts.VideoFile != "" && fileExists(opts.VideoFile)
	hasAudio := opts.AudioFile != "" && fileExists(opts.AudioFile)
	hasWebcam := opts.WebcamFile != "" && fileExists(opts.WebcamFile)

	// If we have no inputs at all, return early
	if !hasVideo && !hasAudio && !hasWebcam {
		return result, fmt.Errorf("no input files provided")
	}

	// Copy room noise file to audio options if provided
	if opts.RoomNoiseFile != "" {
		m.audioOpts.RoomNoiseFile = opts.RoomNoiseFile
	}

	// Process audio if available using jivetalking
	var normalizedAudio string
	processor := audio.NewProcessor(m.audioOpts)

	// Steps 1 & 2: Process audio with jivetalking (analysis + processing in one call)
	// Jivetalking handles: analysis, noise removal, compression, de-essing, normalization
	m.reportProgress(StepAnalyzingAudio, false, false, nil)
	if hasAudio && m.audioOpts.NormalizeEnabled {
		// Check if room noise file exists, clear it if not
		if m.audioOpts.RoomNoiseFile != "" {
			if _, err := os.Stat(m.audioOpts.RoomNoiseFile); os.IsNotExist(err) {
				m.audioOpts.RoomNoiseFile = "" // Skip room noise calibration if file missing
			}
		}

		// Track whether we've transitioned to the normalizing step
		normalizingStarted := false
		receivedAnyProgress := false

		// Create progress callback that reports to both steps
		progressCallback := func(pass int, passName string, progress float64) {
			receivedAnyProgress = true
			if pass == 1 {
				// Pass 1 is analysis
				m.reportPercent(StepAnalyzingAudio, progress*100)
			} else {
				// Pass 2+ is processing/normalization
				// Start the normalizing step on first pass 2+ callback
				if !normalizingStarted {
					m.reportProgress(StepAnalyzingAudio, true, false, nil)
					m.reportProgress(StepNormalizing, false, false, nil)
					normalizingStarted = true
				}
				m.reportPercent(StepNormalizing, progress*100)
			}
		}

		processedPath, err := processor.Process(opts.AudioFile, progressCallback)
		if err != nil {
			// Mark both steps as failed/skipped
			m.reportProgress(StepAnalyzingAudio, true, true, err)
			m.reportProgress(StepNormalizing, true, true, err)
			_ = notify.Warning("Audio Processing Warning", "Using original audio")
			normalizedAudio = opts.AudioFile
		} else {
			// Processing succeeded - ensure proper step transitions
			// If we never received progress (jivetalking output wasn't parsed), mark steps complete
			if !receivedAnyProgress || !normalizingStarted {
				m.reportProgress(StepAnalyzingAudio, true, false, nil)
				m.reportProgress(StepNormalizing, false, false, nil)
			}
			m.reportProgress(StepNormalizing, true, false, nil)
			normalizedAudio = processedPath
			result.NormalizeApplied = true
		}
	} else if hasAudio {
		// Audio exists but normalization disabled
		m.reportProgress(StepAnalyzingAudio, true, true, nil)
		m.reportProgress(StepNormalizing, true, true, nil)
		normalizedAudio = opts.AudioFile
	} else {
		// No audio
		m.reportProgress(StepAnalyzingAudio, true, true, nil)
		m.reportProgress(StepNormalizing, true, true, nil)
	}

	// Step 3: Create merged output
	m.reportProgress(StepMerging, false, false, nil)

	// Determine base file for output naming
	baseFile := opts.VideoFile
	if baseFile == "" {
		baseFile = opts.WebcamFile
	}
	if baseFile == "" {
		// Audio only - skip video merge
		m.reportProgress(StepMerging, true, true, nil)
		m.reportProgress(StepCreatingVertical, true, true, nil)
		return result, nil
	}

	// Generate output filename in format: NNN_title_with_underscores.mp4
	outputFile := generateOutputFilename(opts.OutputDir, opts.VideoTitle, opts.VideoNumber, "")
	if outputFile == "" {
		// Fallback to old behavior if title/number not provided
		outputFile = strings.TrimSuffix(baseFile, ".mp4") + "-merged.mp4"
	}

	// Handle different input combinations
	var mergeErr error
	switch {
	case hasVideo && hasAudio:
		// Standard merge: video + audio
		_ = notify.ProcessingStep("Merging video and audio...")
		mergeErr = m.mergeVideoAudio(opts.VideoFile, normalizedAudio, outputFile, &opts)
	case hasVideo && !hasAudio:
		// Video only: copy/re-encode video without audio
		_ = notify.ProcessingStep("Processing video (no audio)...")
		mergeErr = m.processVideoOnly(opts.VideoFile, outputFile, &opts)
	case !hasVideo && hasWebcam && hasAudio:
		// Webcam + audio only (no screen video)
		_ = notify.ProcessingStep("Merging webcam and audio...")
		mergeErr = m.mergeVideoAudio(opts.WebcamFile, normalizedAudio, outputFile, &opts)
	case !hasVideo && hasWebcam && !hasAudio:
		// Webcam only: copy/re-encode webcam without audio
		_ = notify.ProcessingStep("Processing webcam video (no audio)...")
		mergeErr = m.processVideoOnly(opts.WebcamFile, outputFile, &opts)
	}

	if mergeErr != nil {
		m.reportProgress(StepMerging, true, false, mergeErr)
		return nil, fmt.Errorf("failed to merge recordings: %w", mergeErr)
	}
	m.reportProgress(StepMerging, true, false, nil)

	result.MergedFile = outputFile
	_ = notify.RecordingComplete(filepath.Base(outputFile))

	// Step 4: Create vertical video if requested
	// Supports: screen+webcam, or screen-only (no webcam)
	m.reportProgress(StepCreatingVertical, false, false, nil)
	if opts.CreateVertical && hasVideo {
		// Generate vertical filename in format: NNN_title_with_underscores_vertical.mp4
		verticalFile := generateOutputFilename(opts.OutputDir, opts.VideoTitle, opts.VideoNumber, "_vertical")
		if verticalFile == "" {
			// Fallback to old behavior if title/number not provided
			verticalFile = strings.TrimSuffix(opts.VideoFile, ".mp4") + "-vertical.mp4"
		}

		var verticalErr error
		if hasWebcam {
			// With webcam: use the standard stacked or left-split layout
			if hasAudio {
				verticalErr = m.createVerticalVideo(opts.VideoFile, opts.WebcamFile, normalizedAudio, verticalFile, &opts)
			} else {
				verticalErr = m.createVerticalVideoNoAudio(opts.VideoFile, opts.WebcamFile, verticalFile, &opts)
			}
		} else {
			// No webcam: create vertical video from screen only
			if hasAudio {
				verticalErr = m.createVerticalVideoScreenOnly(opts.VideoFile, normalizedAudio, verticalFile, &opts)
			} else {
				verticalErr = m.createVerticalVideoScreenOnlyNoAudio(opts.VideoFile, verticalFile, &opts)
			}
		}

		if verticalErr != nil {
			result.VerticalError = verticalErr
			m.reportProgress(StepCreatingVertical, true, true, verticalErr)
			_ = notify.Warning("Vertical Video Warning", "Failed to create vertical video")
		} else {
			result.VerticalFile = verticalFile
			m.reportProgress(StepCreatingVertical, true, false, nil)
			_ = notify.VerticalComplete(filepath.Base(verticalFile))
		}
	} else {
		m.reportProgress(StepCreatingVertical, true, true, nil)
	}

	return result, nil
}

// fileExists checks if a file exists and is not a directory
func fileExists(path string) bool {
	info, err := os.Stat(path)
	if os.IsNotExist(err) {
		return false
	}
	return err == nil && !info.IsDir()
}

// processVideoOnly re-encodes a video file without audio, optionally with logo and webcam overlays
func (m *Merger) processVideoOnly(videoFile, outputFile string, opts *MergeOptions) error {
	durationUs := getVideoDurationUs(videoFile)

	// Check if we need overlays (logos or circular webcam)
	hasLogos := opts != nil && opts.AddLogos && opts.OutputDir != ""
	hasWebcamOverlay := opts != nil && opts.WebcamFile != "" && opts.WebcamFile != videoFile && fileExists(opts.WebcamFile)

	if hasLogos || hasWebcamOverlay {
		videoWidth, _, _ := webcam.GetVideoInfo(videoFile)
		if videoWidth > 0 {
			inputs := []string{"-y", "-i", videoFile}
			nextIdx := 1 // next FFmpeg input index

			// Add logo inputs
			var setup logoSetup
			if hasLogos {
				setup, inputs = m.prepareMergedLogos(opts, inputs, nextIdx)
				// Count how many logo inputs were added
				if setup.logo1Path != "" {
					nextIdx++
				}
				if setup.logo2Path != "" {
					nextIdx++
				}
				if setup.bannerPath != "" {
					nextIdx++
				}
			} else {
				setup = logoSetup{startInputIndex: nextIdx}
			}

			// Add webcam input for circular overlay
			webcam := webcamOverlayOpts{inputIdx: -1, size: webcamOverlaySize, margin: webcamOverlayMargin}
			if hasWebcamOverlay {
				inputs = append(inputs, "-i", opts.WebcamFile)
				webcam.inputIdx = nextIdx
			}

			hasAnyLogos := setup.logo1Path != "" || setup.logo2Path != "" || setup.bannerPath != ""
			if hasAnyLogos || webcam.inputIdx >= 0 {
				filter := buildMergedOverlayFilter(setup, videoWidth, webcam)
				args := append(inputs,
					"-filter_complex", filter,
					"-map", "[outv]",
					"-c:v", "libx264",
					"-preset", "medium",
					"-crf", "18",
					"-r", "30",
					"-pix_fmt", "yuv420p",
					"-an",
					outputFile,
				)
				return m.runFFmpegWithProgress(StepMerging, durationUs, args...)
			}
		}
	}

	// Simple re-encode without overlays
	args := []string{
		"-y",
		"-i", videoFile,
		"-c:v", "libx264",
		"-preset", "medium",
		"-crf", "18",
		"-r", "30",
		"-an", // No audio
		outputFile,
	}

	return m.runFFmpegWithProgress(StepMerging, durationUs, args...)
}

// mergeVideoAudio merges video and audio using ffmpeg, optionally with logo and webcam overlays
func (m *Merger) mergeVideoAudio(videoFile, audioFile, outputFile string, opts *MergeOptions) error {
	durationUs := getVideoDurationUs(videoFile)

	// Check if we need overlays (logos or circular webcam)
	hasLogos := opts != nil && opts.AddLogos && opts.OutputDir != ""
	hasWebcamOverlay := opts != nil && opts.WebcamFile != "" && opts.WebcamFile != videoFile && fileExists(opts.WebcamFile)

	if hasLogos || hasWebcamOverlay {
		videoWidth, _, _ := webcam.GetVideoInfo(videoFile)
		if videoWidth > 0 {
			inputs := []string{"-y", "-i", videoFile, "-i", audioFile}
			nextIdx := 2 // next FFmpeg input index

			// Add logo inputs
			var setup logoSetup
			if hasLogos {
				setup, inputs = m.prepareMergedLogos(opts, inputs, nextIdx)
				// Count how many logo inputs were added
				if setup.logo1Path != "" {
					nextIdx++
				}
				if setup.logo2Path != "" {
					nextIdx++
				}
				if setup.bannerPath != "" {
					nextIdx++
				}
			} else {
				setup = logoSetup{startInputIndex: nextIdx}
			}

			// Add webcam input for circular overlay
			webcam := webcamOverlayOpts{inputIdx: -1, size: webcamOverlaySize, margin: webcamOverlayMargin}
			if hasWebcamOverlay {
				inputs = append(inputs, "-i", opts.WebcamFile)
				webcam.inputIdx = nextIdx
			}

			hasAnyLogos := setup.logo1Path != "" || setup.logo2Path != "" || setup.bannerPath != ""
			if hasAnyLogos || webcam.inputIdx >= 0 {
				filter := buildMergedOverlayFilter(setup, videoWidth, webcam)
				args := append(inputs,
					"-filter_complex", filter,
					"-map", "[outv]",
					"-map", "1:a",
					"-c:v", "libx264",
					"-preset", "medium",
					"-crf", "18",
					"-r", "30",
					"-pix_fmt", "yuv420p",
					"-c:a", "aac",
					"-b:a", "320k",
					"-shortest",
					outputFile,
				)
				return m.runFFmpegWithProgress(StepMerging, durationUs, args...)
			}
		}
	}

	// Simple merge without overlays
	args := []string{
		"-y",
		"-i", videoFile,
		"-i", audioFile,
		"-c:v", "libx264",
		"-preset", "medium",
		"-crf", "18",
		"-r", "30",
		"-c:a", "aac",
		"-b:a", "320k",
		"-shortest",
		outputFile,
	}

	return m.runFFmpegWithProgress(StepMerging, durationUs, args...)
}

// YouTube Shorts recommended dimensions
const (
	YouTubeShortsWidth  = 1080
	YouTubeShortsHeight = 1920
)

// createVerticalVideo creates a vertical video with webcam and branding
// Layout depends on LeftSplit option:
// - LeftSplit=true: left half of screen fills frame, webcam circle bottom-right, logos overlaid
// - LeftSplit=false: screen (top) | webcam (middle) | white branding area (bottom third)
// Output is always 1080x1920 (9:16) for YouTube Shorts compatibility
func (m *Merger) createVerticalVideo(videoFile, webcamFile, audioFile, outputFile string, opts *MergeOptions) error {
	var filterComplex string
	var inputs []string
	var err error

	if opts != nil && opts.LeftSplit {
		_ = notify.ProcessingStep("Creating left-split vertical video (1080x1920) with webcam...")
		filterComplex, inputs, err = m.buildLeftSplitVerticalFilterComplex(videoFile, webcamFile, opts, 3)
	} else {
		_ = notify.ProcessingStep("Creating vertical video (1080x1920) with webcam...")
		filterComplex, inputs, err = m.buildVerticalFilterComplex(videoFile, webcamFile, opts, 3)
	}
	if err != nil {
		return err
	}

	// Build inputs list
	allInputs := []string{"-y", "-i", videoFile, "-i", webcamFile, "-i", audioFile}
	allInputs = append(allInputs, inputs...)

	// Get video duration for progress calculation and to set output duration
	durationUs := getVideoDurationUs(videoFile)
	durationSecs := float64(durationUs) / 1000000.0

	args := append(allInputs,
		"-filter_complex", filterComplex,
		"-map", "[outv]",
		"-map", "2:a",
		"-c:v", "libx264",
		"-preset", "medium",
		"-crf", "18",
		"-r", "30",
		"-pix_fmt", "yuv420p",
		"-c:a", "aac",
		"-b:a", "320k",
		"-t", fmt.Sprintf("%.3f", durationSecs),
		outputFile,
	)

	return m.runFFmpegWithProgress(StepCreatingVertical, durationUs, args...)
}

// createVerticalVideoNoAudio creates a vertical video with webcam but without audio
// Layout depends on LeftSplit option:
// - LeftSplit=true: left half of screen fills frame, webcam circle bottom-right, logos overlaid
// - LeftSplit=false: screen (top) | webcam (middle) | white branding area (bottom third)
// Output is always 1080x1920 (9:16) for YouTube Shorts compatibility
func (m *Merger) createVerticalVideoNoAudio(videoFile, webcamFile, outputFile string, opts *MergeOptions) error {
	var filterComplex string
	var inputs []string
	var err error

	if opts != nil && opts.LeftSplit {
		_ = notify.ProcessingStep("Creating left-split vertical video (1080x1920) with webcam (no audio)...")
		filterComplex, inputs, err = m.buildLeftSplitVerticalFilterComplex(videoFile, webcamFile, opts, 2)
	} else {
		_ = notify.ProcessingStep("Creating vertical video (1080x1920) with webcam (no audio)...")
		filterComplex, inputs, err = m.buildVerticalFilterComplex(videoFile, webcamFile, opts, 2)
	}
	if err != nil {
		return err
	}

	// Build inputs list (no audio input)
	allInputs := []string{"-y", "-i", videoFile, "-i", webcamFile}
	allInputs = append(allInputs, inputs...)

	durationUs := getVideoDurationUs(videoFile)
	durationSecs := float64(durationUs) / 1000000.0

	args := append(allInputs,
		"-filter_complex", filterComplex,
		"-map", "[outv]",
		"-c:v", "libx264",
		"-preset", "medium",
		"-crf", "18",
		"-r", "30",
		"-pix_fmt", "yuv420p",
		"-an",
		"-t", fmt.Sprintf("%.3f", durationSecs),
		outputFile,
	)

	return m.runFFmpegWithProgress(StepCreatingVertical, durationUs, args...)
}

// lowerThirdY is the Y coordinate where the bottom third starts in the vertical video
const lowerThirdY = YouTubeShortsHeight * 2 / 3 // 1280

// buildVerticalFilterComplex builds the shared FFmpeg filter_complex for vertical video.
// Layout: screen (top third) | webcam (middle third) | white branding area (bottom third)
// logoStartIndex is the FFmpeg input index where logo inputs begin (3 with audio, 2 without).
// Returns: (filterComplex string, additional FFmpeg inputs for logos, error)
func (m *Merger) buildVerticalFilterComplex(videoFile, webcamFile string, opts *MergeOptions, logoStartIndex int) (string, []string, error) {
	// Get screen video dimensions
	screenWidth, screenHeight, err := webcam.GetVideoInfo(videoFile)
	if err != nil {
		return "", nil, fmt.Errorf("failed to get screen dimensions: %w", err)
	}

	// Get webcam dimensions
	webcamWidth, webcamHeight, err := webcam.GetVideoInfo(webcamFile)
	if err != nil {
		return "", nil, fmt.Errorf("failed to get webcam dimensions: %w", err)
	}

	// Calculate layout for 1080x1920 output
	// Screen takes top portion, webcam fills the middle area (between screen and lower third)
	scaledScreenWidth := YouTubeShortsWidth
	scaledScreenHeight := screenHeight * YouTubeShortsWidth / screenWidth

	// Webcam fills the space between screen and the lower third branding area
	webcamAreaHeight := lowerThirdY - scaledScreenHeight
	if webcamAreaHeight < 0 {
		webcamAreaHeight = 0
	}

	// Scale webcam to fit the webcam area while maintaining aspect ratio
	scaledWebcamWidth := YouTubeShortsWidth
	scaledWebcamHeight := webcamHeight * YouTubeShortsWidth / webcamWidth

	if scaledWebcamHeight > webcamAreaHeight {
		scaledWebcamHeight = webcamAreaHeight
		scaledWebcamWidth = webcamWidth * webcamAreaHeight / webcamHeight
	}

	webcamPadX := (YouTubeShortsWidth - scaledWebcamWidth) / 2

	// Prepare logo inputs
	var logoInputs []string
	setup, logoInputs := m.prepareMergedLogos(opts, logoInputs, logoStartIndex)

	// Determine background color for the lower third
	bgColor := config.DefaultBgColor
	if opts != nil && opts.BgColor != "" {
		bgColor = opts.BgColor
	}

	// Build filter complex for 1080x1920 output
	// 1. Scale screen to fit width (1080)
	// 2. Scale webcam to fit middle area
	// 3. Create black canvas, then draw colored lower third
	// 4. Overlay screen at top, webcam in middle
	filterComplex := fmt.Sprintf(
		"[0:v]scale=%d:%d:flags=lanczos[screen];"+
			"[1:v]scale=%d:%d:flags=lanczos[webcam];"+
			"color=black:size=%dx%d:duration=99999[bg];"+
			// Draw background for the bottom third
			"[bg]drawbox=y=%d:w=%d:h=%d:c=%s:t=fill[canvas];"+
			// Overlay screen at top center
			"[canvas][screen]overlay=(W-w)/2:0[with_screen];"+
			// Overlay webcam in middle area (centered)
			"[with_screen][webcam]overlay=%d:%d[stacked]",
		scaledScreenWidth, scaledScreenHeight,
		scaledWebcamWidth, scaledWebcamHeight,
		YouTubeShortsWidth, YouTubeShortsHeight,
		lowerThirdY, YouTubeShortsWidth, YouTubeShortsHeight-lowerThirdY, bgColor,
		webcamPadX, scaledScreenHeight,
	)

	currentOutput := "[stacked]"
	inputIdx := logoStartIndex

	// Determine title color (default to white if not specified)
	titleColor := "white"
	if opts != nil && opts.TitleColor != "" {
		titleColor = opts.TitleColor
	}

	// Add logo overlays in the bottom third (white branding area)
	// Left logo: 1/3 of output width (360px), top-left of bottom third
	if setup.logo1Path != "" {
		fragment, out := buildLogoOverlay(inputIdx, "logo1", "360:-1", "0", fmt.Sprintf("%d", lowerThirdY), currentOutput, setup.logo1Path, "")
		filterComplex += ";" + fragment
		currentOutput = out
		inputIdx++
	}

	// Right logo: 1/3 of output width (360px), top-right of bottom third
	if setup.logo2Path != "" {
		fragment, out := buildLogoOverlay(inputIdx, "logo2", "360:-1", "W-w", fmt.Sprintf("%d", lowerThirdY), currentOutput, setup.logo2Path, "")
		filterComplex += ";" + fragment
		currentOutput = out
		inputIdx++
	}

	// Banner: full width (1080px), positioned in the lower third area
	if setup.bannerPath != "" {
		// Place banner above where title will go (title is 10px from bottom)
		// Banner positioned to leave room for title text below it
		bannerY := YouTubeShortsHeight - 120 // ~110px from bottom, leaving room for title
		fragment, out := buildLogoOverlay(inputIdx, "banner", fmt.Sprintf("%d:-1", YouTubeShortsWidth), "(W-w)/2", fmt.Sprintf("%d", bannerY), currentOutput, setup.bannerPath, "")
		filterComplex += ";" + fragment
		currentOutput = out

		// Add title text below the banner, centered, 10px from bottom of frame
		if opts != nil && opts.VideoTitle != "" {
			titleY := YouTubeShortsHeight - 46 // 10px padding from bottom + ~36px font height
			filterComplex += fmt.Sprintf(
				";%sdrawtext=text='%s':fontcolor=%s:fontsize=36:x=(w-text_w)/2:y=%d[outv]",
				currentOutput, escapeFFmpegText(opts.VideoTitle), titleColor, titleY,
			)
			return filterComplex, logoInputs, nil
		}
	} else if opts != nil && opts.VideoTitle != "" {
		// Title text without banner, centered, 10px from bottom of frame
		titleY := YouTubeShortsHeight - 46 // 10px padding from bottom + ~36px font height
		filterComplex += fmt.Sprintf(
			";%sdrawtext=text='%s':fontcolor=%s:fontsize=36:x=(w-text_w)/2:y=%d[outv]",
			currentOutput, escapeFFmpegText(opts.VideoTitle), titleColor, titleY,
		)
		return filterComplex, logoInputs, nil
	}

	// Final null filter to create [outv] label
	filterComplex += fmt.Sprintf(";%snull[outv]", currentOutput)

	return filterComplex, logoInputs, nil
}

// buildLeftSplitVerticalFilterComplex builds the FFmpeg filter_complex for left-split vertical video.
// Layout: Left half of the screen video is scaled to fill the WIDTH (1080px) corner-to-corner.
// If the scaled height is less than 1920px, the video is placed at the top with branding at the bottom.
// - Webcam: circular overlay in the bottom-right corner (same style as merged video)
// - Banner: full width at the bottom
// - Left logo: top-left corner
// - Right logo: top-right corner
// logoStartIndex is the FFmpeg input index where logo inputs begin (3 with audio, 2 without).
// Returns: (filterComplex string, additional FFmpeg inputs for logos, error)
func (m *Merger) buildLeftSplitVerticalFilterComplex(videoFile, webcamFile string, opts *MergeOptions, logoStartIndex int) (string, []string, error) {
	// Get screen video dimensions
	screenWidth, screenHeight, err := webcam.GetVideoInfo(videoFile)
	if err != nil {
		return "", nil, fmt.Errorf("failed to get screen dimensions: %w", err)
	}

	// Crop the left half of the screen video
	halfWidth := screenWidth / 2

	// Scale the left half to fill the WIDTH (1080px) corner-to-corner
	// The height will be proportionally scaled, may be less than 1920px
	scaledWidth := YouTubeShortsWidth
	scaledHeight := screenHeight * YouTubeShortsWidth / halfWidth

	// If scaled height exceeds frame height, cap it and adjust
	if scaledHeight > YouTubeShortsHeight {
		scaledHeight = YouTubeShortsHeight
	}

	// Prepare logo inputs
	var logoInputs []string
	setup, logoInputs := m.prepareMergedLogos(opts, logoInputs, logoStartIndex)

	// Determine background color
	bgColor := config.DefaultBgColor
	if opts != nil && opts.BgColor != "" {
		bgColor = opts.BgColor
	}

	// Build filter complex for left-split 1080x1920 output
	// 1. Crop left half of the screen
	// 2. Scale to fill width (1080px), height proportional
	// 3. Create canvas with background color
	// 4. Overlay the scaled screen at the top
	filterComplex := fmt.Sprintf(
		"[0:v]crop=%d:%d:0:0,scale=%d:%d:flags=lanczos[screen];"+
			"color=%s:size=%dx%d:duration=99999[bg];"+
			"[bg][screen]overlay=0:0[canvas]",
		halfWidth, screenHeight,
		scaledWidth, scaledHeight,
		bgColor, YouTubeShortsWidth, YouTubeShortsHeight,
	)

	currentOutput := "[canvas]"
	inputIdx := logoStartIndex

	// Determine title color (default to white if not specified)
	titleColor := "white"
	if opts != nil && opts.TitleColor != "" {
		titleColor = opts.TitleColor
	}

	// Calculate Y position for logos (just below the screen recording with 10px padding)
	logoY := scaledHeight + 10

	// Add left logo overlay below screen recording, left side
	if setup.logo1Path != "" {
		fragment, out := buildLogoOverlay(inputIdx, "logo1", "216:-1", "10", fmt.Sprintf("%d", logoY), currentOutput, setup.logo1Path, "")
		filterComplex += ";" + fragment
		currentOutput = out
		inputIdx++
	}

	// Add right logo overlay below screen recording, right side
	if setup.logo2Path != "" {
		fragment, out := buildLogoOverlay(inputIdx, "logo2", "216:-1", "W-w-10", fmt.Sprintf("%d", logoY), currentOutput, setup.logo2Path, "")
		filterComplex += ";" + fragment
		currentOutput = out
		inputIdx++
	}

	// Add banner at bottom (full width), positioned above title
	if setup.bannerPath != "" {
		// Banner positioned to leave room for title text below it (title is 10px from bottom)
		bannerY := YouTubeShortsHeight - 120 // ~110px from bottom, leaving room for title
		fragment, out := buildLogoOverlay(inputIdx, "banner", fmt.Sprintf("%d:-1", YouTubeShortsWidth), "(W-w)/2", fmt.Sprintf("%d", bannerY), currentOutput, setup.bannerPath, "")
		filterComplex += ";" + fragment
		currentOutput = out
		inputIdx++
	}

	// Add circular webcam overlay (bottom-right, same style as merged video)
	// Build webcam circle filter
	webcamCircleFragment, webcamOut := buildWebcamCircleOverlay(1, webcamOverlaySize, webcamOverlayMargin, currentOutput)
	filterComplex += ";" + webcamCircleFragment
	currentOutput = webcamOut

	// Add title text below banner, centered, 10px from bottom of frame
	if opts != nil && opts.VideoTitle != "" {
		titleY := YouTubeShortsHeight - 46 // 10px padding from bottom + ~36px font height
		filterComplex += fmt.Sprintf(
			";%sdrawtext=text='%s':fontcolor=%s:fontsize=36:x=(w-text_w)/2:y=%d[outv]",
			currentOutput, escapeFFmpegText(opts.VideoTitle), titleColor, titleY,
		)
		return filterComplex, logoInputs, nil
	}

	// Final null filter to create [outv] label
	filterComplex += fmt.Sprintf(";%snull[outv]", currentOutput)

	return filterComplex, logoInputs, nil
}

// createVerticalVideoScreenOnly creates a vertical video from screen recording only (no webcam)
// Layout depends on LeftSplit option:
// - LeftSplit=true: left half of screen fills frame, logos overlaid
// - LeftSplit=false: screen fills top portion, branding area at bottom
// Output is always 1080x1920 (9:16) for YouTube Shorts compatibility
func (m *Merger) createVerticalVideoScreenOnly(videoFile, audioFile, outputFile string, opts *MergeOptions) error {
	var filterComplex string
	var inputs []string
	var err error

	if opts != nil && opts.LeftSplit {
		_ = notify.ProcessingStep("Creating left-split vertical video (1080x1920) from screen...")
		filterComplex, inputs, err = m.buildLeftSplitScreenOnlyFilterComplex(videoFile, opts, 2)
	} else {
		_ = notify.ProcessingStep("Creating vertical video (1080x1920) from screen...")
		filterComplex, inputs, err = m.buildScreenOnlyVerticalFilterComplex(videoFile, opts, 2)
	}
	if err != nil {
		return err
	}

	// Build inputs list
	allInputs := []string{"-y", "-i", videoFile, "-i", audioFile}
	allInputs = append(allInputs, inputs...)

	// Get video duration for progress calculation
	durationUs := getVideoDurationUs(videoFile)
	durationSecs := float64(durationUs) / 1000000.0

	args := append(allInputs,
		"-filter_complex", filterComplex,
		"-map", "[outv]",
		"-map", "1:a",
		"-c:v", "libx264",
		"-preset", "medium",
		"-crf", "18",
		"-r", "30",
		"-pix_fmt", "yuv420p",
		"-c:a", "aac",
		"-b:a", "320k",
		"-t", fmt.Sprintf("%.3f", durationSecs),
		outputFile,
	)

	return m.runFFmpegWithProgress(StepCreatingVertical, durationUs, args...)
}

// createVerticalVideoScreenOnlyNoAudio creates a vertical video from screen recording only, without audio
func (m *Merger) createVerticalVideoScreenOnlyNoAudio(videoFile, outputFile string, opts *MergeOptions) error {
	var filterComplex string
	var inputs []string
	var err error

	if opts != nil && opts.LeftSplit {
		_ = notify.ProcessingStep("Creating left-split vertical video (1080x1920) from screen (no audio)...")
		filterComplex, inputs, err = m.buildLeftSplitScreenOnlyFilterComplex(videoFile, opts, 1)
	} else {
		_ = notify.ProcessingStep("Creating vertical video (1080x1920) from screen (no audio)...")
		filterComplex, inputs, err = m.buildScreenOnlyVerticalFilterComplex(videoFile, opts, 1)
	}
	if err != nil {
		return err
	}

	// Build inputs list (no audio)
	allInputs := []string{"-y", "-i", videoFile}
	allInputs = append(allInputs, inputs...)

	durationUs := getVideoDurationUs(videoFile)
	durationSecs := float64(durationUs) / 1000000.0

	args := append(allInputs,
		"-filter_complex", filterComplex,
		"-map", "[outv]",
		"-c:v", "libx264",
		"-preset", "medium",
		"-crf", "18",
		"-r", "30",
		"-pix_fmt", "yuv420p",
		"-an",
		"-t", fmt.Sprintf("%.3f", durationSecs),
		outputFile,
	)

	return m.runFFmpegWithProgress(StepCreatingVertical, durationUs, args...)
}

// buildScreenOnlyVerticalFilterComplex builds the FFmpeg filter_complex for screen-only vertical video.
// Layout: screen (top portion scaled to fill width) | branding area (bottom third)
// logoStartIndex is the FFmpeg input index where logo inputs begin (2 with audio, 1 without).
// Returns: (filterComplex string, additional FFmpeg inputs for logos, error)
func (m *Merger) buildScreenOnlyVerticalFilterComplex(videoFile string, opts *MergeOptions, logoStartIndex int) (string, []string, error) {
	// Get screen video dimensions
	screenWidth, screenHeight, err := webcam.GetVideoInfo(videoFile)
	if err != nil {
		return "", nil, fmt.Errorf("failed to get screen dimensions: %w", err)
	}

	// Calculate layout for 1080x1920 output
	// Screen fills the top portion (above the lower third branding area)
	screenAreaHeight := lowerThirdY // Top 2/3 of the frame
	scaledScreenWidth := YouTubeShortsWidth
	scaledScreenHeight := screenHeight * YouTubeShortsWidth / screenWidth

	// If scaled screen is taller than available area, scale down to fit
	if scaledScreenHeight > screenAreaHeight {
		scaledScreenHeight = screenAreaHeight
		scaledScreenWidth = screenWidth * screenAreaHeight / screenHeight
	}

	// Center the screen in the available area
	screenPadX := (YouTubeShortsWidth - scaledScreenWidth) / 2
	screenPadY := (screenAreaHeight - scaledScreenHeight) / 2

	// Prepare logo inputs
	var logoInputs []string
	setup, logoInputs := m.prepareMergedLogos(opts, logoInputs, logoStartIndex)

	// Determine background color for the lower third
	bgColor := config.DefaultBgColor
	if opts != nil && opts.BgColor != "" {
		bgColor = opts.BgColor
	}

	// Build filter complex for 1080x1920 output
	filterComplex := fmt.Sprintf(
		"[0:v]scale=%d:%d:flags=lanczos[screen];"+
			"color=black:size=%dx%d:duration=99999[bg];"+
			// Draw background for the bottom third
			"[bg]drawbox=y=%d:w=%d:h=%d:c=%s:t=fill[canvas];"+
			// Overlay screen centered in top area
			"[canvas][screen]overlay=%d:%d[stacked]",
		scaledScreenWidth, scaledScreenHeight,
		YouTubeShortsWidth, YouTubeShortsHeight,
		lowerThirdY, YouTubeShortsWidth, YouTubeShortsHeight-lowerThirdY, bgColor,
		screenPadX, screenPadY,
	)

	currentOutput := "[stacked]"
	inputIdx := logoStartIndex

	// Determine title color
	titleColor := "white"
	if opts != nil && opts.TitleColor != "" {
		titleColor = opts.TitleColor
	}

	// Add logo overlays in the bottom third
	if setup.logo1Path != "" {
		fragment, out := buildLogoOverlay(inputIdx, "logo1", "360:-1", "0", fmt.Sprintf("%d", lowerThirdY), currentOutput, setup.logo1Path, "")
		filterComplex += ";" + fragment
		currentOutput = out
		inputIdx++
	}

	if setup.logo2Path != "" {
		fragment, out := buildLogoOverlay(inputIdx, "logo2", "360:-1", "W-w", fmt.Sprintf("%d", lowerThirdY), currentOutput, setup.logo2Path, "")
		filterComplex += ";" + fragment
		currentOutput = out
		inputIdx++
	}

	if setup.bannerPath != "" {
		// Banner positioned to leave room for title text below it (title is 10px from bottom)
		bannerY := YouTubeShortsHeight - 120 // ~110px from bottom, leaving room for title
		fragment, out := buildLogoOverlay(inputIdx, "banner", fmt.Sprintf("%d:-1", YouTubeShortsWidth), "(W-w)/2", fmt.Sprintf("%d", bannerY), currentOutput, setup.bannerPath, "")
		filterComplex += ";" + fragment
		currentOutput = out

		// Add title text below the banner, centered, 10px from bottom of frame
		if opts != nil && opts.VideoTitle != "" {
			titleY := YouTubeShortsHeight - 46 // 10px padding from bottom + ~36px font height
			filterComplex += fmt.Sprintf(
				";%sdrawtext=text='%s':fontcolor=%s:fontsize=36:x=(w-text_w)/2:y=%d[outv]",
				currentOutput, escapeFFmpegText(opts.VideoTitle), titleColor, titleY,
			)
			return filterComplex, logoInputs, nil
		}
	} else if opts != nil && opts.VideoTitle != "" {
		// Title text without banner, centered, 10px from bottom of frame
		titleY := YouTubeShortsHeight - 46 // 10px padding from bottom + ~36px font height
		filterComplex += fmt.Sprintf(
			";%sdrawtext=text='%s':fontcolor=%s:fontsize=36:x=(w-text_w)/2:y=%d[outv]",
			currentOutput, escapeFFmpegText(opts.VideoTitle), titleColor, titleY,
		)
		return filterComplex, logoInputs, nil
	}

	filterComplex += fmt.Sprintf(";%snull[outv]", currentOutput)
	return filterComplex, logoInputs, nil
}

// buildLeftSplitScreenOnlyFilterComplex builds the FFmpeg filter_complex for left-split screen-only vertical video.
// Layout: Left half of the screen video is scaled to fill the WIDTH (1080px) corner-to-corner.
// If the scaled height is less than 1920px, the video is placed at the top with branding at the bottom.
// - Banner: full width at the bottom
// - Left logo: top-left corner
// - Right logo: top-right corner
// logoStartIndex is the FFmpeg input index where logo inputs begin (2 with audio, 1 without).
// Returns: (filterComplex string, additional FFmpeg inputs for logos, error)
func (m *Merger) buildLeftSplitScreenOnlyFilterComplex(videoFile string, opts *MergeOptions, logoStartIndex int) (string, []string, error) {
	// Get screen video dimensions
	screenWidth, screenHeight, err := webcam.GetVideoInfo(videoFile)
	if err != nil {
		return "", nil, fmt.Errorf("failed to get screen dimensions: %w", err)
	}

	// Crop the left half of the screen video
	halfWidth := screenWidth / 2

	// Scale the left half to fill the WIDTH (1080px) corner-to-corner
	// The height will be proportionally scaled, may be less than 1920px
	scaledWidth := YouTubeShortsWidth
	scaledHeight := screenHeight * YouTubeShortsWidth / halfWidth

	// If scaled height exceeds frame height, cap it
	if scaledHeight > YouTubeShortsHeight {
		scaledHeight = YouTubeShortsHeight
	}

	// Prepare logo inputs
	var logoInputs []string
	setup, logoInputs := m.prepareMergedLogos(opts, logoInputs, logoStartIndex)

	// Determine background color
	bgColor := config.DefaultBgColor
	if opts != nil && opts.BgColor != "" {
		bgColor = opts.BgColor
	}

	// Build filter complex
	// 1. Crop left half of the screen
	// 2. Scale to fill width (1080px), height proportional
	// 3. Create canvas with background color
	// 4. Overlay the scaled screen at the top
	filterComplex := fmt.Sprintf(
		"[0:v]crop=%d:%d:0:0,scale=%d:%d:flags=lanczos[screen];"+
			"color=%s:size=%dx%d:duration=99999[bg];"+
			"[bg][screen]overlay=0:0[canvas]",
		halfWidth, screenHeight,
		scaledWidth, scaledHeight,
		bgColor, YouTubeShortsWidth, YouTubeShortsHeight,
	)

	currentOutput := "[canvas]"
	inputIdx := logoStartIndex

	titleColor := "white"
	if opts != nil && opts.TitleColor != "" {
		titleColor = opts.TitleColor
	}

	// Calculate Y position for logos (just below the screen recording with 10px padding)
	logoY := scaledHeight + 10

	// Add left logo overlay below screen recording, left side
	if setup.logo1Path != "" {
		fragment, out := buildLogoOverlay(inputIdx, "logo1", "216:-1", "10", fmt.Sprintf("%d", logoY), currentOutput, setup.logo1Path, "")
		filterComplex += ";" + fragment
		currentOutput = out
		inputIdx++
	}

	// Add right logo overlay below screen recording, right side
	if setup.logo2Path != "" {
		fragment, out := buildLogoOverlay(inputIdx, "logo2", "216:-1", "W-w-10", fmt.Sprintf("%d", logoY), currentOutput, setup.logo2Path, "")
		filterComplex += ";" + fragment
		currentOutput = out
		inputIdx++
	}

	// Add banner at bottom, positioned above title
	if setup.bannerPath != "" {
		// Banner positioned to leave room for title text below it (title is 10px from bottom)
		bannerY := YouTubeShortsHeight - 120 // ~110px from bottom, leaving room for title
		fragment, out := buildLogoOverlay(inputIdx, "banner", fmt.Sprintf("%d:-1", YouTubeShortsWidth), "(W-w)/2", fmt.Sprintf("%d", bannerY), currentOutput, setup.bannerPath, "")
		filterComplex += ";" + fragment
		currentOutput = out
	}

	// Add title text below banner, centered, 10px from bottom of frame
	if opts != nil && opts.VideoTitle != "" {
		titleY := YouTubeShortsHeight - 46 // 10px padding from bottom + ~36px font height
		filterComplex += fmt.Sprintf(
			";%sdrawtext=text='%s':fontcolor=%s:fontsize=36:x=(w-text_w)/2:y=%d[outv]",
			currentOutput, escapeFFmpegText(opts.VideoTitle), titleColor, titleY,
		)
		return filterComplex, logoInputs, nil
	}

	filterComplex += fmt.Sprintf(";%snull[outv]", currentOutput)
	return filterComplex, logoInputs, nil
}

// copyLogoToOutputDir copies a logo file to the output directory
func (m *Merger) copyLogoToOutputDir(srcPath, outputDir, baseName string) string {
	if srcPath == "" {
		return ""
	}

	// Check if source file exists
	if _, err := os.Stat(srcPath); os.IsNotExist(err) {
		return ""
	}

	// Get the extension
	ext := filepath.Ext(srcPath)
	destPath := filepath.Join(outputDir, baseName+ext)

	// Copy the file
	src, err := os.Open(srcPath)
	if err != nil {
		return ""
	}
	defer func() { _ = src.Close() }()

	dst, err := os.Create(destPath)
	if err != nil {
		return ""
	}
	defer func() { _ = dst.Close() }()

	if _, err := io.Copy(dst, src); err != nil {
		return ""
	}

	return destPath
}

// escapeFFmpegText escapes special characters for FFmpeg drawtext filter
func escapeFFmpegText(text string) string {
	// Escape special characters for FFmpeg
	text = strings.ReplaceAll(text, "\\", "\\\\")
	text = strings.ReplaceAll(text, "'", "'\\''")
	text = strings.ReplaceAll(text, ":", "\\:")
	text = strings.ReplaceAll(text, "[", "\\[")
	text = strings.ReplaceAll(text, "]", "\\]")
	return text
}

// isGif checks if the file is a GIF based on extension
func isGif(path string) bool {
	ext := strings.ToLower(filepath.Ext(path))
	return ext == ".gif"
}

// appendLogoInput adds a logo input to the FFmpeg args
// For GIFs, loop behavior depends on the gifLoopMode parameter
func appendLogoInput(inputs []string, logoPath string, gifLoopMode config.GifLoopMode) []string {
	if isGif(logoPath) {
		switch gifLoopMode {
		case config.GifLoopContinuous:
			// -ignore_loop 0 makes the GIF loop forever
			return append(inputs, "-ignore_loop", "0", "-i", logoPath)
		case config.GifLoopOnce:
			// -ignore_loop 1 plays the GIF once (uses the loop count from the GIF, or plays once if none)
			return append(inputs, "-ignore_loop", "1", "-i", logoPath)
		case config.GifLoopNone:
			// No special options - FFmpeg will just use the first frame by default
			// We'll handle this in the filter by not animating
			return append(inputs, "-i", logoPath)
		default:
			// Default to continuous loop
			return append(inputs, "-ignore_loop", "0", "-i", logoPath)
		}
	}
	return append(inputs, "-i", logoPath)
}

// buildLogoOverlay builds an FFmpeg filter fragment for a single logo overlay.
// It handles both static images and GIFs (with white background for transparency).
// Parameters:
//   - inputIdx: FFmpeg input index for the logo
//   - label: unique label suffix (e.g., "logo1", "logo2", "banner")
//   - scaleExpr: scale expression (e.g., "360:-1")
//   - xExpr: x position expression (e.g., "0", "W-w")
//   - yExpr: y position expression (e.g., "0", "1280")
//   - currentOutput: current filter chain output label (e.g., "[stacked]")
//   - logoPath: path to logo file (to check if GIF)
//   - enableExpr: optional enable expression (e.g., "between(t,0,15)"), empty for always visible
//
// Returns: (filterFragment, newOutputLabel)
func buildLogoOverlay(inputIdx int, label, scaleExpr, xExpr, yExpr, currentOutput, logoPath, enableExpr string) (string, string) {
	outLabel := fmt.Sprintf("[out_%s]", label)
	enableClause := ""
	if enableExpr != "" {
		enableClause = fmt.Sprintf(":enable='%s'", enableExpr)
	}

	if isGif(logoPath) {
		// For animated GIFs: scale first, then overlay directly on the main video
		// The GIF should have its own background or transparency handled
		// Use shortest=1 to stop when main video ends, eof_action=repeat to loop GIF
		fragment := fmt.Sprintf(
			"[%d:v]scale=%s,format=rgba[%s_scaled];"+
				"%s[%s_scaled]overlay=%s:%s:format=auto:eof_action=repeat:shortest=1%s%s",
			inputIdx, scaleExpr, label,
			currentOutput, label, xExpr, yExpr, enableClause, outLabel,
		)
		return fragment, outLabel
	}

	fragment := fmt.Sprintf(
		"[%d:v]scale=%s[%s];%s[%s]overlay=%s:%s:format=auto:eof_action=repeat%s%s",
		inputIdx, scaleExpr, label, currentOutput, label, xExpr, yExpr, enableClause, outLabel,
	)
	return fragment, outLabel
}

// logoSetup holds the paths and input indices for logo overlays
type logoSetup struct {
	logo1Path       string
	logo2Path       string
	bannerPath      string
	gifLoopMode     config.GifLoopMode
	startInputIndex int // FFmpeg input index where logos start
}

// prepareMergedLogos copies logos to the output directory and appends inputs.
// Returns the logoSetup and updated inputs slice.
func (m *Merger) prepareMergedLogos(opts *MergeOptions, inputs []string, startIndex int) (logoSetup, []string) {
	setup := logoSetup{startInputIndex: startIndex}
	if opts == nil || !opts.AddLogos || opts.OutputDir == "" {
		return setup, inputs
	}

	setup.gifLoopMode = config.GifLoopContinuous
	if opts.GifLoopMode != "" {
		setup.gifLoopMode = opts.GifLoopMode
	}

	if opts.ProductLogo1 != "" {
		setup.logo1Path = m.copyLogoToOutputDir(opts.ProductLogo1, opts.OutputDir, "product_logo_1")
		if setup.logo1Path != "" {
			inputs = appendLogoInput(inputs, setup.logo1Path, setup.gifLoopMode)
		}
	}
	if opts.ProductLogo2 != "" {
		setup.logo2Path = m.copyLogoToOutputDir(opts.ProductLogo2, opts.OutputDir, "product_logo_2")
		if setup.logo2Path != "" {
			inputs = appendLogoInput(inputs, setup.logo2Path, setup.gifLoopMode)
		}
	}
	if opts.CompanyLogo != "" {
		setup.bannerPath = m.copyLogoToOutputDir(opts.CompanyLogo, opts.OutputDir, "company_logo")
		if setup.bannerPath != "" {
			inputs = appendLogoInput(inputs, setup.bannerPath, setup.gifLoopMode)
		}
	}

	return setup, inputs
}

// buildMergedOverlayFilter builds the FFmpeg filter_complex for logo overlays and
// circular webcam overlay on the merged video.
// All logo overlays are timed to show for the first 15 seconds only.
// The webcam circle overlay is shown for the full duration.
// videoWidth is the width of the input video in pixels.
func buildMergedOverlayFilter(setup logoSetup, videoWidth int, webcam webcamOverlayOpts) string {
	filter := ""
	currentOutput := "[0:v]"
	inputIdx := setup.startInputIndex
	enableExpr := "between(t,0,15)"

	// Left logo: 1/8 of video width, top-left corner
	if setup.logo1Path != "" {
		scaleW := videoWidth / 8
		fragment, out := buildLogoOverlay(inputIdx, "logo1", fmt.Sprintf("%d:-1", scaleW), "0", "0", currentOutput, setup.logo1Path, enableExpr)
		if filter != "" {
			filter += ";"
		}
		filter += fragment
		currentOutput = out
		inputIdx++
	}

	// Right logo: 1/8 of video width, top-right corner
	if setup.logo2Path != "" {
		scaleW := videoWidth / 8
		fragment, out := buildLogoOverlay(inputIdx, "logo2", fmt.Sprintf("%d:-1", scaleW), "W-w", "0", currentOutput, setup.logo2Path, enableExpr)
		if filter != "" {
			filter += ";"
		}
		filter += fragment
		currentOutput = out
		inputIdx++
	}

	// Banner: half video width, bottom-left corner
	if setup.bannerPath != "" {
		scaleW := videoWidth / 2
		fragment, out := buildLogoOverlay(inputIdx, "banner", fmt.Sprintf("%d:-1", scaleW), "0", "H-h", currentOutput, setup.bannerPath, enableExpr)
		if filter != "" {
			filter += ";"
		}
		filter += fragment
		currentOutput = out
	}

	// Circular webcam overlay: bottom-right corner, full duration
	if webcam.inputIdx >= 0 {
		fragment, out := buildWebcamCircleOverlay(webcam.inputIdx, webcam.size, webcam.margin, currentOutput)
		if filter != "" {
			filter += ";"
		}
		filter += fragment
		currentOutput = out
	}

	// Rename final output to [outv]
	if filter != "" {
		filter += fmt.Sprintf(";%snull[outv]", currentOutput)
	}

	return filter
}

// Circular webcam overlay constants
const (
	webcamOverlaySize   = 250 // Circular webcam overlay diameter in pixels
	webcamOverlayMargin = 20  // Margin from bottom-right corner
)

// webcamOverlayOpts holds parameters for the circular webcam overlay on merged video
type webcamOverlayOpts struct {
	inputIdx int // FFmpeg input index for webcam file; -1 means no webcam overlay
	size     int // diameter in pixels
	margin   int // margin from bottom-right corner in pixels
}

// buildWebcamCircleOverlay builds an FFmpeg filter fragment for a circular webcam overlay.
// It scales the webcam while preserving aspect ratio, crops to a square centered on the frame,
// applies a circular alpha mask using the geq filter, and overlays at the bottom-right.
// Returns: (filterFragment, newOutputLabel)
func buildWebcamCircleOverlay(inputIdx, size, margin int, currentOutput string) (string, string) {
	radius := size / 2
	outLabel := "[out_webcam]"
	// Scale so the smaller dimension equals size (preserving aspect ratio),
	// then crop a centered square of size x size, then apply circular mask.
	// scale='if(gt(iw,ih),(-1):size,(size):-1)' scales preserving aspect ratio
	// crop=size:size centers the crop
	fragment := fmt.Sprintf(
		"[%d:v]scale='if(gt(iw,ih),-1,%d)':'if(gt(iw,ih),%d,-1)',crop=%d:%d,format=yuva420p,"+
			"geq=lum='p(X,Y)':cb='p(X,Y)':cr='p(X,Y)':"+
			"a='if(gt((X-%d)*(X-%d)+(Y-%d)*(Y-%d),%d*%d),0,255)'[webcam_circle];"+
			"%s[webcam_circle]overlay=W-w-%d:H-h-%d%s",
		inputIdx, size, size, size, size,
		radius, radius, radius, radius, radius, radius,
		currentOutput, margin, margin, outLabel,
	)
	return fragment, outLabel
}
