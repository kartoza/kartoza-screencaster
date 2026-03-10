package tui

import (
	"fmt"

	"github.com/charmbracelet/lipgloss"
)

// ========================================
// Brand Colors - Kartoza standard palette
// ========================================

var (
	ColorOrange   = lipgloss.Color("#DDA036") // Primary/Active
	ColorBlue     = lipgloss.Color("#569FC6") // Secondary/Links
	ColorGray     = lipgloss.Color("#9A9EA0") // Inactive/Subtle
	ColorWhite    = lipgloss.Color("#FFFFFF") // Text
	ColorDarkGray = lipgloss.Color("#3A3A3A") // Background
	ColorRed      = lipgloss.Color("#E95420") // Error/Recording
	ColorGreen    = lipgloss.Color("#4CAF50") // Success
)

// HeaderWidth is the standard width for the header
const HeaderWidth = 60

// ========================================
// Global Application State for Header
// ========================================

// AppState contains the global application state shown in all headers
type AppState struct {
	IsRecording      bool
	TotalRecordings  int
	Status           string // e.g., "Ready", "Processing", "Recording"
	BlinkOn          bool   // For blinking recording indicator
	YouTubeConnected bool   // Whether YouTube API is connected
	Version          string // Application version
}

// Global app state - updated by the main app model
var GlobalAppState = &AppState{
	IsRecording:     false,
	TotalRecordings: 0,
	Status:          "Ready",
	BlinkOn:         true,
	Version:         "0.8.0",
}

// ========================================
// Header Rendering (DRY Implementation)
// ========================================

// RenderHeader renders the standard application header for ALL pages
// pageTitle is the name of the current page (e.g., "Main Menu", "New Recording")
//
// Format:
//
//	Kartoza Video Processor - Page Title
//	Serva Momentum
//	────────────────────────────────────────────────────────────
//	Recording: Off | Total Recordings: 10 | Status: Ready
//	────────────────────────────────────────────────────────────
func RenderHeader(pageTitle string) string {
	titleStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(ColorOrange).
		Align(lipgloss.Center).
		Width(HeaderWidth)

	mottoStyle := lipgloss.NewStyle().
		Italic(true).
		Foreground(ColorGray).
		Align(lipgloss.Center).
		Width(HeaderWidth)

	dividerStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Align(lipgloss.Center).
		Width(HeaderWidth)

	statusStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Align(lipgloss.Center).
		Width(HeaderWidth)

	// Line 1: Application Name - Page Title - Version
	title := titleStyle.Render(fmt.Sprintf("Kartoza Video Processor v%s - %s", GlobalAppState.Version, pageTitle))

	// Line 2: Motto
	motto := mottoStyle.Render("Serva Momentum")

	// Line 3: Divider
	divider := dividerStyle.Render("────────────────────────────────────────────────────────────")

	// Line 4: Status bar
	recordingStatus := "Off"
	recordingColor := ColorGray
	if GlobalAppState.IsRecording {
		if GlobalAppState.BlinkOn {
			recordingStatus = "● On"
		} else {
			recordingStatus = "○ On"
		}
		recordingColor = ColorRed
	}

	recordingStyled := lipgloss.NewStyle().
		Foreground(recordingColor).
		Bold(GlobalAppState.IsRecording).
		Render(recordingStatus)

	// YouTube connection status
	youtubeStatus := "YT: -"
	youtubeColor := ColorGray
	if GlobalAppState.YouTubeConnected {
		youtubeStatus = "YT: ✓"
		youtubeColor = ColorGreen
	}
	youtubeStyled := lipgloss.NewStyle().
		Foreground(youtubeColor).
		Render(youtubeStatus)

	statusLine := fmt.Sprintf("Rec: %s | %s | #%d | %s",
		recordingStyled,
		youtubeStyled,
		GlobalAppState.TotalRecordings,
		GlobalAppState.Status,
	)
	status := statusStyle.Render(statusLine)

	return lipgloss.JoinVertical(
		lipgloss.Center,
		title,
		motto,
		divider,
		status,
		divider,
	)
}

// ========================================
// Footer Rendering
// ========================================

// RenderHelpFooter renders the standard help footer at the bottom of the screen
func RenderHelpFooter(helpText string, width int) string {
	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	footerStyle := lipgloss.NewStyle().
		Width(width).
		Align(lipgloss.Center)

	return footerStyle.Render(helpStyle.Render(helpText))
}

// ========================================
// Layout Helpers
// ========================================

// LayoutWithHeaderFooter creates a standard layout with header at top and footer at bottom
func LayoutWithHeaderFooter(header, content, footer string, width, height int) string {
	// Main section with header and content
	mainSection := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		content,
	)

	// Center main content at top (leave room for footer)
	centeredMain := lipgloss.Place(
		width,
		height-2,
		lipgloss.Center,
		lipgloss.Top,
		mainSection,
	)

	return lipgloss.JoinVertical(
		lipgloss.Left,
		centeredMain,
		footer,
	)
}

// CenterContent centers content both horizontally and vertically
func CenterContent(content string, width, height int) string {
	return lipgloss.Place(
		width,
		height,
		lipgloss.Center,
		lipgloss.Center,
		content,
	)
}

// ========================================
// Common Styles
// ========================================

// Box style for content areas
var BoxStyle = lipgloss.NewStyle().
	Border(lipgloss.RoundedBorder()).
	BorderForeground(ColorOrange).
	Padding(1, 2)

// Title style for section headings
var TitleStyle = lipgloss.NewStyle().
	Bold(true).
	Foreground(ColorOrange)

// Subtitle style
var SubtitleStyle = lipgloss.NewStyle().
	Foreground(ColorBlue)

// Label style for form labels
var LabelStyle = lipgloss.NewStyle().
	Foreground(ColorGray)

// Value style for displaying values
var ValueStyle = lipgloss.NewStyle().
	Foreground(ColorWhite)

// Active style for active/selected items
var ActiveStyle = lipgloss.NewStyle().
	Foreground(ColorOrange).
	Bold(true)

// Inactive style for inactive items
var InactiveStyle = lipgloss.NewStyle().
	Foreground(ColorGray)

// Error style for error messages
var ErrorStyle = lipgloss.NewStyle().
	Foreground(ColorRed).
	Bold(true)

// Success style for success messages
var SuccessStyle = lipgloss.NewStyle().
	Foreground(ColorGreen).
	Bold(true)

// Recording style for recording indicator (blinking red)
var RecordingStyle = lipgloss.NewStyle().
	Foreground(ColorRed).
	Bold(true).
	Blink(true)
