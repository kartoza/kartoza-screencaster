package monitor

import (
	"encoding/json"
	"fmt"
	"os/exec"
	"regexp"
	"strconv"
	"strings"

	"github.com/kartoza/kartoza-screencaster/internal/deps"
	"github.com/kartoza/kartoza-screencaster/internal/models"
)

// ListMonitors returns all available monitors
func ListMonitors() ([]models.Monitor, error) {
	switch deps.DetectDisplayServer() {
	case deps.DisplayServerWayland:
		return listMonitorsWayland()
	case deps.DisplayServerX11:
		return listMonitorsX11()
	default:
		// Try Wayland first, then X11
		monitors, err := listMonitorsWayland()
		if err == nil {
			return monitors, nil
		}
		return listMonitorsX11()
	}
}

// listMonitorsWayland returns monitors using compositor-specific tools
func listMonitorsWayland() ([]models.Monitor, error) {
	// Try niri first (if XDG_CURRENT_DESKTOP is niri)
	if monitors, err := listMonitorsNiri(); err == nil {
		return monitors, nil
	}

	// Try hyprctl (Hyprland)
	if monitors, err := listMonitorsHyprland(); err == nil {
		return monitors, nil
	}

	// Try swaymsg (Sway)
	if monitors, err := listMonitorsSway(); err == nil {
		return monitors, nil
	}

	// Try cosmic-randr (COSMIC desktop)
	if monitors, err := listMonitorsCosmic(); err == nil {
		return monitors, nil
	}

	// Try GNOME Mutter DBus interface
	if monitors, err := listMonitorsGnome(); err == nil {
		return monitors, nil
	}

	// Fallback: return a single default monitor
	// wl-screenrec will use the only display if there's one
	return []models.Monitor{{
		Name:    "",
		Width:   1920,
		Height:  1080,
		X:       0,
		Y:       0,
		Focused: true,
	}}, nil
}

// listMonitorsCosmic returns monitors using cosmic-randr
func listMonitorsCosmic() ([]models.Monitor, error) {
	cmd := exec.Command("cosmic-randr", "list")
	output, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("cosmic-randr not available: %w", err)
	}

	var monitors []models.Monitor
	lines := strings.Split(string(output), "\n")

	var current *models.Monitor
	first := true
	for _, line := range lines {
		// Strip ANSI escape codes
		clean := stripAnsi(line)
		trimmed := strings.TrimSpace(clean)

		// New output starts with a non-indented name like "DP-9" or "eDP-1"
		if len(clean) > 0 && clean[0] != ' ' && !strings.HasPrefix(trimmed, "Make:") {
			// Extract output name (first word)
			parts := strings.Fields(trimmed)
			if len(parts) > 0 && (strings.Contains(parts[0], "-") || strings.HasPrefix(parts[0], "HDMI") || strings.HasPrefix(parts[0], "VGA")) {
				if current != nil {
					monitors = append(monitors, *current)
				}
				current = &models.Monitor{
					Name:    parts[0],
					Focused: first,
				}
				first = false
			}
		}

		if current == nil {
			continue
		}

		if strings.HasPrefix(trimmed, "Make:") {
			make := strings.TrimSpace(strings.TrimPrefix(trimmed, "Make:"))
			if current.Description == "" {
				current.Description = make
			}
		}
		if strings.HasPrefix(trimmed, "Model:") {
			model := strings.TrimSpace(strings.TrimPrefix(trimmed, "Model:"))
			if current.Description != "" {
				current.Description += " " + model
			} else {
				current.Description = model
			}
		}
		// Parse current mode line like "1920x1080 @ 60.000 Hz (current)"
		if strings.Contains(trimmed, "(current)") {
			re := regexp.MustCompile(`(\d+)x(\d+)`)
			matches := re.FindStringSubmatch(trimmed)
			if len(matches) >= 3 {
				current.Width, _ = strconv.Atoi(matches[1])
				current.Height, _ = strconv.Atoi(matches[2])
			}
		}
		if strings.HasPrefix(trimmed, "Position:") {
			posStr := strings.TrimSpace(strings.TrimPrefix(trimmed, "Position:"))
			parts := strings.Split(posStr, ",")
			if len(parts) >= 2 {
				current.X, _ = strconv.Atoi(strings.TrimSpace(parts[0]))
				current.Y, _ = strconv.Atoi(strings.TrimSpace(parts[1]))
			}
		}
	}
	if current != nil {
		monitors = append(monitors, *current)
	}

	if len(monitors) == 0 {
		return nil, fmt.Errorf("no monitors found via cosmic-randr")
	}

	return monitors, nil
}

