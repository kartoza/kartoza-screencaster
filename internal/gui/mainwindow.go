package gui

import (
	qt "github.com/mappu/miqt/qt6"
	"github.com/kartoza/kartoza-screencaster/internal/gui/pages"
)

// Page identifiers
const (
	PageRecord     = 0
	PageHistory    = 1
	PageSettings   = 2
	PageProcessing = 3
	PagePlayer     = 4
)

// MainWindow is the primary application window with sidebar navigation
type MainWindow struct {
	window *qt.QMainWindow

	// Layout
	sidebar   *qt.QWidget
	content   *qt.QStackedWidget
	statusBar *qt.QStatusBar

	// Sidebar buttons
	btnRecord   *qt.QPushButton
	btnHistory  *qt.QPushButton
	btnSettings *qt.QPushButton

	// Help text in sidebar
	helpLabel *qt.QLabel

	// Pages
	recordPage     *pages.RecordPage
	historyPage    *pages.HistoryPage
	settingsPage   *pages.SettingsPage
	processingPage *pages.ProcessingPage
	playerPage     *pages.PlayerPage

	version string
}

// NewMainWindow creates the main application window
func NewMainWindow(version string) *MainWindow {
	mw := &MainWindow{
		window:  qt.NewQMainWindow2(),
		version: version,
	}

	mw.window.SetWindowTitle("Kartoza Screencaster")
	mw.window.SetMinimumSize2(1000, 700)

	// Global stylesheet: disable tooltips and style all dialogs with dark theme
	mw.window.SetStyleSheet(`
		QToolTip { border: 0; padding: 0; background: transparent; color: transparent; max-height: 0; max-width: 0; }
		QDialog, QFileDialog, QColorDialog, QInputDialog, QMessageBox {
			background-color: #1e1e2e;
			color: #cdd6f4;
		}
		QDialog QLabel, QFileDialog QLabel, QMessageBox QLabel {
			color: #cdd6f4;
		}
		QDialog QLineEdit, QFileDialog QLineEdit {
			background: #313244;
			color: #cdd6f4;
			border: 1px solid #45475a;
			padding: 4px;
		}
		QDialog QPushButton, QFileDialog QPushButton, QMessageBox QPushButton {
			background: #45475a;
			color: #cdd6f4;
			border: 1px solid #585b70;
			border-radius: 3px;
			padding: 4px 12px;
		}
		QDialog QPushButton:hover, QFileDialog QPushButton:hover, QMessageBox QPushButton:hover {
			background: #585b70;
		}
		QDialog QComboBox, QFileDialog QComboBox {
			background: #313244;
			color: #cdd6f4;
			border: 1px solid #45475a;
			padding: 4px;
		}
		QDialog QComboBox QAbstractItemView, QFileDialog QComboBox QAbstractItemView {
			background: #313244;
			color: #cdd6f4;
			selection-background-color: #45475a;
		}
		QDialog QTreeView, QFileDialog QTreeView, QDialog QListView, QFileDialog QListView {
			background: #1e1e2e;
			color: #cdd6f4;
			border: 1px solid #45475a;
		}
		QDialog QTreeView::item:selected, QFileDialog QTreeView::item:selected,
		QDialog QListView::item:selected, QFileDialog QListView::item:selected {
			background: #45475a;
		}
		QDialog QHeaderView::section, QFileDialog QHeaderView::section {
			background: #313244;
			color: #cdd6f4;
			border: 1px solid #45475a;
			padding: 4px;
		}
		QDialog QGroupBox, QColorDialog QGroupBox {
			color: #cdd6f4;
			font-weight: bold;
		}
		QDialog QSpinBox, QColorDialog QSpinBox {
			background: #313244;
			color: #cdd6f4;
			border: 1px solid #45475a;
			padding: 2px;
		}
		QDialog QScrollBar:vertical {
			background: #1e1e2e;
			width: 12px;
		}
		QDialog QScrollBar::handle:vertical {
			background: #45475a;
			border-radius: 6px;
			min-height: 20px;
		}
	`)

	// Set window icon
	if icon := findIcon("icon_ready.png"); icon != nil {
		mw.window.SetWindowIcon(icon)
	}

	mw.setupUI()

	return mw
}

