package pages

import (
	qt "github.com/mappu/miqt/qt6"
)

// openColorDialog opens a QColorDialog with a light theme so text is readable.
// Returns the selected color name (hex), or empty string if cancelled.
func openColorDialog(parent *qt.QWidget, title string, initial string) string {
	initialColor := qt.NewQColor6(initial)
	dlg := qt.NewQColorDialog4(initialColor, parent)
	dlg.SetWindowTitle(title)

	// Dark mode styling with light text
	dlg.SetStyleSheet(`
		QColorDialog, QDialog, QWidget {
			background-color: #1e1e2e;
			color: #cdd6f4;
		}
		QLabel {
			color: #cdd6f4;
		}
		QLineEdit, QSpinBox {
			background: #313244;
			color: #cdd6f4;
			border: 1px solid #45475a;
			padding: 2px;
		}
		QPushButton {
			background: #45475a;
			color: #cdd6f4;
			border: 1px solid #585b70;
			border-radius: 3px;
			padding: 4px 12px;
		}
		QPushButton:hover {
			background: #585b70;
		}
		QGroupBox {
			color: #cdd6f4;
			font-weight: bold;
		}
	`)

	result := dlg.QDialog.Exec()
	if result == 1 { // QDialog::Accepted
		selected := dlg.SelectedColor()
		if selected.IsValid() {
			return selected.Name()
		}
	}
	return ""
}
