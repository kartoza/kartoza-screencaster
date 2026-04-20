//go:build windows

package webcam

import (
	"os/exec"
	"strings"
)

// DetectAllDevices finds all available webcam devices on Windows.
func DetectAllDevices() ([]DeviceInfo, error) {
	cmd := exec.Command("ffmpeg", "-f", "dshow", "-list_devices", "true", "-i", "dummy")
	output, _ := cmd.CombinedOutput()

	var devices []DeviceInfo
	lines := strings.Split(string(output), "\n")
	for i, line := range lines {
		if strings.Contains(line, "(video)") {
			// Previous line contains device name in quotes
			if i > 0 {
				nameLine := lines[i-1]
				parts := strings.SplitN(nameLine, "\"", 3)
				if len(parts) >= 3 {
					name := parts[1]
					devices = append(devices, DeviceInfo{
						Device: "video=" + name,
						Name:   name,
					})
				}
			}
		}
	}
	return devices, nil
}
