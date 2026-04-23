package pages

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	qt "github.com/mappu/miqt/qt6"
	"github.com/mappu/miqt/qt6/multimedia"
	"github.com/kartoza/kartoza-screencaster/internal/config"
	"github.com/kartoza/kartoza-screencaster/internal/models"
)

// recordingEntry holds a loaded recording with its display info
type recordingEntry struct {
	info      *models.RecordingInfo
	folder    string
	dirName   string
	thumbnail *qt.QPixmap
}

// HistoryPage is the recording browser page
type HistoryPage struct {
	widget *qt.QWidget

	// UI elements
	recordingList *qt.QListWidget
	searchInput   *qt.QLineEdit
	titleLabel    *qt.QLabel
	durationLabel *qt.QLabel
	statusLabel   *qt.QLabel
	filesLabel    *qt.QLabel
	sizeLabel     *qt.QLabel
	playBtn       *qt.QPushButton
	stopBtn       *qt.QPushButton
	reprocessBtn  *qt.QPushButton
	deleteBtn     *qt.QPushButton
	seekSlider    *qt.QSlider
	timeLabel     *qt.QLabel

	// Video player
	videoWidget *multimedia.QVideoWidget
	player      *multimedia.QMediaPlayer
	audioOutput *multimedia.QAudioOutput
	playing     bool

	// Data
	recordings []recordingEntry
}

// NewHistoryPage creates a new history page
func NewHistoryPage() *HistoryPage {
	p := &HistoryPage{
		widget: qt.NewQWidget2(),
	}
	p.setupUI()
	p.loadRecordings()
	return p
}

