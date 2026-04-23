package pages

// UIQueue holds functions to be executed on the main Qt thread.
// Goroutines push to this channel; the canvas refresh timer drains it.
var UIQueue = make(chan func(), 100)

// runOnUI schedules a function to run on the Qt main thread.
// Safe to call from any goroutine.
func runOnUI(fn func()) {
	select {
	case UIQueue <- fn:
	default:
		// Queue full — shouldn't happen with 100 capacity
	}
}
