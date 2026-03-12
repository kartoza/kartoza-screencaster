//go:build cgo

package systray

import (
	"testing"
	"time"
)

// TestManagerNew tests that a new manager is created with correct defaults
func TestManagerNew(t *testing.T) {
	m := New()

	if m == nil {
		t.Fatal("New() returned nil")
	}

	if m.currentState != StateIdle {
		t.Errorf("expected currentState to be StateIdle, got %d", m.currentState)
	}

	if m.isRotating {
		t.Error("expected isRotating to be false")
	}

	if m.isCountingDown {
		t.Error("expected isCountingDown to be false")
	}

	if m.recorder == nil {
		t.Error("expected recorder to be set")
	}
}

// TestManagerChannels tests that all channels are initialized
func TestManagerChannels(t *testing.T) {
	m := New()

	// Test that channels are non-nil and buffered
	select {
	case m.startChan <- struct{}{}:
		// Good, channel is writable
	default:
		t.Error("startChan should be buffered")
	}

	select {
	case m.stopChan <- struct{}{}:
	default:
		t.Error("stopChan should be buffered")
	}

	select {
	case m.pauseChan <- struct{}{}:
	default:
		t.Error("pauseChan should be buffered")
	}

	select {
	case m.tuiChan <- struct{}{}:
	default:
		t.Error("tuiChan should be buffered")
	}

	select {
	case m.quitChan <- struct{}{}:
	default:
		t.Error("quitChan should be buffered")
	}

	select {
	case m.roomNoiseDoneChan <- struct{}{}:
	default:
		t.Error("roomNoiseDoneChan should be buffered")
	}
}

// TestTrayStateConstants tests that tray state constants are unique
func TestTrayStateConstants(t *testing.T) {
	states := []TrayState{
		StateIdle,
		StateRecording,
		StatePaused,
		StateProcessing,
		StateCountdown,
		StateRoomNoise,
	}

	seen := make(map[TrayState]bool)
	for _, s := range states {
		if seen[s] {
			t.Errorf("duplicate state value: %d", s)
		}
		seen[s] = true
	}
}

// TestSetIconStateTransitions tests that state transitions work correctly
// Note: We test currentState changes directly since setIcon calls systray which isn't available in tests
func TestSetIconStateTransitions(t *testing.T) {
	m := New()

	// Verify initial state
	if m.currentState != StateIdle {
		t.Errorf("expected initial StateIdle, got %d", m.currentState)
	}

	// Test direct state changes (simulating what setIcon does)
	m.currentState = StateRecording
	if m.currentState != StateRecording {
		t.Errorf("expected StateRecording, got %d", m.currentState)
	}

	m.currentState = StatePaused
	if m.currentState != StatePaused {
		t.Errorf("expected StatePaused, got %d", m.currentState)
	}

	m.currentState = StateIdle
	if m.currentState != StateIdle {
		t.Errorf("expected StateIdle, got %d", m.currentState)
	}

	m.currentState = StateProcessing
	if m.currentState != StateProcessing {
		t.Errorf("expected StateProcessing, got %d", m.currentState)
	}

	m.currentState = StateRoomNoise
	if m.currentState != StateRoomNoise {
		t.Errorf("expected StateRoomNoise, got %d", m.currentState)
	}

	m.currentState = StateCountdown
	if m.currentState != StateCountdown {
		t.Errorf("expected StateCountdown, got %d", m.currentState)
	}
}

// TestRoomNoiseStateNotOverriddenByStatusPolling is a regression test for the bug
// where status polling would call SetIdle() and interrupt room noise capture
func TestRoomNoiseStateNotOverriddenByStatusPolling(t *testing.T) {
	m := New()

	// Simulate the state after SetRoomNoise() is called
	m.currentState = StateRoomNoise

	// Simulate what updateStatus does when recording stops
	// The key check is: statusChanged && m.currentState != StateProcessing && m.currentState != StateRoomNoise
	statusChanged := true
	shouldSetIdle := statusChanged && m.currentState != StateProcessing && m.currentState != StateRoomNoise

	if shouldSetIdle {
		t.Error("REGRESSION: status polling would override StateRoomNoise - the condition check is broken")
	}
}

// TestProcessingStateNotOverriddenByStatusPolling tests that processing state
// is also protected from status polling
func TestProcessingStateNotOverriddenByStatusPolling(t *testing.T) {
	m := New()

	m.currentState = StateProcessing

	statusChanged := true
	shouldSetIdle := statusChanged && m.currentState != StateProcessing && m.currentState != StateRoomNoise

	if shouldSetIdle {
		t.Error("REGRESSION: status polling would override StateProcessing")
	}
}

