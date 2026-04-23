package gui

import (
	qt "github.com/mappu/miqt/qt6"
)

// RunOnMainThread schedules a function to run on the Qt main thread.
// Safe to call from goroutines. Uses a zero-interval single-shot QTimer.
func RunOnMainThread(fn func()) {
	timer := qt.NewQTimer()
	timer.SetSingleShot(true)
	timer.OnTimeout(func() {
		fn()
	})
	timer.Start(0)
}
