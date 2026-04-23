package gui

import (
	"os"

	qt "github.com/mappu/miqt/qt6"
)

// App is the main GUI application
type App struct {
	qapp       *qt.QApplication
	mainWindow *MainWindow
	tray       *Tray
}

// NewApp creates the Qt application and main window
func NewApp(version string) *App {
	qapp := qt.NewQApplication(os.Args)
	qt.QCoreApplication_SetApplicationName("Kartoza Screencaster")
	qt.QCoreApplication_SetApplicationVersion(version)
	qt.QCoreApplication_SetOrganizationName("Kartoza")
	qt.QCoreApplication_SetOrganizationDomain("kartoza.com")

	a := &App{
		qapp: qapp,
	}

	a.mainWindow = NewMainWindow(version)
	a.tray = NewTray(a.mainWindow)

	return a
}

// Run starts the Qt event loop
func (a *App) Run() int {
	a.mainWindow.Show()
	a.tray.Show()
	return qt.QApplication_Exec()
}
