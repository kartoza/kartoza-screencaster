package webcam

import (
	"os"
	"path/filepath"
	"testing"
)

func TestNewManager(t *testing.T) {
	configs := []WebcamConfig{
		{Device: "video0", Enabled: true, LandscapeMode: DisplayBubble, LandscapeSide: SideRight, VerticalMode: DisplayBottomThird},
		{Device: "video2", Enabled: true, LandscapeMode: DisplayBubble, LandscapeSide: SideLeft, VerticalMode: DisplayBubble},
		{Device: "video4", Enabled: false, LandscapeMode: DisplayOff, LandscapeSide: SideRight, VerticalMode: DisplayOff},
	}

	mgr := NewManager(configs, 60, "1920x1080")
	if mgr == nil {
		t.Fatal("NewManager returned nil")
	}
	if len(mgr.configs) != 3 {
		t.Errorf("expected 3 configs, got %d", len(mgr.configs))
	}
}

func TestManagerEnabledConfigs(t *testing.T) {
	configs := []WebcamConfig{
		{Device: "video0", Enabled: true, LandscapeMode: DisplayBubble, LandscapeSide: SideRight, VerticalMode: DisplayBottomThird},
		{Device: "video2", Enabled: false, LandscapeMode: DisplayOff, LandscapeSide: SideRight, VerticalMode: DisplayOff},
	}

	mgr := NewManager(configs, 60, "1920x1080")
	enabled := mgr.EnabledConfigs()
	if len(enabled) != 1 {
		t.Errorf("expected 1 enabled config, got %d", len(enabled))
	}
	if enabled[0].Device != "video0" {
		t.Errorf("expected video0, got %s", enabled[0].Device)
	}
}

func TestManagerOutputFilePaths(t *testing.T) {
	configs := []WebcamConfig{
		{Device: "video0", Enabled: true, LandscapeMode: DisplayBubble, LandscapeSide: SideRight, VerticalMode: DisplayBottomThird},
		{Device: "video2", Enabled: true, LandscapeMode: DisplayRectangle, LandscapeSide: SideLeft, VerticalMode: DisplayBubble},
	}

	mgr := NewManager(configs, 60, "1920x1080")
	dir := t.TempDir()
	paths := mgr.OutputFilePaths(dir, 0)

	if len(paths) != 2 {
		t.Fatalf("expected 2 paths, got %d", len(paths))
	}

	expected0 := filepath.Join(dir, "webcam_video0_part000.mp4")
	expected2 := filepath.Join(dir, "webcam_video2_part000.mp4")

	if paths["video0"] != expected0 {
		t.Errorf("expected %s, got %s", expected0, paths["video0"])
	}
	if paths["video2"] != expected2 {
		t.Errorf("expected %s, got %s", expected2, paths["video2"])
	}
}

func TestManagerGetOutputs(t *testing.T) {
	configs := []WebcamConfig{
		{Device: "video0", Enabled: true, LandscapeMode: DisplayBubble, LandscapeSide: SideRight, VerticalMode: DisplayBottomThird},
		{Device: "video2", Enabled: true, LandscapeMode: DisplayRectangle, LandscapeSide: SideLeft, VerticalMode: DisplayBubble},
	}

	mgr := NewManager(configs, 60, "1920x1080")
	dir := t.TempDir()

	// Create fake output files so GetOutputs finds them
	for _, path := range mgr.OutputFilePaths(dir, 0) {
		_ = os.WriteFile(path, []byte("fake"), 0644)
	}
	mgr.outputDir = dir
	mgr.partNum = 0

	outputs := mgr.GetOutputs()
	if len(outputs) != 2 {
		t.Fatalf("expected 2 outputs, got %d", len(outputs))
	}

	if outputs[0].Device != "video0" || outputs[0].LandscapeMode != DisplayBubble {
		t.Errorf("unexpected output[0]: %+v", outputs[0])
	}
	if outputs[1].Device != "video2" || outputs[1].LandscapeMode != DisplayRectangle {
		t.Errorf("unexpected output[1]: %+v", outputs[1])
	}
}

func TestMergeConfigsWithDetected(t *testing.T) {
	saved := []WebcamConfig{
		{Device: "video0", Enabled: true, LandscapeMode: DisplayBubble, LandscapeSide: SideRight, VerticalMode: DisplayBottomThird},
	}
	detected := []DeviceInfo{
		{Device: "video0", Name: "Logitech C920"},
		{Device: "video2", Name: "Built-in Camera"},
	}

	merged := MergeConfigsWithDetected(saved, detected)
	if len(merged) != 2 {
		t.Fatalf("expected 2 merged configs, got %d", len(merged))
	}

	// video0 should retain saved config
	if merged[0].LandscapeMode != DisplayBubble {
		t.Errorf("expected saved config for video0, got %s", merged[0].LandscapeMode)
	}

	// video2 should get defaults
	if merged[1].LandscapeMode != DisplayBubble || merged[1].LandscapeSide != SideRight {
		t.Errorf("expected default config for video2, got %+v", merged[1])
	}
	if !merged[1].Enabled {
		t.Error("new devices should be enabled by default")
	}
}