// stripAnsi removes ANSI escape sequences from a string
func stripAnsi(s string) string {
	re := regexp.MustCompile(`\x1b\[[0-9;]*m`)
	return re.ReplaceAllString(s, "")
}

// listMonitorsNiri returns monitors using niri msg
func listMonitorsNiri() ([]models.Monitor, error) {
	cmd := exec.Command("niri", "msg", "-j", "outputs")
	output, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("failed to run niri msg outputs: %w", err)
	}

	// niri outputs JSON format: {"eDP-1": {"name": "eDP-1", "logical": {"x": 0, "y": 0, "width": 1706, "height": 1066, ...}, ...}}
	var niriOutputs map[string]struct {
		Name    string `json:"name"`
		Logical *struct {
			X      int     `json:"x"`
			Y      int     `json:"y"`
			Width  int     `json:"width"`
			Height int     `json:"height"`
			Scale  float64 `json:"scale"`
		} `json:"logical"`
		Modes []struct {
			Width     int  `json:"width"`
			Height    int  `json:"height"`
			Preferred bool `json:"is_preferred"`
		} `json:"modes"`
		CurrentMode int `json:"current_mode"`
	}

	if err := json.Unmarshal(output, &niriOutputs); err != nil {
		return nil, fmt.Errorf("failed to parse niri outputs JSON: %w", err)
	}

	var monitors []models.Monitor
	first := true
	for name, out := range niriOutputs {
		mon := models.Monitor{
			Name:    name,
			Focused: first, // Mark first monitor as focused (niri doesn't expose focus in outputs)
		}

		// Use logical dimensions if available (scaled)
		if out.Logical != nil {
			mon.X = out.Logical.X
			mon.Y = out.Logical.Y
			mon.Width = out.Logical.Width
			mon.Height = out.Logical.Height
		} else if len(out.Modes) > 0 && out.CurrentMode >= 0 && out.CurrentMode < len(out.Modes) {
			// Fall back to current mode dimensions
			mode := out.Modes[out.CurrentMode]
			mon.Width = mode.Width
			mon.Height = mode.Height
		}

		monitors = append(monitors, mon)
		first = false
	}

	if len(monitors) == 0 {
		return nil, fmt.Errorf("no monitors found via niri")
	}

	return monitors, nil
}

// listMonitorsHyprland returns monitors using hyprctl
func listMonitorsHyprland() ([]models.Monitor, error) {
	cmd := exec.Command("hyprctl", "monitors", "-j")
	output, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("failed to run hyprctl monitors: %w", err)
	}

	var monitors []models.Monitor
	if err := json.Unmarshal(output, &monitors); err != nil {
		return nil, fmt.Errorf("failed to parse monitors JSON: %w", err)
	}

	return monitors, nil
}

// listMonitorsSway returns monitors using swaymsg
func listMonitorsSway() ([]models.Monitor, error) {
	cmd := exec.Command("swaymsg", "-t", "get_outputs", "-r")
	output, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("failed to run swaymsg: %w", err)
	}

	// swaymsg output format is similar to hyprctl
	var swayOutputs []struct {
		Name    string `json:"name"`
		Rect    struct {
			X      int `json:"x"`
			Y      int `json:"y"`
			Width  int `json:"width"`
			Height int `json:"height"`
		} `json:"rect"`
		Focused bool `json:"focused"`
	}

	if err := json.Unmarshal(output, &swayOutputs); err != nil {
		return nil, fmt.Errorf("failed to parse sway outputs JSON: %w", err)
	}

	var monitors []models.Monitor
	for _, out := range swayOutputs {
		monitors = append(monitors, models.Monitor{
			Name:    out.Name,
			X:       out.Rect.X,
			Y:       out.Rect.Y,
			Width:   out.Rect.Width,
			Height:  out.Rect.Height,
			Focused: out.Focused,
		})
	}

	if len(monitors) == 0 {
		return nil, fmt.Errorf("no monitors found via sway")
	}

	return monitors, nil
}

