package pages

import (
	qt "github.com/mappu/miqt/qt6"
)

// uiQueue holds functions to be executed on the main Qt thread
var uiQueue = make(chan func(), 100)

// uiPollTimer is initialized once to drain the queue
var uiPollTimer *qt.QTimer

// initUIQueue starts the main-thread poller that drains queued UI updates.
// Must be called once from the main thread (e.g., during page setup).
func initUIQueue() {
	if uiPollTimer != nil {
		return // already initialized
	}
	uiPollTimer = qt.NewQTimer()
	uiPollTimer.SetInterval(50) // 20Hz polling
	uiPollTimer.OnTimeout(func() {
		for {
			select {
			case fn := <-uiQueue:
				fn()
			default:
				return
			}
		}
	})
	uiPollTimer.Start2()
}

// runOnUI schedules a function to run on the Qt main thread.
// Safe to call from any goroutine. The function will execute
// within ~50ms on the next timer tick.
func runOnUI(fn func()) {
	select {
	case uiQueue <- fn:
	default:
		// Queue full — drop (shouldn't happen with 100 capacity)
	}
}
