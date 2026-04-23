package pages

// This file intentionally left minimal.
// Cross-thread communication uses flag-based polling via the canvas OnTick callback,
// not channels or Qt timers from goroutines.
// See the RecordPage.pendingStartDone / pendingStopDone pattern.
