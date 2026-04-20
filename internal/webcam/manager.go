package webcam

import (
	"fmt"
	"os"
	"path/filepath"
	"sync"
)

// Manager manages multiple webcam recorders
type Manager struct {
	configs    []WebcamConfig
	webcams    map[string]*Webcam // keyed by device
	fps        int
	resolution string
	outputDir  string
	partNum    int
	mu         sync.Mutex
}

// NewManager creates a new webcam manager with the given per-device configs
func NewManager(configs []WebcamConfig, fps int, resolution string) *Manager {
	return &Manager{
		configs:    configs,
		webcams:    make(map[string]*Webcam),
		fps:        fps,
		resolution: resolution,
	}
}

// EnabledConfigs returns only the enabled webcam configs
func (m *Manager) EnabledConfigs() []WebcamConfig {
	var enabled []WebcamConfig
	for _, cfg := range m.configs {
		if cfg.Enabled {
			enabled = append(enabled, cfg)
		}
	}
	return enabled
}

// OutputFilePaths returns the expected output file paths for all enabled webcams
func (m *Manager) OutputFilePaths(outputDir string, partNum int) map[string]string {
	paths := make(map[string]string)
	for _, cfg := range m.EnabledConfigs() {
		paths[cfg.Device] = filepath.Join(outputDir, fmt.Sprintf("webcam_%s_part%03d.mp4", cfg.Device, partNum))
	}
	return paths
}

// StartAll starts recording from all enabled webcams concurrently
func (m *Manager) StartAll(outputDir string, partNum int) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	m.outputDir = outputDir
	m.partNum = partNum

	enabled := m.EnabledConfigs()
	if len(enabled) == 0 {
		return fmt.Errorf("no enabled webcam devices")
	}

	var wg sync.WaitGroup
	errCh := make(chan error, len(enabled))

	for _, cfg := range enabled {
		outputFile := filepath.Join(outputDir, fmt.Sprintf("webcam_%s_part%03d.mp4", cfg.Device, partNum))
		opts := Options{
			Device:     cfg.Device,
			FPS:        m.fps,
			Resolution: m.resolution,
			OutputFile: outputFile,
		}

		wg.Add(1)
		go func(c WebcamConfig, o Options) {
			defer wg.Done()
			cam := New(o)
			if err := cam.Start(); err != nil {
				errCh <- fmt.Errorf("webcam %s: %w", c.Device, err)
				return
			}
			m.mu.Lock()
			m.webcams[c.Device] = cam
			m.mu.Unlock()
		}(cfg, opts)
	}

	wg.Wait()
	close(errCh)

	// Collect errors but don't fail if at least one webcam started
	var errs []error
	for err := range errCh {
		errs = append(errs, err)
	}

	if len(m.webcams) == 0 && len(errs) > 0 {
		return errs[0]
	}

	return nil
}

// StopAll stops all webcam recordings gracefully
func (m *Manager) StopAll() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	var lastErr error
	for dev, cam := range m.webcams {
		if err := cam.Stop(); err != nil {
			lastErr = fmt.Errorf("failed to stop webcam %s: %w", dev, err)
		}
	}
	m.webcams = make(map[string]*Webcam)
	return lastErr
}

// PIDs returns all active webcam process IDs
func (m *Manager) PIDs() map[string]int {
	m.mu.Lock()
	defer m.mu.Unlock()

	pids := make(map[string]int)
	for dev, cam := range m.webcams {
		if cam.PID() > 0 {
			pids[dev] = cam.PID()
		}
	}
	return pids
}

// IsRecording returns true if any webcam is currently recording
func (m *Manager) IsRecording() bool {
	m.mu.Lock()
	defer m.mu.Unlock()
	return len(m.webcams) > 0
}

// GetOutputs returns the output files with their display configurations
func (m *Manager) GetOutputs() []WebcamOutput {
	paths := m.OutputFilePaths(m.outputDir, m.partNum)

	var outputs []WebcamOutput
	for _, cfg := range m.EnabledConfigs() {
		filePath := paths[cfg.Device]
		if _, err := os.Stat(filePath); err != nil {
			continue
		}
		outputs = append(outputs, WebcamOutput{
			File:          filePath,
			Device:        cfg.Device,
			LandscapeMode: cfg.LandscapeMode,
			LandscapeSide: cfg.LandscapeSide,
			VerticalMode:  cfg.VerticalMode,
		})
	}
	return outputs
}

// MergeConfigsWithDetected merges persisted configs with currently detected devices.
// New devices get default config. Missing devices are preserved in config but skipped at runtime.
func MergeConfigsWithDetected(saved []WebcamConfig, detected []DeviceInfo) []WebcamConfig {
	savedMap := make(map[string]WebcamConfig)
	for _, cfg := range saved {
		savedMap[cfg.Device] = cfg
	}

	var merged []WebcamConfig
	for _, dev := range detected {
		if cfg, ok := savedMap[dev.Device]; ok {
			merged = append(merged, cfg)
		} else {
			merged = append(merged, DefaultWebcamConfig(dev.Device))
		}
	}
	return merged
}
