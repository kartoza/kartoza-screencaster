//go:build linux

package webcam

import (
	"fmt"
	"os"
	"strings"
)

// DetectAllDevices finds all available webcam capture devices with human-readable names.
// Filters out metadata/non-capture devices by checking the sysfs index (only index 0 is a capture device).
// Returns an empty slice (not error) if no devices are found.
func DetectAllDevices() ([]DeviceInfo, error) {
	var devices []DeviceInfo
	for i := 0; i < 10; i++ {
		dev := fmt.Sprintf("video%d", i)
		path := "/dev/" + dev
		info, err := os.Stat(path)
		if err != nil {
			continue
		}
		if info.Mode()&os.ModeCharDevice == 0 {
			continue
		}
		// Only include capture devices (index 0), skip metadata devices (index 1+)
		if !isCaptureDevice(dev) {
			continue
		}
		name := getDeviceName(dev)
		devices = append(devices, DeviceInfo{Device: dev, Name: name})
	}
	return devices, nil
}

// isCaptureDevice checks if the device is a video capture device (index 0)
// rather than a metadata stream (index 1+).
func isCaptureDevice(dev string) bool {
	indexPath := fmt.Sprintf("/sys/class/video4linux/%s/index", dev)
	data, err := os.ReadFile(indexPath)
	if err != nil {
		return true // if we can't read, assume it's a capture device
	}
	return strings.TrimSpace(string(data)) == "0"
}

// getDeviceName reads the human-readable name from sysfs.
// Falls back to the device path if sysfs is unavailable.
func getDeviceName(dev string) string {
	sysfsPath := fmt.Sprintf("/sys/class/video4linux/%s/name", dev)
	data, err := os.ReadFile(sysfsPath)
	if err != nil {
		return dev // fallback to device path
	}
	name := strings.TrimSpace(string(data))
	if name == "" {
		return dev
	}
	return name
}