// listMonitorsGnome returns monitors using GNOME Mutter DBus interface
func listMonitorsGnome() ([]models.Monitor, error) {
	// Call: gdbus call --session --dest org.gnome.Mutter.DisplayConfig \
	//   --object-path /org/gnome/Mutter/DisplayConfig \
	//   --method org.gnome.Mutter.DisplayConfig.GetCurrentState
	cmd := exec.Command("gdbus", "call", "--session",
		"--dest", "org.gnome.Mutter.DisplayConfig",
		"--object-path", "/org/gnome/Mutter/DisplayConfig",
		"--method", "org.gnome.Mutter.DisplayConfig.GetCurrentState")
	output, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("failed to call Mutter DBus: %w", err)
	}

	// Parse the output - it's a GVariant tuple
	// Format: (serial, [(connector, vendor, product, serial_string), [modes], props], [logical_monitors], props)
	// We need to extract connector name and current mode dimensions
	outputStr := string(output)

	var monitors []models.Monitor

	// Extract monitor info using regex patterns
	// Looking for patterns like: ('eDP-1', 'BOE', '0x0bc9', '0x00000000')
	// and mode: ('2560x1600@165.000', 2560, 1600, 165.0, ...
	connectorRe := regexp.MustCompile(`\('([^']+)', '[^']*', '[^']*', '[^']*'\), \[\('(\d+)x(\d+)@[^']+', (\d+), (\d+),`)

	matches := connectorRe.FindAllStringSubmatch(outputStr, -1)
	for i, match := range matches {
		if len(match) >= 6 {
			name := match[1]
			width, _ := strconv.Atoi(match[4])
			height, _ := strconv.Atoi(match[5])

			monitors = append(monitors, models.Monitor{
				Name:    name,
				Width:   width,
				Height:  height,
				X:       0, // GNOME DBus doesn't easily expose position in this call
				Y:       0,
				Focused: i == 0, // Mark first as focused
			})
		}
	}

	if len(monitors) == 0 {
		return nil, fmt.Errorf("no monitors found via GNOME DBus")
	}

	return monitors, nil
}

// listMonitorsX11 returns monitors using xrandr (X11)
func listMonitorsX11() ([]models.Monitor, error) {
	cmd := exec.Command("xrandr", "--query")
	output, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("failed to run xrandr: %w", err)
	}

	var monitors []models.Monitor
	lines := strings.Split(string(output), "\n")

	// Pattern: "DP-0 connected primary 1920x1080+0+0 ..."
	// or: "HDMI-0 connected 1920x1080+1920+0 ..."
	re := regexp.MustCompile(`^(\S+)\s+connected\s+(primary\s+)?(\d+)x(\d+)\+(\d+)\+(\d+)`)

	for _, line := range lines {
		matches := re.FindStringSubmatch(line)
		if matches == nil {
			continue
		}

		name := matches[1]
		isPrimary := matches[2] != ""
		width, _ := strconv.Atoi(matches[3])
		height, _ := strconv.Atoi(matches[4])
		x, _ := strconv.Atoi(matches[5])
		y, _ := strconv.Atoi(matches[6])

		monitors = append(monitors, models.Monitor{
			Name:    name,
			Width:   width,
			Height:  height,
			X:       x,
			Y:       y,
			Focused: isPrimary, // Use primary as "focused" for X11
		})
	}

	if len(monitors) == 0 {
		return nil, fmt.Errorf("no monitors found via xrandr")
	}

	return monitors, nil
}

// GetCursorPosition returns the current cursor position
func GetCursorPosition() (models.CursorPosition, error) {
	switch deps.DetectDisplayServer() {
	case deps.DisplayServerWayland:
		return getCursorPositionWayland()
	case deps.DisplayServerX11:
		return getCursorPositionX11()
	default:
		// Try Wayland first
		pos, err := getCursorPositionWayland()
		if err == nil {
			return pos, nil
		}
		return getCursorPositionX11()
	}
}

// getCursorPositionWayland gets cursor position using compositor-specific tools
func getCursorPositionWayland() (models.CursorPosition, error) {
	// Try hyprctl first (Hyprland)
	if pos, err := getCursorPositionHyprland(); err == nil {
		return pos, nil
	}

	// For niri and sway, cursor position isn't directly exposed
	// Fall back to returning an error to use focused monitor instead
	return models.CursorPosition{}, fmt.Errorf("cursor position not available")
}

