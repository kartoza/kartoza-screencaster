package pages

import (
	qt "github.com/mappu/miqt/qt6"
)

// runOnUI schedules a function to run on the Qt main thread.
// Safe to call from goroutines.
func runOnUI(fn func()) {
	timer := qt.NewQTimer()
	timer.SetSingleShot(true)
	timer.OnTimeout(func() {
		fn()
	})
	timer.Start(0)
}