func (mw *MainWindow) setupUI() {
	// Central widget
	central := qt.NewQWidget2()
	mainLayout := qt.NewQVBoxLayout(central)
	mainLayout.SetContentsMargins(0, 0, 0, 0)
	mainLayout.SetSpacing(0)

	// Top area: sidebar + content
	topArea := qt.NewQWidget2()
	topLayout := qt.NewQHBoxLayout(topArea)
	topLayout.SetContentsMargins(0, 0, 0, 0)
	topLayout.SetSpacing(0)

	// Sidebar
	mw.sidebar = mw.createSidebar()
	topLayout.AddWidget(mw.sidebar)

	// Content stack
	mw.content = qt.NewQStackedWidget2()
	mw.content.SetStyleSheet("background-color: #1e1e2e;")

	// Create pages
	mw.recordPage = pages.NewRecordPage()
	mw.historyPage = pages.NewHistoryPage()
	mw.settingsPage = pages.NewSettingsPage()
	mw.processingPage = pages.NewProcessingPage()
	mw.playerPage = pages.NewPlayerPage()

	mw.content.AddWidget(mw.recordPage.Widget())
	mw.content.AddWidget(mw.historyPage.Widget())
	mw.content.AddWidget(mw.settingsPage.Widget())
	mw.content.AddWidget(mw.processingPage.Widget())
	mw.content.AddWidget(mw.playerPage.Widget())

	topLayout.AddWidget(mw.content.QFrame.QWidget)
	mainLayout.AddWidget(topArea)

	// Footer
	footer := mw.createFooter()
	mainLayout.AddWidget(footer)

	// Status bar
	mw.statusBar = qt.NewQStatusBar2()
	mw.statusBar.ShowMessage("Idle")
	mw.window.SetStatusBar(mw.statusBar)

	mw.window.SetCentralWidget(central)

	// Wire Record page recording callbacks — hide window on start, show on stop
	mw.recordPage.SetRecordingCallbacks(
		func() { mw.Hide() },   // onStart: hide window
		func() {                 // onStop: show window
			mw.Show()
			mw.Raise()
		},
	)

	// Wire Record page navigation callback
	mw.recordPage.SetNavigateCallback(func(page int) {
		mw.navigateTo(page)
		if page == PageProcessing {
			// Start processing and pipe progress to the processing page
			ch := mw.recordPage.GetProgressChannel()
			mw.processingPage.StartProcessing(ch)
		}
	})

	// Wire Processing page completion callback
	mw.processingPage.OnComplete(func(success bool) {
		if success {
			mw.historyPage.Refresh()
			mw.navigateTo(PageHistory)
			mw.SetStatus("Processing complete")
		} else {
			mw.SetStatus("Processing failed")
		}
	})

	// Start on Record page
	mw.navigateTo(PageRecord)

	// Poll for widget under cursor to update help text
	mw.startHelpPoller()
}

func (mw *MainWindow) startHelpPoller() {
	lastHelp := ""
	timer := qt.NewQTimer()
	timer.SetInterval(200)
	timer.OnTimeout(func() {
		pos := qt.QCursor_Pos()
		widget := qt.QApplication_WidgetAt(pos)
		if widget == nil {
			return
		}
		tip := widget.ToolTip()
		// Walk up parents to find a tooltip if this widget doesn't have one
		if tip == "" {
			parent := widget.ParentWidget()
			for parent != nil && tip == "" {
				tip = parent.ToolTip()
				parent = parent.ParentWidget()
			}
		}
		if tip != "" && tip != lastHelp {
			lastHelp = tip
			mw.helpLabel.SetText(tip)
		}
	})
	timer.Start2()
}

