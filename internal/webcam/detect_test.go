//go:build linux

package webcam

import (
	"testing"
)

func TestDetectAllDevices(t *testing.T) {
	devices, err := DetectAllDevices()
	// On CI without webcams, this should return empty list, not error
	if err != nil {
		t.Fatalf("DetectAllDevices should not error: %v", err)
	}
	for _, dev := range devices {
		if dev.Device == "" {
			t.Error("device path should not be empty")
		}
		t.Logf("Found device: %s (%s)", dev.Device, dev.Name)
	}
}

func TestDetectAllDevicesReturnsNames(t *testing.T) {
	devices, err := DetectAllDevices()
	if err != nil || len(devices) == 0 {
		t.Skip("No webcam devices available for testing")
	}
	// At minimum, Name should fall back to Device if sysfs unavailable
	for _, dev := range devices {
		if dev.Name == "" {
			t.Errorf("device %s has empty name, should fall back to device path", dev.Device)
		}
	}
}
