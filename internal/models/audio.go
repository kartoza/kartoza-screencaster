package models

// LoudnormStats contains the measured audio levels from analysis
type LoudnormStats struct {
	InputI       string `json:"input_i"`
	InputTP      string `json:"input_tp"`
	InputLRA     string `json:"input_lra"`
	InputThresh  string `json:"input_thresh"`
	OutputI      string `json:"output_i"`
	OutputTP     string `json:"output_tp"`
	OutputLRA    string `json:"output_lra"`
	OutputThresh string `json:"output_thresh"`
	TargetOffset string `json:"target_offset"`
}

// AudioProcessingOptions contains options for audio post-processing
// Uses jivetalking for professional broadcast-quality audio processing
type AudioProcessingOptions struct {
	// NormalizeEnabled enables the full audio processing pipeline
	// When enabled, audio goes through jivetalking's 4-pass processing:
	// Pass 1: Analysis (loudness, noise floor, speech characteristics)
	// Pass 2: Processing (high-pass, noise removal, gate, compression, de-esser)
	// Pass 3: Loudnorm measurement
	// Pass 4: Loudnorm application + click repair
	NormalizeEnabled bool

	// TargetLoudness is the target integrated loudness in LUFS
	// -18 LUFS is the podcast standard (EBU R128)
	// -14 LUFS is louder, good for screen recordings
	TargetLoudness float64

	// TruePeak is the maximum true peak level in dBTP
	// -1.5 dBTP is the podcast standard, prevents clipping on playback
	TruePeak float64

	// LoudnessRange is the target loudness range in LU
	// 11 LU preserves natural dynamic range
	LoudnessRange float64

	// NoiseRemovalEnabled enables adaptive noise removal
	// Uses jivetalking's anlmdn + compand noise reduction and DS201-style gate
	NoiseRemovalEnabled bool

	// CompressionEnabled enables LA-2A style optical compression
	// Evens out dynamics for consistent speech levels
	CompressionEnabled bool

	// DeesserEnabled enables adaptive de-essing
	// Reduces harsh sibilance automatically
	DeesserEnabled bool

	// RoomNoiseFile is the path to a room noise recording for noise profile calibration
	// If provided, jivetalking uses this to better identify and remove background noise
	RoomNoiseFile string
}

// DefaultAudioProcessingOptions returns sensible defaults for audio processing
func DefaultAudioProcessingOptions() AudioProcessingOptions {
	return AudioProcessingOptions{
		NormalizeEnabled:    true,
		TargetLoudness:      -18.0, // Podcast standard (EBU R128)
		TruePeak:            -1.5,  // Prevents clipping
		LoudnessRange:       11.0,  // Preserves dynamic range
		NoiseRemovalEnabled: true,  // Enable noise removal by default
		CompressionEnabled:  true,  // Enable compression by default
		DeesserEnabled:      true,  // Enable de-esser by default
	}
}
