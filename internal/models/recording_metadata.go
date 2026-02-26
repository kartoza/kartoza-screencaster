package models

import (
	"fmt"
	"regexp"
	"strings"
)

// RecordingMetadata holds user-provided metadata for a recording
type RecordingMetadata struct {
	Number      int    `json:"number"`
	Title       string `json:"title"`
	Description string `json:"description"`
	Topic       string `json:"topic"`
	Presenter   string `json:"presenter"`
	FolderName  string `json:"folder_name,omitempty"`

	// YouTube upload information
	YouTube *YouTubeMetadata `json:"youtube,omitempty"`

	// Syndication information (posts to other platforms)
	Syndication *SyndicationMetadata `json:"syndication,omitempty"`
}

// YouTubeMetadata holds information about a video uploaded to YouTube
type YouTubeMetadata struct {
	VideoID      string `json:"video_id"`
	VideoURL     string `json:"video_url"`
	PlaylistID   string `json:"playlist_id,omitempty"`
	PlaylistName string `json:"playlist_name,omitempty"`
	Privacy      string `json:"privacy"` // public, unlisted, private
	UploadedAt   string `json:"uploaded_at"`
	ThumbnailURL string `json:"thumbnail_url,omitempty"`
	ChannelID    string `json:"channel_id,omitempty"`
	ChannelName  string `json:"channel_name,omitempty"`
}

// IsPublishedToYouTube returns true if the recording has been uploaded to YouTube
func (m *RecordingMetadata) IsPublishedToYouTube() bool {
	return m.YouTube != nil && m.YouTube.VideoID != ""
}

// SyndicationPost represents a single syndication post to a platform
type SyndicationPost struct {
	AccountID   string `json:"account_id"`
	Platform    string `json:"platform"`
	AccountName string `json:"account_name"`
	PostID      string `json:"post_id,omitempty"`
	PostURL     string `json:"post_url,omitempty"`
	PostedAt    string `json:"posted_at"`
	Success     bool   `json:"success"`
	Error       string `json:"error,omitempty"`
}

// SyndicationMetadata holds information about syndication posts
type SyndicationMetadata struct {
	Posts []SyndicationPost `json:"posts,omitempty"`
}

// HasBeenSyndicated returns true if the recording has been syndicated to any platform
func (m *RecordingMetadata) HasBeenSyndicated() bool {
	return m.Syndication != nil && len(m.Syndication.Posts) > 0
}

// GetSuccessfulSyndications returns all successful syndication posts
func (m *RecordingMetadata) GetSuccessfulSyndications() []SyndicationPost {
	if m.Syndication == nil {
		return nil
	}
	var successful []SyndicationPost
	for _, post := range m.Syndication.Posts {
		if post.Success {
			successful = append(successful, post)
		}
	}
	return successful
}

// HasSyndicatedTo returns true if the recording has been syndicated to the given platform
func (m *RecordingMetadata) HasSyndicatedTo(platform string) bool {
	if m.Syndication == nil {
		return false
	}
	for _, post := range m.Syndication.Posts {
		if post.Platform == platform && post.Success {
			return true
		}
	}
	return false
}

// AddSyndicationPost adds a syndication post record
func (m *RecordingMetadata) AddSyndicationPost(post SyndicationPost) {
	if m.Syndication == nil {
		m.Syndication = &SyndicationMetadata{}
	}
	m.Syndication.Posts = append(m.Syndication.Posts, post)
}

// GenerateFolderName creates a folder name from the counter and title
// Format: NNN-sanitized-title
func (m *RecordingMetadata) GenerateFolderName() string {
	// Sanitize title for filesystem use
	sanitized := sanitizeForFilename(m.Title)
	if sanitized == "" {
		sanitized = "recording"
	}

	m.FolderName = fmt.Sprintf("%03d-%s", m.Number, sanitized)
	return m.FolderName
}

// sanitizeForFilename removes or replaces characters that are invalid in filenames
func sanitizeForFilename(s string) string {
	// Convert to lowercase
	s = strings.ToLower(s)

	// Replace spaces and hyphens with underscores for consistency
	s = strings.ReplaceAll(s, " ", "_")
	s = strings.ReplaceAll(s, "-", "_")

	// Remove or replace invalid characters
	// Keep only alphanumeric and underscores
	reg := regexp.MustCompile(`[^a-z0-9_]`)
	s = reg.ReplaceAllString(s, "")

	// Remove multiple consecutive underscores
	reg = regexp.MustCompile(`_+`)
	s = reg.ReplaceAllString(s, "_")

	// Trim underscores from ends
	s = strings.Trim(s, "_")

	// Limit length
	if len(s) > 50 {
		s = s[:50]
	}

	return s
}

// Topic represents a recording topic/category
type Topic struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}

// DefaultTopics returns a list of default topics
func DefaultTopics() []Topic {
	return []Topic{
		{ID: "tutorial", Name: "Tutorial"},
		{ID: "demo", Name: "Demo"},
		{ID: "presentation", Name: "Presentation"},
		{ID: "meeting", Name: "Meeting"},
		{ID: "training", Name: "Training"},
		{ID: "review", Name: "Code Review"},
		{ID: "other", Name: "Other"},
	}
}
