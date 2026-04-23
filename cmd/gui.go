package cmd

import (
	"fmt"
	"os"

	"github.com/kartoza/kartoza-screencaster/internal/gui"
	"github.com/spf13/cobra"
)

var guiCmd = &cobra.Command{
	Use:   "gui",
	Short: "Run the Qt6 graphical user interface",
	Long: `Run the Kartoza Screencaster with the Qt6 GUI.

The GUI provides a full-featured graphical interface with:
  - Live webcam preview
  - Visual monitor picker
  - Drag-and-drop logo selection
  - Built-in video player
  - Real-time processing progress

This is the default mode when running kartoza-screencaster.`,
	Run: func(cmd *cobra.Command, args []string) {
		app := gui.NewApp(version)
		code := app.Run()
		if code != 0 {
			fmt.Fprintf(os.Stderr, "GUI exited with code %d\n", code)
			os.Exit(code)
		}
	},
}

func init() {
	rootCmd.AddCommand(guiCmd)
}
