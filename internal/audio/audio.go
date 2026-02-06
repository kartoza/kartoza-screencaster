package audio

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"

	"github.com/kartoza/kartoza-screencaster/internal/models"
	"github.com/kartoza/kartoza-screencaster/internal/notify"
)

// Note: Recorder is defined in platform-specific files:
// - audio_linux.go: uses pw-record (PipeWire)
// - audio_darwin.go: uses ffmpeg with avfoundation
// - audio_windows.go: uses ffmpeg with dshow

// Processor handles audio post-processing using ffmpeg
type Processor struct {
	options models.AudioProcessingOptions
}

// NewProcessor creates a new audio processor
func NewProcessor(opts models.AudioProcessingOptions) *Processor {
	return &Processor{options: opts}
}

// ProgressCallback is a function that receives progress updates during processing
type ProgressCallback func(pass int, passName string, progress float64)

// Process performs audio processing pipeline using ffmpeg
// Returns the path to the processed output file
func (p *Processor) Process(inputFile string, progressCallback ProgressCallback) (string, error) {
	if !p.options.NormalizeEnabled {
		// No processing enabled, return original file
		return inputFile, nil
	}

	_ = notify.ProcessingStep("Processing audio with ffmpeg...")

	// Step 1: Analyze
	if progressCallback != nil {
		progressCallback(1, "Analyzing", 0.0)
	}

	stats, err := p.AnalyzeLoudness(inputFile)
	if err != nil {
		// Return original file on analysis error
		if progressCallback != nil {
			progressCallback(1, "Analyzing", 1.0)
			progressCallback(2, "Normalizing", 1.0)
		}
		return inputFile, nil
	}

	if progressCallback != nil {
		progressCallback(1, "Analyzing", 1.0)
	}

	// Step 2: Normalize
	if progressCallback != nil {
		progressCallback(2, "Normalizing", 0.0)
	}

	outputFile := strings.TrimSuffix(inputFile, filepath.Ext(inputFile)) + "-normalized.wav"
	if err := p.Normalize(inputFile, outputFile, stats); err != nil {
		// Return original file on normalization error
		if progressCallback != nil {
			progressCallback(2, "Normalizing", 1.0)
		}
		return inputFile, nil
	}

	if progressCallback != nil {
		progressCallback(2, "Normalizing", 1.0)
	}

	return outputFile, nil
}

// AnalyzeLoudness performs first-pass loudnorm analysis using ffmpeg
func (p *Processor) AnalyzeLoudness(inputFile string) (*models.LoudnormStats, error) {
	filter := fmt.Sprintf("loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:print_format=json",
		p.options.TargetLoudness,
		p.options.TruePeak,
		p.options.LoudnessRange,
	)

	cmd := exec.Command("ffmpeg",
		"-nostdin", // Don't wait for stdin
		"-i", inputFile,
		"-af", filter,
		"-f", "null",
		"-",
	)

	output, err := cmd.CombinedOutput()
	if err != nil {
		return nil, fmt.Errorf("loudness analysis failed: %w", err)
	}

	// Extract JSON from ffmpeg output
	stats, err := parseLoudnormOutput(string(output))
	if err != nil {
		return nil, err
	}

	return stats, nil
}

// Normalize performs two-pass loudness normalization using ffmpeg
func (p *Processor) Normalize(inputFile, outputFile string, stats *models.LoudnormStats) error {
	filter := fmt.Sprintf(
		"loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:measured_I=%s:measured_TP=%s:measured_LRA=%s:measured_thresh=%s:linear=true:print_format=summary",
		p.options.TargetLoudness,
		p.options.TruePeak,
		p.options.LoudnessRange,
		stats.InputI,
		stats.InputTP,
		stats.InputLRA,
		stats.InputThresh,
	)

	cmd := exec.Command("ffmpeg",
		"-nostdin", // Don't wait for stdin
		"-y",
		"-i", inputFile,
		"-af", filter,
		"-c:a", "pcm_s16le",
		outputFile,
	)

	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("normalization failed: %w, output: %s", err, output)
	}

	return nil
}

// fileExists checks if a file exists
func fileExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

// parseLoudnormOutput extracts loudnorm stats from ffmpeg output
func parseLoudnormOutput(output string) (*models.LoudnormStats, error) {
	// Find JSON block in output
	re := regexp.MustCompile(`\{[^}]+\}`)
	matches := re.FindAllString(output, -1)

	if len(matches) == 0 {
		return nil, fmt.Errorf("no loudnorm stats found in output")
	}

	// Use the last JSON block (should be the loudnorm stats)
	// Parse individual values using regex since JSON might not be perfect
	stats := extractLoudnormValues(output)

	return &stats, nil
}

// extractLoudnormValues extracts values from ffmpeg output using regex
func extractLoudnormValues(output string) models.LoudnormStats {
	stats := models.LoudnormStats{}

	patterns := map[string]*string{
		`"input_i"\s*:\s*"([^"]+)"`:       &stats.InputI,
		`"input_tp"\s*:\s*"([^"]+)"`:      &stats.InputTP,
		`"input_lra"\s*:\s*"([^"]+)"`:     &stats.InputLRA,
		`"input_thresh"\s*:\s*"([^"]+)"`:  &stats.InputThresh,
		`"output_i"\s*:\s*"([^"]+)"`:      &stats.OutputI,
		`"output_tp"\s*:\s*"([^"]+)"`:     &stats.OutputTP,
		`"output_lra"\s*:\s*"([^"]+)"`:    &stats.OutputLRA,
		`"output_thresh"\s*:\s*"([^"]+)"`: &stats.OutputThresh,
		`"target_offset"\s*:\s*"([^"]+)"`: &stats.TargetOffset,
	}

	for pattern, target := range patterns {
		re := regexp.MustCompile(pattern)
		if matches := re.FindStringSubmatch(output); len(matches) > 1 {
			*target = matches[1]
		}
	}

	return stats
}
