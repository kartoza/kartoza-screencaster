//go:build linux

package webcam

import (
	"fmt"
	"os"
	"strings"
)

// DetectAllDevices finds all available webcam devices with human-readable names.
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
		name := getDeviceName(dev)
		devices = append(devices, DeviceInfo{Device: dev, Name: name})
	}
	return devices, nil
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