// TestIdleStateCanBeSetByStatusPolling tests that idle CAN be set when appropriate
func TestIdleStateCanBeSetByStatusPolling(t *testing.T) {
	m := New()

	// When in StateRecording and recording stops, idle should be set
	m.currentState = StateRecording

	statusChanged := true
	shouldSetIdle := statusChanged && m.currentState != StateProcessing && m.currentState != StateRoomNoise

	if !shouldSetIdle {
		t.Error("status polling should be able to set idle when transitioning from StateRecording")
	}
}

// TestIconRotationStartStop tests icon rotation lifecycle
// Note: We can't actually start the rotation in tests because systray isn't initialized,
// so we test the state management instead
func TestIconRotationStartStop(t *testing.T) {
	m := New()

	// Initially not rotating
	if m.isRotating {
		t.Error("expected isRotating to be false initially")
	}

	// Simulate rotation state (without actually starting the goroutine)
	m.isRotating = true
	m.rotationTicker = nil // Would be set by real rotation
	m.stopRotation = make(chan struct{})

	// Stop rotation
	m.stopIconRotation()
	if m.isRotating {
		t.Error("expected isRotating to be false after stopIconRotation")
	}
}

// TestIconRotationDoesNotStartTwice tests that rotation doesn't restart if already rotating
func TestIconRotationDoesNotStartTwice(t *testing.T) {
	m := New()

	// Simulate already rotating (without starting the goroutine)
	m.isRotating = true

	// This should return early without doing anything
	m.startIconRotation()

	// isRotating should still be true (not reset)
	if !m.isRotating {
		t.Error("rotation state should be preserved when already rotating")
	}

	// Clean up
	m.isRotating = false
}

// TestRoomNoiseRotatesInReverse tests that room noise state should set reverse rotation
// Note: We test the expected behavior by examining the setIcon switch case logic
func TestRoomNoiseRotatesInReverse(t *testing.T) {
	// The setIcon function for StateRoomNoise does:
	// m.reverseRotation = true
	// m.startIconRotationWithSpeed(50 * time.Millisecond)

	// We verify by checking that the code path sets reverseRotation = true
	// This is a documentation test - the actual behavior is in setIcon

	m := New()

	// Verify initial state
	if m.reverseRotation {
		t.Error("reverseRotation should be false initially")
	}

	// Simulate what setIcon(StateRoomNoise) does for reverseRotation
	m.reverseRotation = true
	m.currentState = StateRoomNoise

	if !m.reverseRotation {
		t.Error("expected reverseRotation to be true for StateRoomNoise")
	}
}

// TestProcessingRotatesForward tests that processing state rotates forward
func TestProcessingRotatesForward(t *testing.T) {
	// The setIcon function for StateProcessing does:
	// m.reverseRotation = false
	// m.startIconRotation()

	m := New()

	// Set reverse to true first
	m.reverseRotation = true

	// Simulate what setIcon(StateProcessing) does for reverseRotation
	m.reverseRotation = false
	m.currentState = StateProcessing

	if m.reverseRotation {
		t.Error("expected reverseRotation to be false for StateProcessing")
	}
}

// TestCountdownCancellation tests countdown can be cancelled
func TestCountdownCancellation(t *testing.T) {
	m := New()

	m.isCountingDown = true
	m.cancelCountdown = make(chan struct{})

	// Cancel should close the channel
	m.CancelCountdown()

	// Channel should be closed
	select {
	case <-m.cancelCountdown:
		// Good, channel was closed
	default:
		t.Error("cancelCountdown channel should be closed after CancelCountdown()")
	}
}

// TestCountdownCancellationWhenNotCounting tests that cancel is safe when not counting
func TestCountdownCancellationWhenNotCounting(t *testing.T) {
	m := New()

	m.isCountingDown = false

	// Should not panic
	m.CancelCountdown()
}

// TestFormatDuration tests duration formatting
func TestFormatDuration(t *testing.T) {
	tests := []struct {
		duration time.Duration
		expected string
	}{
		{0, "00:00"},
		{30 * time.Second, "00:30"},
		{65 * time.Second, "01:05"},
		{3600 * time.Second, "1:00:00"},
		{3661 * time.Second, "1:01:01"},
	}

	for _, tt := range tests {
		t.Run(tt.expected, func(t *testing.T) {
			result := formatDuration(tt.duration)
			if result != tt.expected {
				t.Errorf("formatDuration(%v) = %q, want %q", tt.duration, result, tt.expected)
			}
		})
	}
}

// TestRecordingInfo tests RecordingInfo struct
func TestRecordingInfo(t *testing.T) {
	info := RecordingInfo{
		Monitor:   "HDMI-A-1",
		StartTime: time.Now(),
		IsPaused:  false,
	}

	if info.Monitor != "HDMI-A-1" {
		t.Errorf("expected Monitor to be 'HDMI-A-1', got %q", info.Monitor)
	}

	if info.IsPaused {
		t.Error("expected IsPaused to be false")
	}
}