func (p *HistoryPage) setupUI() {
	layout := qt.NewQHBoxLayout(p.widget)
	layout.SetSpacing(15)
	layout.SetContentsMargins(15, 15, 15, 15)

	// === Left panel: recording list ===
	leftPanel := qt.NewQWidget2()
	leftLayout := qt.NewQVBoxLayout(leftPanel)
	leftLayout.SetSpacing(8)

	titleLabel := qt.NewQLabel3("Recording History")
	titleLabel.SetStyleSheet("QLabel { color: #cdd6f4; font-size: 18px; font-weight: bold; }")
	leftLayout.AddWidget(titleLabel.QFrame.QWidget)

	// Search bar
	p.searchInput = qt.NewQLineEdit2()
	p.searchInput.SetPlaceholderText("Search recordings...")
	p.searchInput.SetStyleSheet("QLineEdit { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 6px; font-size: 13px; }")
	p.searchInput.OnTextChanged(func(text string) {
		p.filterRecordings(text)
	})
	leftLayout.AddWidget(p.searchInput.QWidget)

	// Recording list
	p.recordingList = qt.NewQListWidget2()
	p.recordingList.SetStyleSheet(`
		QListWidget {
			background: #1e1e2e;
			color: #cdd6f4;
			border: 1px solid #313244;
			border-radius: 4px;
			font-size: 13px;
		}
		QListWidget::item {
			padding: 8px 10px;
			border-bottom: 1px solid #313244;
		}
		QListWidget::item:selected {
			background: #45475a;
		}
		QListWidget::item:hover {
			background: #313244;
		}
	`)
	p.recordingList.OnCurrentRowChanged(func(row int) {
		p.onRecordingSelected(row)
	})
	leftLayout.AddWidget(p.recordingList.QListView.QAbstractItemView.QAbstractScrollArea.QFrame.QWidget)

	// Refresh button
	refreshBtn := qt.NewQPushButton3("Refresh")
	refreshBtn.SetStyleSheet("QPushButton { background: #45475a; color: #cdd6f4; border: none; border-radius: 4px; padding: 6px; } QPushButton:hover { background: #585b70; }")
	refreshBtn.OnClicked(func() {
		p.loadRecordings()
	})
	leftLayout.AddWidget(refreshBtn.QAbstractButton.QWidget)

	layout.AddWidget(leftPanel)

	// === Right panel: preview + player ===
	rightPanel := qt.NewQWidget2()
	rightPanel.SetFixedWidth(420)
	rightLayout := qt.NewQVBoxLayout(rightPanel)
	rightLayout.SetSpacing(8)

	detailsTitle := qt.NewQLabel3("Details")
	detailsTitle.SetStyleSheet("QLabel { color: #cdd6f4; font-size: 16px; font-weight: bold; }")
	rightLayout.AddWidget(detailsTitle.QFrame.QWidget)

	// Inline video player
	p.videoWidget = multimedia.NewQVideoWidget2()
	p.videoWidget.SetMinimumSize2(400, 225) // 16:9
	p.videoWidget.SetStyleSheet("background: #000000; border-radius: 8px;")
	rightLayout.AddWidget(p.videoWidget.QWidget)

	// Media player setup
	p.player = multimedia.NewQMediaPlayer()
	p.audioOutput = multimedia.NewQAudioOutput()
	p.player.SetAudioOutput(p.audioOutput)
	p.player.SetVideoOutput(p.videoWidget.QWidget.QObject)

	// Playback controls row
	controlsRow := qt.NewQHBoxLayout2()
	controlsRow.SetSpacing(5)

	p.playBtn = qt.NewQPushButton3("Play")
	p.playBtn.SetStyleSheet("QPushButton { background: #a6e3a1; color: #1e1e2e; border: none; border-radius: 4px; padding: 6px 16px; font-weight: bold; } QPushButton:hover { background: #94e2d5; } QPushButton:disabled { background: #45475a; color: #6c7086; }")
	p.playBtn.SetEnabled(false)
	p.playBtn.OnClicked(func() {
		if p.playing {
			p.player.Pause()
			p.playing = false
			p.playBtn.SetText("Play")
		} else {
			p.onPlayClicked()
		}
	})
	controlsRow.AddWidget(p.playBtn.QAbstractButton.QWidget)

	p.stopBtn = qt.NewQPushButton3("Stop")
	p.stopBtn.SetStyleSheet("QPushButton { background: #f38ba8; color: #1e1e2e; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold; } QPushButton:hover { background: #eba0ac; }")
	p.stopBtn.SetEnabled(false)
	p.stopBtn.OnClicked(func() { p.onStopPlayback() })
	controlsRow.AddWidget(p.stopBtn.QAbstractButton.QWidget)

	p.timeLabel = qt.NewQLabel3("00:00 / 00:00")
	p.timeLabel.SetStyleSheet("QLabel { color: #6c7086; font-size: 11px; }")
	controlsRow.AddWidget(p.timeLabel.QFrame.QWidget)

	rightLayout.AddLayout(controlsRow.QLayout)

	// Seek slider
	p.seekSlider = qt.NewQSlider2()
	p.seekSlider.SetOrientation(qt.Horizontal)
	p.seekSlider.SetStyleSheet(`
		QSlider::groove:horizontal { background: #313244; height: 6px; border-radius: 3px; }
		QSlider::handle:horizontal { background: #89b4fa; width: 12px; height: 12px; margin: -3px 0; border-radius: 6px; }
		QSlider::sub-page:horizontal { background: #89b4fa; border-radius: 3px; }
	`)
	p.seekSlider.OnSliderMoved(func(position int) {
		p.player.SetPosition(int64(position))
	})
	rightLayout.AddWidget(p.seekSlider.QAbstractSlider.QWidget)

	// Connect player signals for position/duration updates
	p.player.OnPositionChanged(func(position int64) {
		if !p.seekSlider.IsSliderDown() {
			p.seekSlider.SetValue(int(position))
		}
		dur := p.player.Duration()
		p.timeLabel.SetText(fmt.Sprintf("%s / %s", formatMs(position), formatMs(dur)))
	})
	p.player.OnDurationChanged(func(duration int64) {
		p.seekSlider.SetMaximum(int(duration))
	})

	// Metadata labels
	dimStyle := "QLabel { color: #6c7086; font-size: 12px; }"

	p.titleLabel = qt.NewQLabel3("Select a recording")
	p.titleLabel.SetStyleSheet("QLabel { color: #cdd6f4; font-size: 14px; font-weight: bold; }")
	p.titleLabel.SetWordWrap(true)
	rightLayout.AddWidget(p.titleLabel.QFrame.QWidget)

	p.statusLabel = qt.NewQLabel3("")
	p.statusLabel.SetStyleSheet(dimStyle)
	rightLayout.AddWidget(p.statusLabel.QFrame.QWidget)

	p.durationLabel = qt.NewQLabel3("")
	p.durationLabel.SetStyleSheet(dimStyle)
	rightLayout.AddWidget(p.durationLabel.QFrame.QWidget)

	p.filesLabel = qt.NewQLabel3("")
	p.filesLabel.SetStyleSheet(dimStyle)
	p.filesLabel.SetWordWrap(true)
	rightLayout.AddWidget(p.filesLabel.QFrame.QWidget)

	p.sizeLabel = qt.NewQLabel3("")
	p.sizeLabel.SetStyleSheet(dimStyle)
	rightLayout.AddWidget(p.sizeLabel.QFrame.QWidget)

	// Action buttons
	actionRow := qt.NewQHBoxLayout2()
	actionRow.SetSpacing(5)

	p.reprocessBtn = qt.NewQPushButton3("Reprocess")
	p.reprocessBtn.SetStyleSheet("QPushButton { background: #45475a; color: #cdd6f4; border: none; border-radius: 4px; padding: 6px 12px; } QPushButton:hover { background: #585b70; } QPushButton:disabled { background: #313244; color: #6c7086; }")
	p.reprocessBtn.SetEnabled(false)
	actionRow.AddWidget(p.reprocessBtn.QAbstractButton.QWidget)

	p.deleteBtn = qt.NewQPushButton3("Delete")
	p.deleteBtn.SetStyleSheet("QPushButton { background: #f38ba8; color: #1e1e2e; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold; } QPushButton:hover { background: #eba0ac; } QPushButton:disabled { background: #313244; color: #6c7086; }")
	p.deleteBtn.SetEnabled(false)
	p.deleteBtn.OnClicked(func() { p.onDeleteClicked() })
	actionRow.AddWidget(p.deleteBtn.QAbstractButton.QWidget)

	rightLayout.AddLayout(actionRow.QLayout)

	rightLayout.AddStretch()
	layout.AddWidget(rightPanel)
}

