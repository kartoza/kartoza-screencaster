package webcam

// WebcamDisplayMode defines how a webcam feed is shown in output
type WebcamDisplayMode string

const (
	DisplayOff         WebcamDisplayMode = "off"
	DisplayBubble      WebcamDisplayMode = "bubble"
	DisplayRectangle   WebcamDisplayMode = "rectangle"    // landscape: strip below main video
	DisplayBottomThird WebcamDisplayMode = "bottom_third" // vertical: middle section
)

// WebcamSide defines which side a bubble is positioned on
type WebcamSide string

const (
	SideLeft  WebcamSide = "left"
	SideRight WebcamSide = "right"
)

// WebcamConfig holds per-webcam persistent configuration
type WebcamConfig struct {
	Device        string            `json:"device"`
	Enabled       bool              `json:"enabled"`
	LandscapeMode WebcamDisplayMode `json:"landscape_mode"`
	LandscapeSide WebcamSide        `json:"landscape_side"`
	VerticalMode  WebcamDisplayMode `json:"vertical_mode"`
	OverlayX      int               `json:"overlay_x,omitempty"`    // custom X position in video coords (-1 = auto)
	OverlayY      int               `json:"overlay_y,omitempty"`    // custom Y position in video coords (-1 = auto)
	OverlaySize   int               `json:"overlay_size,omitempty"` // diameter/width in video pixels (0 = default 250)
}

// DefaultWebcamConfig returns the default config for a newly detected device
func DefaultWebcamConfig(device string) WebcamConfig {
	return WebcamConfig{
		Device:        device,
		Enabled:       true,
		LandscapeMode: DisplayBubble,
		LandscapeSide: SideRight,
		VerticalMode:  DisplayBubble,
	}
}

// WebcamOutput represents a recorded webcam file with its display configuration
type WebcamOutput struct {
	File          string
	Device        string
	LandscapeMode WebcamDisplayMode
	LandscapeSide WebcamSide
	VerticalMode  WebcamDisplayMode
}

// DeviceInfo holds detected webcam device information
type DeviceInfo struct {
	Device string // e.g. "video0"
	Name   string // human-readable, e.g. "Logitech C920"
}