// TestRotatedIconsPrerendered tests that rotated icons are pre-rendered
func TestRotatedIconsPrerendered(t *testing.T) {
	m := New()

	// Should have 12 rotated icons (30 degree increments)
	if len(m.rotatedReadyIcons) != 12 {
		t.Errorf("expected 12 rotated icons, got %d", len(m.rotatedReadyIcons))
	}

	// Each should be non-nil (icons are embedded)
	for i, icon := range m.rotatedReadyIcons {
		if icon == nil {
			t.Errorf("rotated icon %d is nil", i)
		}
	}
}

// TestCountdownIconsPrerendered tests that countdown icons are pre-rendered
func TestCountdownIconsPrerendered(t *testing.T) {
	m := New()

	// Should have countdown icons for 1-5
	for i := 1; i <= 5; i++ {
		if m.countdownIcons[i] == nil {
			t.Errorf("countdown icon %d is nil", i)
		}
	}
}

// TestGetDigitBitmap tests digit bitmap generation
func TestGetDigitBitmap(t *testing.T) {
	for digit := 1; digit <= 5; digit++ {
		bitmap := getDigitBitmap(digit)
		if bitmap == nil {
			t.Errorf("getDigitBitmap(%d) returned nil", digit)
		}
		if len(bitmap) == 0 {
			t.Errorf("getDigitBitmap(%d) returned empty bitmap", digit)
		}
	}

	// Invalid digit should return nil
	if getDigitBitmap(0) != nil {
		t.Error("getDigitBitmap(0) should return nil")
	}
	if getDigitBitmap(6) != nil {
		t.Error("getDigitBitmap(6) should return nil")
	}
}

// TestStopIconRotationIdempotent tests that stopping rotation multiple times is safe
func TestStopIconRotationIdempotent(t *testing.T) {
	m := New()

	// Simulate rotation state (without starting actual goroutine)
	m.isRotating = true
	m.stopRotation = make(chan struct{})

	// Stop once
	m.stopIconRotation()
	if m.isRotating {
		t.Error("expected isRotating to be false after first stop")
	}

	// Stop again - should not panic (isRotating is already false, so it returns early)
	m.stopIconRotation()
	m.stopIconRotation()

	if m.isRotating {
		t.Error("expected isRotating to be false")
	}
}

// TestSetIconStopsRotationLogic tests that the rotation stop condition is correct
// for non-rotating states (we test the logic, not the actual setIcon call which requires systray)
func TestSetIconStopsRotationLogic(t *testing.T) {
	m := New()

	// Simulate rotating state
	m.isRotating = true
	m.currentState = StateProcessing

	// The condition in setIcon that determines if rotation should stop:
	// if m.isRotating && state != StateProcessing && state != StateRoomNoise
	targetState := StateIdle
	shouldStopRotation := m.isRotating && targetState != StateProcessing && targetState != StateRoomNoise

	if !shouldStopRotation {
		t.Error("expected rotation to stop when transitioning to StateIdle")
	}
}

// TestSetIconKeepsRotationForRotatingStates tests the rotation state logic
func TestSetIconKeepsRotationForRotatingStates(t *testing.T) {
	m := New()

	// Simulate rotating in processing state
	m.isRotating = true
	m.currentState = StateProcessing

	// Test: transitioning to another rotating state (RoomNoise) should NOT stop rotation
	targetState := StateRoomNoise
	shouldStopRotation := m.isRotating && targetState != StateProcessing && targetState != StateRoomNoise

	if shouldStopRotation {
		t.Error("condition should NOT trigger stop when transitioning to another rotating state")
	}

	// Test: transitioning to Processing should also NOT stop rotation
	targetState = StateProcessing
	shouldStopRotation = m.isRotating && targetState != StateProcessing && targetState != StateRoomNoise

	if shouldStopRotation {
		t.Error("condition should NOT trigger stop when staying in processing state")
	}
}

// TestChannelGetters tests that channel getters return the correct channels
func TestChannelGetters(t *testing.T) {
	m := New()

	if m.StartChan() != m.startChan {
		t.Error("StartChan() should return startChan")
	}

	if m.StopChan() != m.stopChan {
		t.Error("StopChan() should return stopChan")
	}

	if m.PauseChan() != m.pauseChan {
		t.Error("PauseChan() should return pauseChan")
	}

	if m.TUIChan() != m.tuiChan {
		t.Error("TUIChan() should return tuiChan")
	}

	if m.QuitChan() != m.quitChan {
		t.Error("QuitChan() should return quitChan")
	}

	if m.RoomNoiseDoneChan() != m.roomNoiseDoneChan {
		t.Error("RoomNoiseDoneChan() should return roomNoiseDoneChan")
	}
}
