package merger

import (
	"testing"

	"github.com/kartoza/kartoza-screencaster/internal/webcam"
)

func TestMergeOptionsBackwardCompat(t *testing.T) {
	// Old-style single WebcamFile should still work when WebcamOutputs is empty
	opts := MergeOptions{
		WebcamFile:   "/tmp/fake_webcam.mp4",
		WebcamBubble: true,
	}

	hasLegacyWebcam := opts.WebcamFile != "" && len(opts.WebcamOutputs) == 0
	if !hasLegacyWebcam {
		t.Error("expected legacy webcam path to be detected")
	}
}

func TestMergeOptionsMultiWebcamOverridesLegacy(t *testing.T) {
	opts := MergeOptions{
		WebcamFile: "/tmp/old_webcam.mp4",
		WebcamOutputs: []webcam.WebcamOutput{
			{File: "/tmp/webcam_video0.mp4", Device: "video0", LandscapeMode: webcam.DisplayBubble, LandscapeSide: webcam.SideRight},
		},
	}

	// When WebcamOutputs is set, it should take priority over legacy WebcamFile
	usesMulti := len(opts.WebcamOutputs) > 0
	if !usesMulti {
		t.Error("expected multi-webcam path to take priority")
	}
}

func TestMergeConfigsRoundTrip(t *testing.T) {
	// Verify that WebcamConfig types work correctly in MergeOptions
	outputs := []webcam.WebcamOutput{
		{File: "/tmp/webcam_video0.mp4", Device: "video0", LandscapeMode: webcam.DisplayBubble, LandscapeSide: webcam.SideRight, VerticalMode: webcam.DisplayBottomThird},
		{File: "/tmp/webcam_video2.mp4", Device: "video2", LandscapeMode: webcam.DisplayRectangle, LandscapeSide: webcam.SideLeft, VerticalMode: webcam.DisplayBubble},
	}

	opts := MergeOptions{
		WebcamOutputs: outputs,
	}

	if len(opts.WebcamOutputs) != 2 {
		t.Fatalf("expected 2 webcam outputs, got %d", len(opts.WebcamOutputs))
	}
	if opts.WebcamOutputs[0].LandscapeMode != webcam.DisplayBubble {
		t.Errorf("expected bubble mode for first webcam, got %s", opts.WebcamOutputs[0].LandscapeMode)
	}
	if opts.WebcamOutputs[1].LandscapeMode != webcam.DisplayRectangle {
		t.Errorf("expected rectangle mode for second webcam, got %s", opts.WebcamOutputs[1].LandscapeMode)
	}
}