func (p *HistoryPage) loadRecordings() {
	p.recordings = nil
	p.recordingList.Clear()

	cfg, _ := config.Load()
	videosDir := cfg.OutputDir
	if videosDir == "" {
		videosDir = config.GetDefaultVideosDir()
	}

	entries, err := os.ReadDir(videosDir)
	if err != nil {
		return
	}

	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}
		folderPath := filepath.Join(videosDir, entry.Name())
		info, err := models.LoadRecordingInfo(folderPath)
		if err != nil {
			continue
		}

		rec := recordingEntry{
			info:    info,
			folder:  folderPath,
			dirName: entry.Name(),
		}

		p.recordings = append(p.recordings, rec)
	}

	// Sort by start time descending (newest first)
	sort.Slice(p.recordings, func(i, j int) bool {
		return p.recordings[i].info.StartTime.After(p.recordings[j].info.StartTime)
	})

	// Populate list
	for _, rec := range p.recordings {
		title := rec.info.Metadata.Title
		if title == "" {
			title = rec.dirName
		}

		date := rec.info.StartTime.Format("2006-01-02 15:04")
		duration := formatDuration(rec.info.Duration)
		status := rec.info.Status

		display := fmt.Sprintf("%s\n%s | %s | %s", title, date, duration, status)
		p.recordingList.AddItem(display)
	}

	// Reset preview
	p.clearPreview()
}

func (p *HistoryPage) filterRecordings(query string) {
	query = strings.ToLower(query)
	for i := 0; i < int(p.recordingList.Count()); i++ {
		item := p.recordingList.Item(i)
		text := strings.ToLower(item.Text())
		item.SetHidden(!strings.Contains(text, query))
	}
}

func (p *HistoryPage) onRecordingSelected(row int) {
	if row < 0 || row >= len(p.recordings) {
		p.clearPreview()
		return
	}

	rec := p.recordings[row]
	info := rec.info

	// Title
	title := info.Metadata.Title
	if title == "" {
		title = rec.dirName
	}
	if info.Metadata.Number > 0 {
		title = fmt.Sprintf("#%03d - %s", info.Metadata.Number, title)
	}
	p.titleLabel.SetText(title)

	// Status with color
	switch info.Status {
	case models.StatusCompleted:
		p.statusLabel.SetText("Status: Completed")
		p.statusLabel.SetStyleSheet("QLabel { color: #a6e3a1; font-size: 12px; }")
	case models.StatusFailed:
		p.statusLabel.SetText("Status: Failed")
		p.statusLabel.SetStyleSheet("QLabel { color: #f38ba8; font-size: 12px; }")
	case models.StatusProcessing:
		p.statusLabel.SetText("Status: Processing")
		p.statusLabel.SetStyleSheet("QLabel { color: #fab387; font-size: 12px; }")
	default:
		p.statusLabel.SetText("Status: " + info.Status)
		p.statusLabel.SetStyleSheet("QLabel { color: #6c7086; font-size: 12px; }")
	}

	// Duration
	p.durationLabel.SetText("Duration: " + formatDuration(info.Duration))

	// Files
	var files []string
	if info.Files.MergedFile != "" {
		files = append(files, "Merged")
	}
	if info.Files.VerticalFile != "" {
		files = append(files, "Vertical")
	}
	if info.Files.VideoFile != "" {
		files = append(files, "Screen")
	}
	if info.Files.WebcamFile != "" {
		files = append(files, "Webcam")
	}
	if info.Files.AudioFile != "" {
		files = append(files, "Audio")
	}
	p.filesLabel.SetText("Files: " + strings.Join(files, ", "))

	// Size
	p.sizeLabel.SetText(fmt.Sprintf("Size: %s", formatBytes(info.Files.TotalSize)))

	// Stop any current playback
	if p.playing {
		p.onStopPlayback()
	}

	// Enable buttons
	hasVideo := (info.Files.MergedFile != "" && fileExists(info.Files.MergedFile)) ||
		(info.Files.VideoFile != "" && fileExists(info.Files.VideoFile))
	p.playBtn.SetEnabled(hasVideo)
	p.playBtn.SetText("Play")
	p.reprocessBtn.SetEnabled(true)
	p.deleteBtn.SetEnabled(true)
}

