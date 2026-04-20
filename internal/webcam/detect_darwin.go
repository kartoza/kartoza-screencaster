//go:build darwin

package webcam

import (
	"fmt"
	"os/exec"
	"strings"
)

// DetectAllDevices finds all available webcam devices on macOS.
func DetectAllDevices() ([]DeviceInfo, error) {
	cmd := exec.Command("ffmpeg", "-f", "avfoundation", "-list_devices", "true", "-i", "")
	output, _ := cmd.CombinedOutput() // ffmpeg exits non-zero for -list_devices

	var devices []DeviceInfo
	lines := strings.Split(string(output), "\n")
	inVideoSection := false
	deviceIdx := 0
	for _, line := range lines {
		if strings.Contains(line, "AVFoundation video devices") {
			inVideoSection = true
			continue
		}
		if strings.Contains(line, "AVFoundation audio devices") {
			break
		}
		if inVideoSection && strings.Contains(line, "]") {
			// Extract device name from line like "[AVFoundation ...] [0] FaceTime HD Camera"
			parts := strings.SplitN(line, "] ", 2)
			if len(parts) >= 2 {
				name := strings.TrimSpace(parts[len(parts)-1])
				devices = append(devices, DeviceInfo{
					Device: fmt.Sprintf("%d", deviceIdx),
					Name:   name,
				})
				deviceIdx++
			}
		}
	}
	return devices, nil
}
