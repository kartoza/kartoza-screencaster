package gui

import (
	qt "github.com/mappu/miqt/qt6"
)

// Tray manages the Qt6 system tray icon
type Tray struct {
	trayIcon   *qt.QSystemTrayIcon
	mainWindow *MainWindow

	// Context menu and dynamic actions
	menu          *qt.QMenu
	startAction   *qt.QAction
	pauseAction   *qt.QAction
	stopAction    *qt.QAction
	openAction    *qt.QAction
	quitAction    *qt.QAction

	// Recording state tracking
	recording bool
	paused    bool
}

// NewTray creates a new system tray icon
func NewTray(mainWindow *MainWindow) *Tray {
	t := &Tray{
		mainWindow: mainWindow,
	}

	// Create tray icon — must set icon before Show() for Cosmic/Wayland
	icon := findIcon("icon_ready.png")
	if icon != nil {
		t.trayIcon = qt.NewQSystemTrayIcon2(icon)
	} else {
		// Fallback: use application icon or a theme icon
		t.trayIcon = qt.NewQSystemTrayIcon()
		themeIcon := qt.QIcon_FromTheme("camera-video")
		t.trayIcon.SetIcon(themeIcon)
	}
	t.trayIcon.SetToolTip("Kartoza Screencaster - Idle")

	t.setupMenu()
	t.setupSignals()

	// Listen for recording state changes from the record page
	mainWindow.recordPage.SetStatusCallback(func(status string) {
		t.onRecordingStateChanged(status)
	})

	return t
}

func (t *Tray) setupMenu() {
	t.menu = qt.NewQMenu2()

	t.startAction = t.menu.AddActionWithText("Start Recording")
	t.pauseAction = t.menu.AddActionWithText("Pause")
	t.stopAction = t.menu.AddActionWithText("Stop Recording")
	t.menu.AddSeparator()
	t.openAction = t.menu.AddActionWithText("Open Window")
	t.menu.AddSeparator()
	t.quitAction = t.menu.AddActionWithText("Quit")

	// Initially hide recording controls
	t.pauseAction.SetVisible(false)
	t.stopAction.SetVisible(false)

	// Start recording: open window to record page
	t.startAction.OnTriggered(func() {
		t.mainWindow.Show()
		t.mainWindow.Raise()
		t.mainWindow.NavigateTo(PageRecord)
	})

	// Pause/Resume
	t.pauseAction.OnTriggered(func() {
		if t.paused {
			t.mainWindow.recordPage.TriggerResume()
		} else {
			t.mainWindow.recordPage.TriggerPause()
		}
	})

	// Stop — also show the window for processing
	t.stopAction.OnTriggered(func() {
		t.mainWindow.recordPage.TriggerStop()
	})

	// Open window
	t.openAction.OnTriggered(func() {
		t.mainWindow.Show()
		t.mainWindow.Raise()
	})

	// Quit
	t.quitAction.OnTriggered(func() {
		// Stop webcam previews before quitting
		t.mainWindow.recordPage.StopPreviews()
		qt.QCoreApplication_Exit()
	})

	t.trayIcon.SetContextMenu(t.menu)
}

func (t *Tray) setupSignals() {
	t.trayIcon.OnActivated(func(reason qt.QSystemTrayIcon__ActivationReason) {
		switch reason {
		case qt.QSystemTrayIcon__Trigger:
			// Single click
			if t.recording {
				// Pause/resume during recording
				if t.paused {
					t.mainWindow.recordPage.TriggerResume()
				} else {
					t.mainWindow.recordPage.TriggerPause()
				}
			} else {
				// Toggle window when idle
				if t.mainWindow.IsVisible() {
					t.mainWindow.Hide()
				} else {
					t.mainWindow.Show()
					t.mainWindow.Raise()
				}
			}
		case qt.QSystemTrayIcon__DoubleClick:
			// Double click: stop recording if active
			if t.recording {
				t.mainWindow.recordPage.TriggerStop()
			}
		}
	})
}

func (t *Tray) onRecordingStateChanged(status string) {
	switch status {
	case "Recording":
		t.recording = true
		t.paused = false
		t.startAction.SetVisible(false)
		t.pauseAction.SetVisible(true)
		t.pauseAction.SetText("Pause")
		t.stopAction.SetVisible(true)
		t.trayIcon.SetToolTip("Kartoza Screencaster - Recording")
		if icon := findIcon("icon_recording.png"); icon != nil {
			t.trayIcon.SetIcon(icon)
		}
	case "Paused":
		t.recording = true
		t.paused = true
		t.pauseAction.SetText("Resume")
		t.trayIcon.SetToolTip("Kartoza Screencaster - Paused")
		if icon := findIcon("icon_paused.png"); icon != nil {
			t.trayIcon.SetIcon(icon)
		}
	case "Idle":
		t.recording = false
		t.paused = false
		t.startAction.SetVisible(true)
		t.pauseAction.SetVisible(false)
		t.stopAction.SetVisible(false)
		t.trayIcon.SetToolTip("Kartoza Screencaster - Idle")
		if icon := findIcon("icon_ready.png"); icon != nil {
			t.trayIcon.SetIcon(icon)
		}
	}
}

// Show shows the tray icon
func (t *Tray) Show() {
	t.trayIcon.Show()
}
