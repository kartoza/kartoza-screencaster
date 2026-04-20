package merger

import (
	"strings"
	"testing"

	"github.com/kartoza/kartoza-screencaster/internal/webcam"
)

func TestBuildMultiBubbleFilter(t *testing.T) {
	outputs := []webcamOverlayMulti{
		{inputIdx: 2, side: webcam.SideRight, size: 250, margin: 20, stackIndex: 0},
		{inputIdx: 3, side: webcam.SideRight, size: 250, margin: 20, stackIndex: 1},
	}

	filter := buildMultiBubbleFilter(outputs, "[base]")

	// Should contain two geq (circular mask) operations
	if strings.Count(filter, "geq=") != 2 {
		t.Errorf("expected 2 geq operations, got %d in: %s", strings.Count(filter, "geq="), filter)
	}

	// Second bubble should be offset vertically: margin + (size+gap)*1 = 20 + (250+20)*1 = 290
	if !strings.Contains(filter, "H-h-290") {
		t.Errorf("expected vertical offset H-h-290 for second bubble, got: %s", filter)
	}

	// First bubble at H-h-20 (just margin)
	if !strings.Contains(filter, "H-h-20") {
		t.Errorf("expected H-h-20 for first bubble, got: %s", filter)
	}
}

func TestBuildMultiBubbleFilterLeftSide(t *testing.T) {
	outputs := []webcamOverlayMulti{
		{inputIdx: 2, side: webcam.SideLeft, size: 250, margin: 20, stackIndex: 0},
	}

	filter := buildMultiBubbleFilter(outputs, "[base]")

	// Left side should use margin for X position, not W-w-margin
	if !strings.Contains(filter, "overlay=20:") {
		t.Errorf("expected left-side positioning (overlay=20:), got: %s", filter)
	}
}

func TestBuildMultiBubbleFilterRightSide(t *testing.T) {
	outputs := []webcamOverlayMulti{
		{inputIdx: 2, side: webcam.SideRight, size: 250, margin: 20, stackIndex: 0},
	}

	filter := buildMultiBubbleFilter(outputs, "[base]")

	// Right side should use W-w-margin for X position
	if !strings.Contains(filter, "overlay=W-w-20:") {
		t.Errorf("expected right-side positioning (overlay=W-w-20:), got: %s", filter)
	}
}

func TestBuildRectangleStripFilter(t *testing.T) {
	outputs := []webcamRectInput{
		{inputIdx: 2, device: "video0"},
		{inputIdx: 3, device: "video2"},
	}

	filter := buildRectangleStripFilter(outputs, 1920, "[base]")

	// Should contain hstack for combining rectangles
	if !strings.Contains(filter, "hstack") {
		t.Errorf("expected hstack in filter, got: %s", filter)
	}
	// Should contain vstack for combining with main video
	if !strings.Contains(filter, "vstack") {
		t.Errorf("expected vstack in filter, got: %s", filter)
	}
	// Each should be scaled to half width (1920/2 = 960)
	if !strings.Contains(filter, "scale=960:-1") {
		t.Errorf("expected scale=960:-1, got: %s", filter)
	}
}

func TestBuildRectangleStripFilterSingle(t *testing.T) {
	outputs := []webcamRectInput{
		{inputIdx: 2, device: "video0"},
	}

	filter := buildRectangleStripFilter(outputs, 1920, "[base]")

	// Single rectangle should NOT use hstack
	if strings.Contains(filter, "hstack") {
		t.Errorf("single rectangle should not use hstack, got: %s", filter)
	}
	// Should still use vstack
	if !strings.Contains(filter, "vstack") {
		t.Errorf("expected vstack in filter, got: %s", filter)
	}
}

func TestBuildVerticalBottomThirdFilter(t *testing.T) {
	outputs := []webcamRectInput{
		{inputIdx: 1, device: "video0"},
		{inputIdx: 2, device: "video2"},
	}

	filter := buildVerticalBottomThirdFilter(outputs, 1080, "[base]")

	// Should contain hstack for multiple webcams
	if !strings.Contains(filter, "hstack") {
		t.Errorf("expected hstack in filter, got: %s", filter)
	}
	// Should contain overlay positioning
	if !strings.Contains(filter, "overlay=") {
		t.Errorf("expected overlay in filter, got: %s", filter)
	}
	// Each webcam should be 540px wide (1080/2)
	if !strings.Contains(filter, "scale=540:-1") {
		t.Errorf("expected scale=540:-1, got: %s", filter)
	}
}

func TestBuildVerticalBottomThirdFilterSingle(t *testing.T) {
	outputs := []webcamRectInput{
		{inputIdx: 1, device: "video0"},
	}

	filter := buildVerticalBottomThirdFilter(outputs, 1080, "[base]")

	// Single webcam should NOT use hstack
	if strings.Contains(filter, "hstack") {
		t.Errorf("single webcam should not use hstack, got: %s", filter)
	}
	// Should contain overlay
	if !strings.Contains(filter, "overlay=") {
		t.Errorf("expected overlay in filter, got: %s", filter)
	}
}