func (p *HistoryPage) clearPreview() {
	if p.playing {
		p.onStopPlayback()
	}
	p.titleLabel.SetText("Select a recording")
	p.statusLabel.SetText("")
	p.durationLabel.SetText("")
	p.filesLabel.SetText("")
	p.sizeLabel.SetText("")
	p.playBtn.SetEnabled(false)
	p.playBtn.SetText("Play")
	p.stopBtn.SetEnabled(false)
	p.reprocessBtn.SetEnabled(false)
	p.deleteBtn.SetEnabled(false)
}

func (p *HistoryPage) onPlayClicked() {
	row := int(p.recordingList.CurrentRow())
	if row < 0 || row >= len(p.recordings) {
		return
	}

	rec := p.recordings[row]
	videoFile := rec.info.Files.MergedFile
	if videoFile == "" {
		// Fall back to raw screen recording
		videoFile = rec.info.Files.VideoFile
	}
	if videoFile == "" || !fileExists(videoFile) {
		return
	}

	// Play inline
	url := qt.QUrl_FromLocalFile(videoFile)
	p.player.SetSource(url)
	p.player.Play()
	p.playing = true
	p.playBtn.SetText("Pause")
	p.stopBtn.SetEnabled(true)
}

func (p *HistoryPage) onStopPlayback() {
	p.player.Stop()
	p.playing = false
	p.playBtn.SetText("Play")
	p.stopBtn.SetEnabled(false)
	p.seekSlider.SetValue(0)
	p.timeLabel.SetText("00:00 / 00:00")
}

func (p *HistoryPage) onDeleteClicked() {
	row := int(p.recordingList.CurrentRow())
	if row < 0 || row >= len(p.recordings) {
		return
	}

	rec := p.recordings[row]

	// Confirmation dialog
	result := qt.QMessageBox_Question(
		p.widget,
		"Delete Recording",
		fmt.Sprintf("Delete recording \"%s\"?\n\nThis will permanently remove all files in:\n%s", rec.dirName, rec.folder),
	)

	if result == qt.QMessageBox__Yes {
		_ = os.RemoveAll(rec.folder)
		p.loadRecordings()
	}
}

// Refresh reloads the recordings list
func (p *HistoryPage) Refresh() {
	p.loadRecordings()
}

// Widget returns the underlying QWidget
func (p *HistoryPage) Widget() *qt.QWidget {
	return p.widget
}

// Helper functions

func formatDuration(d time.Duration) string {
	if d == 0 {
		return "0:00"
	}
	h := int(d.Hours())
	m := int(d.Minutes()) % 60
	s := int(d.Seconds()) % 60
	if h > 0 {
		return fmt.Sprintf("%d:%02d:%02d", h, m, s)
	}
	return fmt.Sprintf("%d:%02d", m, s)
}

func formatMs(ms int64) string {
	s := ms / 1000
	m := s / 60
	s = s % 60
	h := m / 60
	m = m % 60
	if h > 0 {
		return fmt.Sprintf("%d:%02d:%02d", h, m, s)
	}
	return fmt.Sprintf("%d:%02d", m, s)
}

func formatBytes(b int64) string {
	if b == 0 {
		return "0 B"
	}
	const unit = 1024
	if b < unit {
		return fmt.Sprintf("%d B", b)
	}
	div, exp := int64(unit), 0
	for n := b / unit; n >= unit; n /= unit {
		div *= unit
		exp++
	}
	return fmt.Sprintf("%.1f %cB", float64(b)/float64(div), "KMGTPE"[exp])
}

func fileExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}