// getCursorPositionHyprland gets cursor position using hyprctl
func getCursorPositionHyprland() (models.CursorPosition, error) {
	cmd := exec.Command("hyprctl", "cursorpos")
	output, err := cmd.Output()
	if err != nil {
		return models.CursorPosition{}, fmt.Errorf("failed to get cursor position: %w", err)
	}

	// Parse format: "x, y"
	parts := strings.Split(strings.TrimSpace(string(output)), ",")
	if len(parts) != 2 {
		return models.CursorPosition{}, fmt.Errorf("unexpected cursor position format: %s", output)
	}

	x, err := strconv.Atoi(strings.TrimSpace(parts[0]))
	if err != nil {
		return models.CursorPosition{}, fmt.Errorf("failed to parse cursor X: %w", err)
	}

	y, err := strconv.Atoi(strings.TrimSpace(parts[1]))
	if err != nil {
		return models.CursorPosition{}, fmt.Errorf("failed to parse cursor Y: %w", err)
	}

	return models.CursorPosition{X: x, Y: y}, nil
}

// getCursorPositionX11 gets cursor position using xdotool
func getCursorPositionX11() (models.CursorPosition, error) {
	cmd := exec.Command("xdotool", "getmouselocation", "--shell")
	output, err := cmd.Output()
	if err != nil {
		return models.CursorPosition{}, fmt.Errorf("failed to get cursor position: %w", err)
	}

	// Parse format: "X=123\nY=456\nSCREEN=0\nWINDOW=..."
	var x, y int
	for _, line := range strings.Split(string(output), "\n") {
		if strings.HasPrefix(line, "X=") {
			x, _ = strconv.Atoi(strings.TrimPrefix(line, "X="))
		} else if strings.HasPrefix(line, "Y=") {
			y, _ = strconv.Atoi(strings.TrimPrefix(line, "Y="))
		}
	}

	return models.CursorPosition{X: x, Y: y}, nil
}

// GetMouseMonitor returns the name of the monitor containing the mouse cursor
func GetMouseMonitor() (string, error) {
	pos, err := GetCursorPosition()
	if err != nil {
		// Fallback to focused monitor
		return GetFocusedMonitor()
	}

	monitors, err := ListMonitors()
	if err != nil {
		return "", err
	}

	for _, m := range monitors {
		if m.ContainsCursor(pos) {
			return m.Name, nil
		}
	}

	// If no monitor found, return focused monitor
	return GetFocusedMonitor()
}

// GetFocusedMonitor returns the name of the currently focused monitor
func GetFocusedMonitor() (string, error) {
	// Try niri's focused-output first
	if name, err := getFocusedMonitorNiri(); err == nil {
		return name, nil
	}

	monitors, err := ListMonitors()
	if err != nil {
		return "", err
	}

	for _, m := range monitors {
		if m.Focused {
			return m.Name, nil
		}
	}

	// Return first monitor if none focused
	if len(monitors) > 0 {
		return monitors[0].Name, nil
	}

	return "", fmt.Errorf("no monitors found")
}

// getFocusedMonitorNiri gets focused monitor using niri msg
func getFocusedMonitorNiri() (string, error) {
	cmd := exec.Command("niri", "msg", "-j", "focused-output")
	output, err := cmd.Output()
	if err != nil {
		return "", fmt.Errorf("failed to run niri msg focused-output: %w", err)
	}

	var focusedOutput struct {
		Name string `json:"name"`
	}

	if err := json.Unmarshal(output, &focusedOutput); err != nil {
		return "", fmt.Errorf("failed to parse niri focused-output JSON: %w", err)
	}

	if focusedOutput.Name == "" {
		return "", fmt.Errorf("no focused output name in niri response")
	}

	return focusedOutput.Name, nil
}

// GetMonitorByName returns the monitor with the given name
func GetMonitorByName(name string) (*models.Monitor, error) {
	monitors, err := ListMonitors()
	if err != nil {
		return nil, err
	}

	for _, m := range monitors {
		if m.Name == name {
			return &m, nil
		}
	}

	return nil, fmt.Errorf("monitor not found: %s", name)
}