func (mw *MainWindow) createSidebar() *qt.QWidget {
	sidebar := qt.NewQWidget2()
	sidebar.SetFixedWidth(200)
	sidebar.SetStyleSheet(`
		QWidget {
			background-color: #181825;
			color: #cdd6f4;
		}
		QPushButton {
			background-color: transparent;
			color: #cdd6f4;
			border: none;
			padding: 12px 20px;
			text-align: left;
			font-size: 14px;
		}
		QPushButton:hover {
			background-color: #313244;
		}
		QPushButton:checked {
			background-color: #45475a;
			color: #89b4fa;
			font-weight: bold;
		}
	`)

	layout := qt.NewQVBoxLayout(sidebar)
	layout.SetContentsMargins(0, 0, 0, 0)
	layout.SetSpacing(0)

	// App title
	title := qt.NewQLabel3("Kartoza\nScreencaster")
	title.SetStyleSheet(`
		QLabel {
			color: #89b4fa;
			font-size: 16px;
			font-weight: bold;
			padding: 20px;
		}
	`)
	title.SetAlignment(qt.AlignCenter)
	layout.AddWidget(title.QFrame.QWidget)

	// Version
	ver := qt.NewQLabel3("v" + mw.version)
	ver.SetStyleSheet("QLabel { color: #6c7086; font-size: 11px; padding: 0 20px 15px 20px; }")
	ver.SetAlignment(qt.AlignCenter)
	layout.AddWidget(ver.QFrame.QWidget)

	// Navigation buttons
	mw.btnRecord = qt.NewQPushButton3("Record")
	mw.btnRecord.SetCheckable(true)
	mw.btnRecord.SetChecked(true)
	mw.btnRecord.SetToolTip("Set up and start a new recording.\nConfigure sources, monitors, webcams,\nand output options.")
	mw.btnRecord.OnClicked(func() {
		mw.navigateTo(PageRecord)
	})
	layout.AddWidget(mw.btnRecord.QAbstractButton.QWidget)

	mw.btnHistory = qt.NewQPushButton3("History")
	mw.btnHistory.SetCheckable(true)
	mw.btnHistory.SetToolTip("Browse past recordings.\nPlay, reprocess, or delete\ncompleted recordings.")
	mw.btnHistory.OnClicked(func() {
		mw.navigateTo(PageHistory)
	})
	layout.AddWidget(mw.btnHistory.QAbstractButton.QWidget)

	mw.btnSettings = qt.NewQPushButton3("Settings")
	mw.btnSettings.SetCheckable(true)
	mw.btnSettings.SetToolTip("Configure application settings.\nOutput directory, audio processing,\nlogos, YouTube, and topics.")
	mw.btnSettings.OnClicked(func() {
		mw.navigateTo(PageSettings)
	})
	layout.AddWidget(mw.btnSettings.QAbstractButton.QWidget)

	// Spacer
	layout.AddStretch()

	// Help text area at bottom of sidebar
	separator := qt.NewQFrame2()
	separator.SetFrameShape(qt.QFrame__HLine)
	separator.SetStyleSheet("QFrame { color: #313244; }")
	layout.AddWidget(separator.QWidget)

	helpTitle := qt.NewQLabel3("Help")
	helpTitle.SetStyleSheet("QLabel { color: #585b70; font-size: 11px; font-weight: bold; padding: 5px 10px 0 10px; }")
	layout.AddWidget(helpTitle.QFrame.QWidget)

	mw.helpLabel = qt.NewQLabel3("Hover over any option\nfor help.")
	mw.helpLabel.SetStyleSheet("QLabel { color: #6c7086; font-size: 11px; padding: 2px 10px 10px 10px; }")
	mw.helpLabel.SetWordWrap(true)
	mw.helpLabel.SetMinimumHeight(80)
	mw.helpLabel.SetAlignment(qt.AlignTop)
	layout.AddWidget(mw.helpLabel.QFrame.QWidget)

	return sidebar
}

func (mw *MainWindow) createFooter() *qt.QWidget {
	footer := qt.NewQWidget2()
	footer.SetFixedHeight(30)
	footer.SetStyleSheet(`
		QWidget { background-color: #11111b; }
		QLabel { color: #6c7086; font-size: 11px; }
	`)

	layout := qt.NewQHBoxLayout(footer)
	layout.SetContentsMargins(10, 0, 10, 0)

	label := qt.NewQLabel3(`Made with ❤️ by <a href="https://kartoza.com" style="color:#89b4fa;">Kartoza</a> | <a href="https://github.com/sponsors/kartoza" style="color:#89b4fa;">Donate!</a> | <a href="https://github.com/kartoza/kartoza-screencaster" style="color:#89b4fa;">GitHub</a>`)
	label.SetAlignment(qt.AlignCenter)
	label.SetOpenExternalLinks(true)
	label.SetTextFormat(qt.RichText)
	layout.AddWidget(label.QFrame.QWidget)

	return footer
}

func (mw *MainWindow) navigateTo(page int) {
	mw.content.SetCurrentIndex(page)

	// Update button states
	mw.btnRecord.SetChecked(page == PageRecord)
	mw.btnHistory.SetChecked(page == PageHistory)
	mw.btnSettings.SetChecked(page == PageSettings)
}

// Show shows the main window
func (mw *MainWindow) Show() {
	mw.window.Show()
}

// Hide hides the main window
func (mw *MainWindow) Hide() {
	mw.window.Hide()
}

// IsVisible returns whether the main window is visible
func (mw *MainWindow) IsVisible() bool {
	return mw.window.IsVisible()
}

// Raise brings the window to the front
func (mw *MainWindow) Raise() {
	mw.window.Raise()
	mw.window.ActivateWindow()
}

// SetHelpText updates the sidebar help text
func (mw *MainWindow) SetHelpText(text string) {
	mw.helpLabel.SetText(text)
}

// SetStatus updates the status bar message
func (mw *MainWindow) SetStatus(msg string) {
	mw.statusBar.ShowMessage(msg)
}

// NavigateTo switches to a specific page
func (mw *MainWindow) NavigateTo(page int) {
	mw.navigateTo(page)
}

// QMainWindow returns the underlying Qt main window
func (mw *MainWindow) QMainWindow() *qt.QMainWindow {
	return mw.window
}
