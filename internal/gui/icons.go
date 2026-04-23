package gui

import (
	"os"
	"path/filepath"

	qt "github.com/mappu/miqt/qt6"
)

// findIcon searches for an icon file in common locations
func findIcon(name string) *qt.QIcon {
	// Search paths in priority order
	paths := []string{
		// Relative to working directory
		filepath.Join("resources", name),
		// Absolute from working directory
		absIcon("resources", name),
		// Relative to executable
		execIcon(name),
		// System-installed locations
		filepath.Join("/usr/share/icons/hicolor/scalable/apps", name),
		filepath.Join("/usr/local/share/icons/hicolor/scalable/apps", name),
	}

	for _, p := range paths {
		if p == "" {
			continue
		}
		if _, err := os.Stat(p); err == nil {
			icon := qt.NewQIcon4(p)
			return icon
		}
	}

	return nil
}

// absIcon returns an absolute path from working directory
func absIcon(dir, name string) string {
	wd, err := os.Getwd()
	if err != nil {
		return ""
	}
	return filepath.Join(wd, dir, name)
}

// execIcon returns the icon path relative to the executable
func execIcon(name string) string {
	exe, err := os.Executable()
	if err != nil {
		return ""
	}
	return filepath.Join(filepath.Dir(exe), "resources", name)
}
