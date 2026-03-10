package tui

import (
	"context"
	"fmt"
	"strings"
	"time"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
	"github.com/kartoza/kartoza-screencaster/internal/config"
	"github.com/kartoza/kartoza-screencaster/internal/youtube"
)

// YouTubeSetupStep represents the current step in the setup wizard
type YouTubeSetupStep int

const (
	YouTubeStepWelcome YouTubeSetupStep = iota
	YouTubeStepInstructions
	YouTubeStepCredentials
	YouTubeStepAuthenticating
	YouTubeStepSelectChannel // Select brand channel after auth
	YouTubeStepConnected
	YouTubeStepVerifying
	YouTubeStepVerified
	YouTubeStepPlaylists
	YouTubeStepCreatePlaylist
	YouTubeStepAccounts      // Account list view
	YouTubeStepAccountAdd    // Add new account
	YouTubeStepAccountEdit   // Edit existing account
	YouTubeStepAccountDelete // Delete confirmation
	YouTubeStepError
)

// YouTubeSetupModel handles YouTube account setup
type YouTubeSetupModel struct {
	width  int
	height int

	step         YouTubeSetupStep
	clientID     textinput.Model
	clientSecret textinput.Model
	focusedInput int // 0 = client ID, 1 = client secret

	// Status
	authStatus       youtube.AuthStatus
	channelName      string
	errorMessage     string
	isAuthenticating bool
	authURL          string // URL for manual browser opening

	// Verification data
	isVerifying bool
	verifyError string
	channelID   string
	playlists   []youtube.Playlist
	playlistPage int // For scrolling through playlists

	// Playlist management
	isLoadingPlaylists  bool
	playlistsError      string
	newPlaylistTitle    textinput.Model
	newPlaylistDesc     textinput.Model
	newPlaylistPrivacy  youtube.PrivacyStatus
	createPlaylistFocus int // 0=title, 1=desc, 2=privacy
	isCreatingPlaylist  bool

	// Account management
	accounts             []youtube.Account
	selectedAccountIndex int
	accountName          textinput.Model
	accountClientID      textinput.Model
	accountClientSecret  textinput.Model
	accountFormFocus     int  // 0=name, 1=clientID, 2=clientSecret
	editingAccountID     string
	isAuthenticatingAccount bool
	accountAuthURL       string

	// Channel selection (brand accounts)
	availableChannels       []youtube.Channel
	selectedChannelIndex    int
	isLoadingChannels       bool
	channelSelectionForAccount string // Account ID for which we're selecting a channel

	// Config
	cfg *config.Config
}

// NewYouTubeSetupModel creates a new YouTube setup model
func NewYouTubeSetupModel() *YouTubeSetupModel {
	clientIDInput := textinput.New()
	clientIDInput.Placeholder = "xxxxx.apps.googleusercontent.com"
	clientIDInput.CharLimit = 200
	clientIDInput.Width = 50

	clientSecretInput := textinput.New()
	clientSecretInput.Placeholder = "GOCSPX-xxxxx"
	clientSecretInput.CharLimit = 100
	clientSecretInput.Width = 50
	clientSecretInput.EchoMode = textinput.EchoPassword

	// Playlist creation inputs
	playlistTitleInput := textinput.New()
	playlistTitleInput.Placeholder = "My Playlist"
	playlistTitleInput.CharLimit = 150
	playlistTitleInput.Width = 50

	playlistDescInput := textinput.New()
	playlistDescInput.Placeholder = "Description (optional)"
	playlistDescInput.CharLimit = 500
	playlistDescInput.Width = 50

	// Account name input
	accountNameInput := textinput.New()
	accountNameInput.Placeholder = "My YouTube Channel"
	accountNameInput.CharLimit = 100
	accountNameInput.Width = 50

	// Account client ID input
	accountClientIDInput := textinput.New()
	accountClientIDInput.Placeholder = "xxxxx.apps.googleusercontent.com"
	accountClientIDInput.CharLimit = 200
	accountClientIDInput.Width = 50

	// Account client secret input
	accountClientSecretInput := textinput.New()
	accountClientSecretInput.Placeholder = "GOCSPX-xxxxx"
	accountClientSecretInput.CharLimit = 100
	accountClientSecretInput.Width = 50
	accountClientSecretInput.EchoMode = textinput.EchoPassword

	// Load existing config
	cfg, _ := config.Load()

	// Pre-fill if credentials exist
	if cfg.YouTube.ClientID != "" {
		clientIDInput.SetValue(cfg.YouTube.ClientID)
	}
	if cfg.YouTube.ClientSecret != "" {
		clientSecretInput.SetValue(cfg.YouTube.ClientSecret)
	}

	m := &YouTubeSetupModel{
		clientID:            clientIDInput,
		clientSecret:        clientSecretInput,
		newPlaylistTitle:    playlistTitleInput,
		newPlaylistDesc:     playlistDescInput,
		newPlaylistPrivacy:  youtube.PrivacyPrivate,
		accountName:         accountNameInput,
		accountClientID:     accountClientIDInput,
		accountClientSecret: accountClientSecretInput,
		accounts:            cfg.YouTube.GetAccounts(),
		cfg:                 cfg,
		authStatus:          cfg.GetYouTubeAuthStatus(),
	}

	// Start at appropriate step based on current status
	switch m.authStatus {
	case youtube.AuthStatusAuthenticated:
		m.step = YouTubeStepConnected
		m.channelName = cfg.YouTube.ChannelName
	case youtube.AuthStatusConfigured:
		m.step = YouTubeStepCredentials
	default:
		m.step = YouTubeStepWelcome
	}

	return m
}

// Init initializes the YouTube setup model
func (m *YouTubeSetupModel) Init() tea.Cmd {
	return textinput.Blink
}

// Update handles messages
func (m *YouTubeSetupModel) Update(msg tea.Msg) (*YouTubeSetupModel, tea.Cmd) {
	var cmd tea.Cmd

	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height

	case tea.KeyMsg:
		return m.handleKeyMsg(msg)

	case youtubeAuthStartedMsg:
		m.step = YouTubeStepAuthenticating
		m.isAuthenticating = true
		m.authURL = msg.authURL
		// Start waiting for the auth result
		return m, m.waitForAuthResult()

	case youtubeAuthCompleteMsg:
		m.isAuthenticating = false
		if msg.err != nil {
			m.step = YouTubeStepError
			m.errorMessage = msg.err.Error()
			return m, nil
		}
		// Auth succeeded - load available channels to check for brand accounts
		m.isLoadingChannels = true
		m.channelSelectionForAccount = ""
		return m, m.loadAvailableChannels()

	case youtubeChannelsLoadedMsg:
		m.isLoadingChannels = false
		if msg.err != nil {
			// Failed to load channels, just proceed
			if m.channelSelectionForAccount != "" {
				m.step = YouTubeStepAccounts
			} else {
				m.step = YouTubeStepConnected
			}
			return m, nil
		}
		m.availableChannels = msg.channels
		if len(msg.channels) > 1 {
			// Multiple channels available - let user select
			m.step = YouTubeStepSelectChannel
			m.selectedChannelIndex = 0
		} else if len(msg.channels) == 1 {
			// Only one channel - use it automatically
			if m.channelSelectionForAccount != "" {
				// Account-specific flow
				if m.selectedAccountIndex < len(m.accounts) {
					acc := m.accounts[m.selectedAccountIndex]
					acc.ChannelName = msg.channels[0].Title
					acc.ChannelID = msg.channels[0].ID
					m.cfg.YouTube.UpdateAccount(acc)
					_ = config.Save(m.cfg)
					m.accounts = m.cfg.YouTube.GetAccounts()
				}
				m.channelSelectionForAccount = ""
				m.step = YouTubeStepAccounts
			} else {
				// Legacy flow
				m.channelName = msg.channels[0].Title
				m.cfg.YouTube.ChannelName = msg.channels[0].Title
				m.cfg.YouTube.ChannelID = msg.channels[0].ID
				_ = config.Save(m.cfg)
				m.step = YouTubeStepConnected
			}
		} else {
			// No channels found
			if m.channelSelectionForAccount != "" {
				m.channelSelectionForAccount = ""
				m.step = YouTubeStepAccounts
			} else {
				m.step = YouTubeStepConnected
			}
		}
		return m, nil

	case youtubeChannelSelectedMsg:
		// User selected a channel from the list
		if m.channelSelectionForAccount != "" {
			// Account-specific flow
			if m.selectedAccountIndex < len(m.accounts) {
				acc := m.accounts[m.selectedAccountIndex]
				acc.ChannelName = msg.channel.Title
				acc.ChannelID = msg.channel.ID
				m.cfg.YouTube.UpdateAccount(acc)
				_ = config.Save(m.cfg)
				m.accounts = m.cfg.YouTube.GetAccounts()
			}
			m.channelSelectionForAccount = ""
			m.step = YouTubeStepAccounts
		} else {
			// Legacy flow
			m.channelName = msg.channel.Title
			m.cfg.YouTube.ChannelName = msg.channel.Title
			m.cfg.YouTube.ChannelID = msg.channel.ID
			_ = config.Save(m.cfg)
			m.step = YouTubeStepConnected
		}
		return m, nil

	case youtubeDisconnectMsg:
		m.authStatus = youtube.AuthStatusConfigured
		m.channelName = ""
		m.cfg.YouTube.ChannelName = ""
		_ = config.Save(m.cfg)
		m.step = YouTubeStepCredentials
		return m, nil

	case youtubeVerifyCompleteMsg:
		m.isVerifying = false
		if msg.err != nil {
			m.verifyError = msg.err.Error()
			m.step = YouTubeStepVerified
		} else {
			m.channelName = msg.channelName
			m.channelID = msg.channelID
			m.playlists = msg.playlists
			m.verifyError = ""
			m.step = YouTubeStepVerified
			// Save channel info to config
			m.cfg.YouTube.ChannelName = msg.channelName
			m.cfg.YouTube.ChannelID = msg.channelID
			_ = config.Save(m.cfg)
		}
		return m, nil

	case youtubePlaylistsLoadedMsg:
		m.isLoadingPlaylists = false
		if msg.err != nil {
			m.playlistsError = msg.err.Error()
		} else {
			m.playlists = msg.playlists
			m.playlistsError = ""
		}
		return m, nil

	case youtubePlaylistCreatedMsg:
		m.isCreatingPlaylist = false
		if msg.err != nil {
			m.playlistsError = msg.err.Error()
		} else {
			// Add the new playlist to our list
			if msg.playlist != nil {
				m.playlists = append([]youtube.Playlist{*msg.playlist}, m.playlists...)
			}
			m.playlistsError = ""
			// Clear the form and go back to playlists list
			m.newPlaylistTitle.SetValue("")
			m.newPlaylistDesc.SetValue("")
			m.newPlaylistPrivacy = youtube.PrivacyPrivate
			m.step = YouTubeStepPlaylists
		}
		return m, nil

	case youtubeAccountAuthStartedMsg:
		m.isAuthenticatingAccount = true
		m.accountAuthURL = msg.authURL
		return m, m.waitForAccountAuthResult()

	case youtubeAccountAuthCompleteMsg:
		m.isAuthenticatingAccount = false
		m.accountAuthURL = ""
		if msg.err != nil {
			m.errorMessage = msg.err.Error()
			m.step = YouTubeStepAccounts
			return m, nil
		}
		// Auth succeeded for account - load available channels
		if m.selectedAccountIndex < len(m.accounts) {
			m.channelSelectionForAccount = m.accounts[m.selectedAccountIndex].ID
			m.isLoadingChannels = true
			return m, m.loadAvailableChannelsForAccount(m.accounts[m.selectedAccountIndex])
		}
		m.step = YouTubeStepAccounts
		return m, nil
	}

	// Update text inputs
	switch m.step {
	case YouTubeStepCredentials:
		switch m.focusedInput {
		case 0:
			m.clientID, cmd = m.clientID.Update(msg)
		default:
			m.clientSecret, cmd = m.clientSecret.Update(msg)
		}
	case YouTubeStepCreatePlaylist:
		switch m.createPlaylistFocus {
		case 0:
			m.newPlaylistTitle, cmd = m.newPlaylistTitle.Update(msg)
		case 1:
			m.newPlaylistDesc, cmd = m.newPlaylistDesc.Update(msg)
		}
	}

	return m, cmd
}

// handleKeyMsg handles keyboard input
func (m *YouTubeSetupModel) handleKeyMsg(msg tea.KeyMsg) (*YouTubeSetupModel, tea.Cmd) {
	// Handle global keys first
	switch msg.String() {
	case "ctrl+c":
		return m, tea.Quit

	case "esc":
		if m.step == YouTubeStepAuthenticating {
			// Can't cancel during authentication
			return m, nil
		}
		return m, func() tea.Msg { return backToMenuMsg{} }
	}

	// Handle step-specific keys
	switch m.step {
	case YouTubeStepCredentials:
		switch msg.String() {
		case "tab", "shift+tab":
			m.focusedInput = (m.focusedInput + 1) % 2
			if m.focusedInput == 0 {
				m.clientID.Focus()
				m.clientSecret.Blur()
			} else {
				m.clientID.Blur()
				m.clientSecret.Focus()
			}
			return m, textinput.Blink

		case "enter":
			return m.handleEnter()

		default:
			// Forward all other keys to the focused text input
			var cmd tea.Cmd
			if m.focusedInput == 0 {
				m.clientID, cmd = m.clientID.Update(msg)
			} else {
				m.clientSecret, cmd = m.clientSecret.Update(msg)
			}
			return m, cmd
		}

	case YouTubeStepWelcome:
		switch msg.String() {
		case "enter", "n":
			m.step = YouTubeStepInstructions
			return m, nil
		}

	case YouTubeStepInstructions:
		switch msg.String() {
		case "enter", "c":
			m.step = YouTubeStepCredentials
			m.focusedInput = 0
			m.clientID.Focus()
			m.clientSecret.Blur()
			return m, textinput.Blink
		}

	case YouTubeStepSelectChannel:
		switch msg.String() {
		case "up", "k":
			if m.selectedChannelIndex > 0 {
				m.selectedChannelIndex--
			}
			return m, nil
		case "down", "j":
			if m.selectedChannelIndex < len(m.availableChannels)-1 {
				m.selectedChannelIndex++
			}
			return m, nil
		case "enter", " ":
			if m.selectedChannelIndex < len(m.availableChannels) {
				return m, func() tea.Msg {
					return youtubeChannelSelectedMsg{
						channel: m.availableChannels[m.selectedChannelIndex],
					}
				}
			}
			return m, nil
		}

	case YouTubeStepConnected:
		switch msg.String() {
		case "enter":
			return m, func() tea.Msg { return backToMenuMsg{} }
		case "d":
			return m, m.disconnect()
		case "v", "t":
			// Verify/Test credentials
			m.step = YouTubeStepVerifying
			m.isVerifying = true
			return m, m.verifyCredentials()
		case "p":
			// Manage playlists
			m.step = YouTubeStepPlaylists
			m.isLoadingPlaylists = true
			m.playlistPage = 0
			return m, m.loadPlaylists()
		case "a":
			// Manage accounts
			m.accounts = m.cfg.YouTube.GetAccounts()
			m.selectedAccountIndex = 0
			m.step = YouTubeStepAccounts
			return m, nil
		}

	case YouTubeStepVerifying:
		// No actions during verification
		return m, nil

	case YouTubeStepVerified:
		switch msg.String() {
		case "enter", "b":
			m.step = YouTubeStepConnected
			return m, nil
		case "up", "k":
			if m.playlistPage > 0 {
				m.playlistPage--
			}
		case "down", "j":
			maxPages := (len(m.playlists) - 1) / 5
			if m.playlistPage < maxPages {
				m.playlistPage++
			}
		}

	case YouTubeStepPlaylists:
		if m.isLoadingPlaylists {
			return m, nil
		}
		switch msg.String() {
		case "enter", "b", "esc":
			m.step = YouTubeStepConnected
			return m, nil
		case "n", "c":
			// Create new playlist
			m.step = YouTubeStepCreatePlaylist
			m.createPlaylistFocus = 0
			m.newPlaylistTitle.Focus()
			m.newPlaylistDesc.Blur()
			return m, textinput.Blink
		case "r":
			// Refresh playlists
			m.isLoadingPlaylists = true
			return m, m.loadPlaylists()
		case "up", "k":
			if m.playlistPage > 0 {
				m.playlistPage--
			}
		case "down", "j":
			maxPages := (len(m.playlists) - 1) / 10
			if m.playlistPage < maxPages {
				m.playlistPage++
			}
		}

	case YouTubeStepCreatePlaylist:
		if m.isCreatingPlaylist {
			return m, nil
		}
		switch msg.String() {
		case "esc":
			m.step = YouTubeStepPlaylists
			return m, nil
		case "tab", "shift+tab":
			// Cycle through: title -> desc -> privacy -> title
			m.createPlaylistFocus = (m.createPlaylistFocus + 1) % 3
			switch m.createPlaylistFocus {
			case 0:
				m.newPlaylistTitle.Focus()
				m.newPlaylistDesc.Blur()
			case 1:
				m.newPlaylistTitle.Blur()
				m.newPlaylistDesc.Focus()
			case 2:
				m.newPlaylistTitle.Blur()
				m.newPlaylistDesc.Blur()
			}
			return m, textinput.Blink
		case "left", "right":
			// Toggle privacy when on privacy field
			if m.createPlaylistFocus == 2 {
				switch m.newPlaylistPrivacy {
				case youtube.PrivacyPublic:
					m.newPlaylistPrivacy = youtube.PrivacyUnlisted
				case youtube.PrivacyUnlisted:
					m.newPlaylistPrivacy = youtube.PrivacyPrivate
				case youtube.PrivacyPrivate:
					m.newPlaylistPrivacy = youtube.PrivacyPublic
				}
			}
		case "enter":
			if m.createPlaylistFocus == 2 {
				// Create the playlist
				title := strings.TrimSpace(m.newPlaylistTitle.Value())
				if title == "" {
					m.playlistsError = "Title is required"
					return m, nil
				}
				m.isCreatingPlaylist = true
				m.playlistsError = ""
				return m, m.createPlaylist()
			}
			// Move to next field
			m.createPlaylistFocus = (m.createPlaylistFocus + 1) % 3
			switch m.createPlaylistFocus {
			case 0:
				m.newPlaylistTitle.Focus()
				m.newPlaylistDesc.Blur()
			case 1:
				m.newPlaylistTitle.Blur()
				m.newPlaylistDesc.Focus()
			case 2:
				m.newPlaylistTitle.Blur()
				m.newPlaylistDesc.Blur()
			}
			return m, textinput.Blink
		default:
			// Forward to focused text input
			var cmd tea.Cmd
			switch m.createPlaylistFocus {
			case 0:
				m.newPlaylistTitle, cmd = m.newPlaylistTitle.Update(msg)
			case 1:
				m.newPlaylistDesc, cmd = m.newPlaylistDesc.Update(msg)
			}
			return m, cmd
		}

	case YouTubeStepAccounts:
		switch msg.String() {
		case "enter", "b", "esc":
			m.step = YouTubeStepConnected
			return m, nil
		case "up", "k":
			if m.selectedAccountIndex > 0 {
				m.selectedAccountIndex--
			}
		case "down", "j":
			if m.selectedAccountIndex < len(m.accounts)-1 {
				m.selectedAccountIndex++
			}
		case "n", "a":
			// Add new account
			m.accountName.SetValue("")
			m.accountClientID.SetValue("")
			m.accountClientSecret.SetValue("")
			m.accountFormFocus = 0
			m.accountName.Focus()
			m.accountClientID.Blur()
			m.accountClientSecret.Blur()
			m.editingAccountID = ""
			m.step = YouTubeStepAccountAdd
			return m, textinput.Blink
		case "e":
			// Edit selected account
			if len(m.accounts) > 0 && m.selectedAccountIndex < len(m.accounts) {
				acc := m.accounts[m.selectedAccountIndex]
				m.accountName.SetValue(acc.Name)
				m.accountClientID.SetValue(acc.ClientID)
				m.accountClientSecret.SetValue(acc.ClientSecret)
				m.accountFormFocus = 0
				m.accountName.Focus()
				m.accountClientID.Blur()
				m.accountClientSecret.Blur()
				m.editingAccountID = acc.ID
				m.step = YouTubeStepAccountEdit
				return m, textinput.Blink
			}
		case "d":
			// Delete selected account
			if len(m.accounts) > 0 && m.selectedAccountIndex < len(m.accounts) {
				m.step = YouTubeStepAccountDelete
				return m, nil
			}
		case "c":
			// Connect/authenticate selected account
			if len(m.accounts) > 0 && m.selectedAccountIndex < len(m.accounts) {
				acc := m.accounts[m.selectedAccountIndex]
				if acc.IsConfigured() {
					return m, m.startAccountAuth(acc)
				}
			}
		}

	case YouTubeStepAccountAdd, YouTubeStepAccountEdit:
		if m.isAuthenticatingAccount {
			return m, nil
		}
		switch msg.String() {
		case "esc":
			m.step = YouTubeStepAccounts
			return m, nil
		case "tab", "shift+tab":
			// Cycle through fields: name -> clientID -> clientSecret -> name
			m.accountFormFocus = (m.accountFormFocus + 1) % 3
			switch m.accountFormFocus {
			case 0:
				m.accountName.Focus()
				m.accountClientID.Blur()
				m.accountClientSecret.Blur()
			case 1:
				m.accountName.Blur()
				m.accountClientID.Focus()
				m.accountClientSecret.Blur()
			case 2:
				m.accountName.Blur()
				m.accountClientID.Blur()
				m.accountClientSecret.Focus()
			}
			return m, textinput.Blink
		case "enter":
			// Save account
			name := strings.TrimSpace(m.accountName.Value())
			clientID := strings.TrimSpace(m.accountClientID.Value())
			clientSecret := strings.TrimSpace(m.accountClientSecret.Value())

			if name == "" {
				m.errorMessage = "Account name is required"
				return m, nil
			}
			if err := youtube.ValidateCredentials(context.Background(), clientID, clientSecret); err != nil {
				m.errorMessage = err.Error()
				return m, nil
			}

			if m.step == YouTubeStepAccountAdd {
				// Add new account
				newAccount := youtube.Account{
					Name:         name,
					ClientID:     clientID,
					ClientSecret: clientSecret,
				}
				m.cfg.YouTube.AddAccount(newAccount)
			} else {
				// Update existing account
				acc := m.cfg.YouTube.GetAccount(m.editingAccountID)
				if acc != nil {
					acc.Name = name
					acc.ClientID = clientID
					acc.ClientSecret = clientSecret
					m.cfg.YouTube.UpdateAccount(*acc)
				}
			}

			if err := config.Save(m.cfg); err != nil {
				m.errorMessage = "Failed to save: " + err.Error()
				return m, nil
			}

			m.accounts = m.cfg.YouTube.GetAccounts()
			m.errorMessage = ""
			m.step = YouTubeStepAccounts
			return m, nil
		default:
			// Forward to focused text input
			var cmd tea.Cmd
			switch m.accountFormFocus {
			case 0:
				m.accountName, cmd = m.accountName.Update(msg)
			case 1:
				m.accountClientID, cmd = m.accountClientID.Update(msg)
			case 2:
				m.accountClientSecret, cmd = m.accountClientSecret.Update(msg)
			}
			return m, cmd
		}

	case YouTubeStepAccountDelete:
		switch msg.String() {
		case "y", "Y":
			// Confirm delete
			if len(m.accounts) > 0 && m.selectedAccountIndex < len(m.accounts) {
				acc := m.accounts[m.selectedAccountIndex]
				m.cfg.YouTube.RemoveAccount(acc.ID)
				// Also delete the token file
				_ = youtube.DeleteTokenForAccount(config.GetConfigDir(), acc.ID)
				if err := config.Save(m.cfg); err != nil {
					m.errorMessage = "Failed to save: " + err.Error()
				}
				m.accounts = m.cfg.YouTube.GetAccounts()
				if m.selectedAccountIndex >= len(m.accounts) && m.selectedAccountIndex > 0 {
					m.selectedAccountIndex--
				}
			}
			m.step = YouTubeStepAccounts
			return m, nil
		case "n", "N", "esc":
			m.step = YouTubeStepAccounts
			return m, nil
		}

	case YouTubeStepError:
		switch msg.String() {
		case "enter", "r":
			m.step = YouTubeStepCredentials
			m.errorMessage = ""
			m.focusedInput = 0
			m.clientID.Focus()
			m.clientSecret.Blur()
			return m, textinput.Blink
		}
	}

	return m, nil
}

// handleEnter handles the enter key based on current step
func (m *YouTubeSetupModel) handleEnter() (*YouTubeSetupModel, tea.Cmd) {
	switch m.step {
	case YouTubeStepWelcome:
		m.step = YouTubeStepInstructions
		return m, nil

	case YouTubeStepInstructions:
		m.step = YouTubeStepCredentials
		m.clientID.Focus()
		return m, textinput.Blink

	case YouTubeStepCredentials:
		// Validate and save credentials
		clientID := strings.TrimSpace(m.clientID.Value())
		clientSecret := strings.TrimSpace(m.clientSecret.Value())

		if err := youtube.ValidateCredentials(context.Background(), clientID, clientSecret); err != nil {
			m.errorMessage = err.Error()
			return m, nil
		}

		// Save credentials
		m.cfg.YouTube.ClientID = clientID
		m.cfg.YouTube.ClientSecret = clientSecret
		if err := config.Save(m.cfg); err != nil {
			m.errorMessage = "Failed to save config: " + err.Error()
			return m, nil
		}

		// Start authentication
		return m, m.startAuth()

	case YouTubeStepConnected:
		// Return to menu
		return m, func() tea.Msg { return backToMenuMsg{} }

	case YouTubeStepError:
		m.step = YouTubeStepCredentials
		m.errorMessage = ""
		return m, nil
	}

	return m, nil
}

// authState holds the state for async authentication
type authState struct {
	urlChan    chan string
	resultChan chan tea.Msg
}

var currentAuthState *authState

// startAuth starts the OAuth authentication process
func (m *YouTubeSetupModel) startAuth() tea.Cmd {
	clientID := m.cfg.YouTube.ClientID
	clientSecret := m.cfg.YouTube.ClientSecret
	configDir := config.GetConfigDir()

	// Create channels for communication
	currentAuthState = &authState{
		urlChan:    make(chan string, 1),
		resultChan: make(chan tea.Msg, 1),
	}

	// Start authentication in background goroutine
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Minute)
		defer cancel()

		auth := youtube.NewAuth(clientID, clientSecret, configDir)

		// Use the callback to capture and send the URL
		err := auth.AuthenticateWithCallback(ctx, func(url string) {
			// Send the URL through the channel
			select {
			case currentAuthState.urlChan <- url:
			default:
			}
		})

		if err != nil {
			currentAuthState.resultChan <- youtubeAuthCompleteMsg{err: err}
			return
		}

		// Get channel name
		channelName, err := auth.GetChannelName(ctx)
		if err != nil {
			channelName = "Unknown Channel"
		}

		currentAuthState.resultChan <- youtubeAuthCompleteMsg{channelName: channelName}
	}()

	// Return a command that waits for the URL and signals auth started
	return func() tea.Msg {
		// Wait for the URL from the auth flow (with timeout)
		select {
		case url := <-currentAuthState.urlChan:
			return youtubeAuthStartedMsg{authURL: url}
		case <-time.After(5 * time.Second):
			// If we don't get a URL in 5 seconds, start anyway without it
			return youtubeAuthStartedMsg{authURL: ""}
		}
	}
}

// waitForAuthResult returns a command that waits for auth to complete
func (m *YouTubeSetupModel) waitForAuthResult() tea.Cmd {
	return func() tea.Msg {
		if currentAuthState == nil {
			return youtubeAuthCompleteMsg{err: nil}
		}
		// Wait for result with timeout
		select {
		case msg := <-currentAuthState.resultChan:
			return msg
		case <-time.After(5 * time.Minute):
			return youtubeAuthCompleteMsg{err: context.DeadlineExceeded}
		}
	}
}

// disconnect disconnects the YouTube account
func (m *YouTubeSetupModel) disconnect() tea.Cmd {
	configDir := config.GetConfigDir()

	return func() tea.Msg {
		// Delete token
		_ = youtube.DeleteToken(configDir)
		return youtubeDisconnectMsg{}
	}
}

// loadAvailableChannels loads all channels the user can manage (including brand accounts)
func (m *YouTubeSetupModel) loadAvailableChannels() tea.Cmd {
	clientID := m.cfg.YouTube.ClientID
	clientSecret := m.cfg.YouTube.ClientSecret
	configDir := config.GetConfigDir()

	return func() tea.Msg {
		ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		defer cancel()

		auth := youtube.NewAuth(clientID, clientSecret, configDir)

		// Get all managed channels
		channels, err := auth.GetManagedChannels(ctx)
		if err != nil {
			return youtubeChannelsLoadedMsg{err: err}
		}

		return youtubeChannelsLoadedMsg{channels: channels}
	}
}

// loadAvailableChannelsForAccount loads channels for a specific account
func (m *YouTubeSetupModel) loadAvailableChannelsForAccount(account youtube.Account) tea.Cmd {
	configDir := config.GetConfigDir()

	return func() tea.Msg {
		ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		defer cancel()

		auth := youtube.NewAuthForAccount(account.ClientID, account.ClientSecret, configDir, account.ID)

		// Get all managed channels
		channels, err := auth.GetManagedChannels(ctx)
		if err != nil {
			return youtubeChannelsLoadedMsg{err: err}
		}

		return youtubeChannelsLoadedMsg{channels: channels}
	}
}

// verifyCredentials tests the credentials and fetches channel/playlist info
func (m *YouTubeSetupModel) verifyCredentials() tea.Cmd {
	clientID := m.cfg.YouTube.ClientID
	clientSecret := m.cfg.YouTube.ClientSecret
	configDir := config.GetConfigDir()

	return func() tea.Msg {
		ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		defer cancel()

		auth := youtube.NewAuth(clientID, clientSecret, configDir)

		// Test the connection
		if err := auth.TestConnection(ctx); err != nil {
			return youtubeVerifyCompleteMsg{err: err}
		}

		// Get channel info
		channelName, err := auth.GetChannelName(ctx)
		if err != nil {
			return youtubeVerifyCompleteMsg{err: err}
		}

		// Get channel ID
		channelID, err := auth.GetChannelID(ctx)
		if err != nil {
			channelID = ""
		}

		// Get playlists
		uploader, err := youtube.NewUploader(ctx, auth)
		if err != nil {
			return youtubeVerifyCompleteMsg{
				channelName: channelName,
				channelID:   channelID,
				err:         nil,
			}
		}

		playlists, err := uploader.ListPlaylists(ctx)
		if err != nil {
			playlists = nil
		}

		return youtubeVerifyCompleteMsg{
			channelName: channelName,
			channelID:   channelID,
			playlists:   playlists,
		}
	}
}

// loadPlaylists fetches playlists from YouTube
func (m *YouTubeSetupModel) loadPlaylists() tea.Cmd {
	clientID := m.cfg.YouTube.ClientID
	clientSecret := m.cfg.YouTube.ClientSecret
	configDir := config.GetConfigDir()

	return func() tea.Msg {
		ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		defer cancel()

		auth := youtube.NewAuth(clientID, clientSecret, configDir)
		uploader, err := youtube.NewUploader(ctx, auth)
		if err != nil {
			return youtubePlaylistsLoadedMsg{err: err}
		}

		playlists, err := uploader.ListPlaylists(ctx)
		if err != nil {
			return youtubePlaylistsLoadedMsg{err: err}
		}

		return youtubePlaylistsLoadedMsg{playlists: playlists}
	}
}

// createPlaylist creates a new playlist on YouTube
func (m *YouTubeSetupModel) createPlaylist() tea.Cmd {
	clientID := m.cfg.YouTube.ClientID
	clientSecret := m.cfg.YouTube.ClientSecret
	configDir := config.GetConfigDir()
	title := strings.TrimSpace(m.newPlaylistTitle.Value())
	desc := strings.TrimSpace(m.newPlaylistDesc.Value())
	privacy := m.newPlaylistPrivacy

	return func() tea.Msg {
		ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		defer cancel()

		auth := youtube.NewAuth(clientID, clientSecret, configDir)
		uploader, err := youtube.NewUploader(ctx, auth)
		if err != nil {
			return youtubePlaylistCreatedMsg{err: err}
		}

		playlist, err := uploader.CreatePlaylist(ctx, title, desc, privacy)
		if err != nil {
			return youtubePlaylistCreatedMsg{err: err}
		}

		return youtubePlaylistCreatedMsg{playlist: playlist}
	}
}

// accountAuthState holds the state for async account authentication
var currentAccountAuthState *authState

// startAccountAuth starts OAuth authentication for a specific account
func (m *YouTubeSetupModel) startAccountAuth(acc youtube.Account) tea.Cmd {
	clientID := acc.ClientID
	clientSecret := acc.ClientSecret
	accountID := acc.ID
	configDir := config.GetConfigDir()

	// Create channels for communication
	currentAccountAuthState = &authState{
		urlChan:    make(chan string, 1),
		resultChan: make(chan tea.Msg, 1),
	}

	// Start authentication in background goroutine
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Minute)
		defer cancel()

		auth := youtube.NewAuthForAccount(clientID, clientSecret, configDir, accountID)

		// Use the callback to capture and send the URL
		err := auth.AuthenticateWithCallback(ctx, func(url string) {
			select {
			case currentAccountAuthState.urlChan <- url:
			default:
			}
		})

		if err != nil {
			currentAccountAuthState.resultChan <- youtubeAccountAuthCompleteMsg{err: err}
			return
		}

		// Get channel info
		channelName, _ := auth.GetChannelName(ctx)
		channelID, _ := auth.GetChannelID(ctx)

		currentAccountAuthState.resultChan <- youtubeAccountAuthCompleteMsg{
			channelName: channelName,
			channelID:   channelID,
		}
	}()

	// Return a command that waits for the URL and signals auth started
	return func() tea.Msg {
		select {
		case url := <-currentAccountAuthState.urlChan:
			return youtubeAccountAuthStartedMsg{authURL: url}
		case <-time.After(5 * time.Second):
			return youtubeAccountAuthStartedMsg{authURL: ""}
		}
	}
}

// waitForAccountAuthResult returns a command that waits for account auth to complete
func (m *YouTubeSetupModel) waitForAccountAuthResult() tea.Cmd {
	return func() tea.Msg {
		if currentAccountAuthState == nil {
			return youtubeAccountAuthCompleteMsg{err: nil}
		}
		select {
		case msg := <-currentAccountAuthState.resultChan:
			return msg
		case <-time.After(5 * time.Minute):
			return youtubeAccountAuthCompleteMsg{err: context.DeadlineExceeded}
		}
	}
}

// View renders the YouTube setup screen
func (m *YouTubeSetupModel) View() string {
	if m.width == 0 {
		return "Loading..."
	}

	var content string

	switch m.step {
	case YouTubeStepWelcome:
		content = m.renderWelcome()
	case YouTubeStepInstructions:
		content = m.renderInstructions()
	case YouTubeStepCredentials:
		content = m.renderCredentials()
	case YouTubeStepAuthenticating:
		content = m.renderAuthenticating()
	case YouTubeStepSelectChannel:
		content = m.renderSelectChannel()
	case YouTubeStepConnected:
		content = m.renderConnected()
	case YouTubeStepVerifying:
		content = m.renderVerifying()
	case YouTubeStepVerified:
		content = m.renderVerified()
	case YouTubeStepPlaylists:
		content = m.renderPlaylists()
	case YouTubeStepCreatePlaylist:
		content = m.renderCreatePlaylist()
	case YouTubeStepAccounts:
		content = m.renderAccounts()
	case YouTubeStepAccountAdd, YouTubeStepAccountEdit:
		content = m.renderAccountForm()
	case YouTubeStepAccountDelete:
		content = m.renderAccountDelete()
	case YouTubeStepError:
		content = m.renderError()
	}

	return content
}

// renderWelcome renders the welcome screen
func (m *YouTubeSetupModel) renderWelcome() string {
	header := RenderHeader("YouTube Integration")

	// YouTube logo in ASCII
	logo := lipgloss.NewStyle().Foreground(ColorRed).Bold(true).Render(`
    ╔══════════════════════════╗
    ║                          ║
    ║      ▶  YouTube          ║
    ║                          ║
    ╚══════════════════════════╝
`)

	titleStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Bold(true).
		Align(lipgloss.Center)

	descStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Align(lipgloss.Center).
		Width(60)

	title := titleStyle.Render("Upload Videos Directly to YouTube")

	desc := descStyle.Render(`
Connect your YouTube account to upload processed recordings
directly from Kartoza Video Processor.

Features:
• Automatic thumbnail generation
• Set title, description, and tags
• Choose privacy settings
• Add to playlists
• Track upload progress
`)

	buttonStyle := lipgloss.NewStyle().
		Padding(1, 4).
		Background(ColorRed).
		Foreground(ColorWhite).
		Bold(true)

	button := buttonStyle.Render("Get Started →")

	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	helpText := helpStyle.Render("enter: continue • esc: back")

	content := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		logo,
		"",
		title,
		desc,
		"",
		button,
		"",
		helpText,
	)

	return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, content)
}

// renderInstructions renders the setup instructions
func (m *YouTubeSetupModel) renderInstructions() string {
	header := RenderHeader("YouTube Setup - Instructions")

	instructionStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorOrange).
		Padding(1, 2).
		Width(70)

	stepStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	textStyle := lipgloss.NewStyle().
		Foreground(ColorWhite)

	linkStyle := lipgloss.NewStyle().
		Foreground(ColorBlue).
		Underline(true)

	instructions := lipgloss.JoinVertical(lipgloss.Left,
		stepStyle.Render("Step 1: Go to Google Cloud Console"),
		textStyle.Render("  Visit: ")+linkStyle.Render("https://console.cloud.google.com/"),
		"",
		stepStyle.Render("Step 2: Create or select a project"),
		textStyle.Render("  Create a new project or use an existing one"),
		textStyle.Render("  TIP: Name it after your YouTube channel for clarity"),
		"",
		stepStyle.Render("Step 3: Enable YouTube Data API v3"),
		textStyle.Render("  Go to APIs & Services → Library"),
		textStyle.Render("  Search for \"YouTube Data API v3\" and enable it"),
		"",
		stepStyle.Render("Step 4: Create OAuth credentials"),
		textStyle.Render("  Go to APIs & Services → Credentials"),
		textStyle.Render("  Click \"Create Credentials\" → \"OAuth client ID\""),
		textStyle.Render("  Select \"Desktop app\" as the application type"),
		"",
		stepStyle.Render("Step 5: Configure OAuth consent screen"),
		textStyle.Render("  Go to OAuth consent screen"),
		textStyle.Render("  Set user type to \"External\" (or \"Internal\" for Workspace)"),
		textStyle.Render("  Add your email as a test user"),
		"",
		stepStyle.Render("Step 6: Copy your credentials"),
		textStyle.Render("  Copy the Client ID and Client Secret"),
	)

	brandNote := lipgloss.JoinVertical(lipgloss.Left,
		"",
		stepStyle.Render("Brand Accounts:"),
		textStyle.Render("  If you have multiple YouTube channels (personal + brand accounts):"),
		textStyle.Render("  • During OAuth login, Google will ask which account to use"),
		textStyle.Render("  • Select your brand account from the account chooser"),
		textStyle.Render("  • The API project doesn't need to match - it just provides access"),
		textStyle.Render("  • You can manage brand accounts at: ")+linkStyle.Render("youtube.com/account"),
	)

	fullInstructions := lipgloss.JoinVertical(lipgloss.Left, instructions, brandNote)
	content := instructionStyle.Render(fullInstructions)

	noteStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true).
		Width(70).
		Align(lipgloss.Center)

	note := noteStyle.Render("Note: While in testing mode, only users added as test users can authenticate.")

	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	helpText := helpStyle.Render("c: continue to credentials • esc: back")

	fullContent := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		content,
		"",
		note,
		"",
		helpText,
	)

	return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, fullContent)
}

// renderCredentials renders the credentials input screen
func (m *YouTubeSetupModel) renderCredentials() string {
	header := RenderHeader("YouTube Setup - Credentials")

	titleStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	focusedLabelStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	hintStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	var rows []string

	rows = append(rows, titleStyle.Render("Enter your Google OAuth credentials:"))
	rows = append(rows, "")

	// Client ID
	if m.focusedInput == 0 {
		rows = append(rows, focusedLabelStyle.Render("▶ Client ID:"))
	} else {
		rows = append(rows, labelStyle.Render("  Client ID:"))
	}
	rows = append(rows, "  "+m.clientID.View())
	rows = append(rows, hintStyle.Render("  (ends with .apps.googleusercontent.com)"))
	rows = append(rows, "")

	// Client Secret
	if m.focusedInput == 1 {
		rows = append(rows, focusedLabelStyle.Render("▶ Client Secret:"))
	} else {
		rows = append(rows, labelStyle.Render("  Client Secret:"))
	}
	rows = append(rows, "  "+m.clientSecret.View())
	rows = append(rows, hintStyle.Render("  (starts with GOCSPX-)"))

	// Error message
	if m.errorMessage != "" {
		errorStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Bold(true)
		rows = append(rows, "")
		rows = append(rows, errorStyle.Render("Error: "+m.errorMessage))
	}

	content := lipgloss.JoinVertical(lipgloss.Left, rows...)

	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	helpText := helpStyle.Render("tab: switch field • enter: connect • esc: cancel")

	footer := RenderHelpFooter(helpText, m.width)

	return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
}

// renderSelectChannel renders the channel selection screen for brand accounts
func (m *YouTubeSetupModel) renderSelectChannel() string {
	header := RenderHeader("YouTube - Select Channel")

	// Loading state
	if m.isLoadingChannels {
		spinnerFrames := []string{"◐", "◓", "◑", "◒"}
		frame := spinnerFrames[int(time.Now().UnixMilli()/200)%len(spinnerFrames)]

		spinnerStyle := lipgloss.NewStyle().
			Foreground(ColorOrange).
			Bold(true)

		messageStyle := lipgloss.NewStyle().
			Foreground(ColorWhite).
			Bold(true)

		spinner := spinnerStyle.Render(frame)
		message := messageStyle.Render("Loading available channels...")

		content := lipgloss.JoinVertical(
			lipgloss.Center,
			spinner+" "+message,
		)

		helpText := "Please wait..."
		footer := RenderHelpFooter(helpText, m.width)

		return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
	}

	titleStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	descStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	selectedStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	normalStyle := lipgloss.NewStyle().
		Foreground(ColorWhite)

	brandStyle := lipgloss.NewStyle().
		Foreground(ColorBlue).
		Italic(true)

	var rows []string

	rows = append(rows, titleStyle.Render("Select which YouTube channel to use:"))
	rows = append(rows, "")
	rows = append(rows, descStyle.Render("You have access to multiple YouTube channels."))
	rows = append(rows, descStyle.Render("Select the channel you want to upload videos to."))
	rows = append(rows, "")

	// List channels
	for i, ch := range m.availableChannels {
		var line string
		channelName := ch.Title
		if ch.IsBrand {
			channelName += " " + brandStyle.Render("(Brand Account)")
		}

		if i == m.selectedChannelIndex {
			line = selectedStyle.Render("▶ " + channelName)
		} else {
			line = normalStyle.Render("  " + channelName)
		}
		rows = append(rows, line)
	}

	content := lipgloss.JoinVertical(lipgloss.Left, rows...)

	helpText := "↑/k: up • ↓/j: down • enter: select • esc: cancel"
	footer := RenderHelpFooter(helpText, m.width)

	return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
}

// renderAuthenticating renders the authenticating screen
func (m *YouTubeSetupModel) renderAuthenticating() string {
	header := RenderHeader("YouTube Setup - Authenticating")

	spinnerFrames := []string{"◐", "◓", "◑", "◒"}
	frame := spinnerFrames[int(time.Now().UnixMilli()/200)%len(spinnerFrames)]

	spinnerStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	messageStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Bold(true)

	subMessageStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	linkStyle := lipgloss.NewStyle().
		Foreground(ColorBlue)

	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Bold(true)

	spinner := spinnerStyle.Render(frame)
	message := messageStyle.Render("Waiting for authorization...")
	subMessage := subMessageStyle.Render("A browser window should have opened.\nPlease sign in to your Google account and grant access.")

	var rows []string
	rows = append(rows, spinner+" "+message)
	rows = append(rows, "")
	rows = append(rows, subMessage)
	rows = append(rows, "")

	// Show the auth URL for manual opening
	if m.authURL != "" {
		rows = append(rows, labelStyle.Render("If browser didn't open, visit this URL:"))
		rows = append(rows, "")
		// Wrap the URL if it's too long
		url := m.authURL
		if len(url) > 70 {
			// Show truncated URL with indication it's truncated
			rows = append(rows, linkStyle.Render(url[:70]+"..."))
		} else {
			rows = append(rows, linkStyle.Render(url))
		}
		rows = append(rows, "")
	}

	rows = append(rows, subMessageStyle.Render("This may take a moment..."))

	content := lipgloss.JoinVertical(lipgloss.Center, rows...)

	helpText := "Waiting for browser authentication..."
	footer := RenderHelpFooter(helpText, m.width)

	return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
}

// renderConnected renders the connected screen
func (m *YouTubeSetupModel) renderConnected() string {
	header := RenderHeader("YouTube Connected")

	checkStyle := lipgloss.NewStyle().
		Foreground(ColorGreen).
		Bold(true)

	check := checkStyle.Render("✓")

	titleStyle := lipgloss.NewStyle().
		Foreground(ColorGreen).
		Bold(true)

	title := titleStyle.Render("Successfully Connected!")

	containerStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorGreen).
		Padding(1, 3).
		Width(50)

	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	valueStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Bold(true)

	channelName := m.channelName
	if channelName == "" {
		channelName = "Connected"
	}

	info := lipgloss.JoinVertical(lipgloss.Left,
		lipgloss.JoinHorizontal(lipgloss.Top,
			labelStyle.Render("Channel: "),
			valueStyle.Render(channelName),
		),
		"",
		lipgloss.JoinHorizontal(lipgloss.Top,
			labelStyle.Render("Status: "),
			checkStyle.Render("Ready to upload"),
		),
	)

	content := containerStyle.Render(info)

	buttonStyle := lipgloss.NewStyle().
		Padding(0, 3).
		Background(ColorGreen).
		Foreground(ColorWhite).
		Bold(true)

	button := buttonStyle.Render("Done")

	optionStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	optionsText := lipgloss.JoinVertical(lipgloss.Center,
		optionStyle.Render("a: manage accounts"),
		optionStyle.Render("p: manage playlists"),
		optionStyle.Render("v: verify credentials"),
		optionStyle.Render("d: disconnect account"),
	)

	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	helpText := helpStyle.Render("enter: menu • a: accounts • p: playlists • v: verify • d: disconnect")

	fullContent := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		check+" "+title,
		"",
		content,
		"",
		button,
		"",
		optionsText,
		"",
		helpText,
	)

	return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, fullContent)
}

// renderError renders the error screen
func (m *YouTubeSetupModel) renderError() string {
	header := RenderHeader("YouTube Setup - Error")

	errorStyle := lipgloss.NewStyle().
		Foreground(ColorRed).
		Bold(true)

	errorIcon := errorStyle.Render("✗")

	titleStyle := lipgloss.NewStyle().
		Foreground(ColorRed).
		Bold(true)

	title := titleStyle.Render("Authentication Failed")

	containerStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorRed).
		Padding(1, 3).
		Width(60)

	messageStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Width(52)

	content := containerStyle.Render(messageStyle.Render(m.errorMessage))

	buttonStyle := lipgloss.NewStyle().
		Padding(0, 3).
		Background(ColorOrange).
		Foreground(ColorWhite).
		Bold(true)

	button := buttonStyle.Render("Try Again")

	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	helpText := helpStyle.Render("r/enter: retry • esc: back")

	fullContent := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		errorIcon + " " + title,
		"",
		content,
		"",
		button,
		"",
		helpText,
	)

	return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, fullContent)
}

// Message types for YouTube setup
type youtubeAuthStartedMsg struct {
	authURL string
}
type youtubeAuthCompleteMsg struct {
	err         error
	channelName string
}
type youtubeDisconnectMsg struct{}
type youtubeVerifyCompleteMsg struct {
	err         error
	channelName string
	channelID   string
	playlists   []youtube.Playlist
}
type youtubePlaylistsLoadedMsg struct {
	err       error
	playlists []youtube.Playlist
}
type youtubePlaylistCreatedMsg struct {
	err      error
	playlist *youtube.Playlist
}
type youtubeAccountAuthStartedMsg struct {
	authURL string
}
type youtubeAccountAuthCompleteMsg struct {
	err         error
	channelName string
	channelID   string
}
type youtubeChannelsLoadedMsg struct {
	err      error
	channels []youtube.Channel
}
type youtubeChannelSelectedMsg struct {
	channel youtube.Channel
}

// renderVerifying renders the verification in progress screen
func (m *YouTubeSetupModel) renderVerifying() string {
	header := RenderHeader("YouTube - Verifying Credentials")

	spinnerFrames := []string{"◐", "◓", "◑", "◒"}
	frame := spinnerFrames[int(time.Now().UnixMilli()/200)%len(spinnerFrames)]

	spinnerStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	messageStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Bold(true)

	subMessageStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	spinner := spinnerStyle.Render(frame)
	message := messageStyle.Render("Verifying credentials...")
	subMessage := subMessageStyle.Render("Testing connection and fetching channel information...")

	content := lipgloss.JoinVertical(
		lipgloss.Center,
		spinner+" "+message,
		"",
		subMessage,
	)

	helpText := "Please wait..."
	footer := RenderHelpFooter(helpText, m.width)

	return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
}

// renderVerified renders the verification results screen
func (m *YouTubeSetupModel) renderVerified() string {
	header := RenderHeader("YouTube - Verification Results")

	// Check if there was an error
	if m.verifyError != "" {
		errorStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Bold(true)

		containerStyle := lipgloss.NewStyle().
			Border(lipgloss.RoundedBorder()).
			BorderForeground(ColorRed).
			Padding(1, 3).
			Width(60)

		errorContent := lipgloss.JoinVertical(lipgloss.Left,
			errorStyle.Render("✗ Verification Failed"),
			"",
			m.verifyError,
		)

		content := containerStyle.Render(errorContent)

		helpStyle := lipgloss.NewStyle().
			Foreground(ColorGray).
			Italic(true)

		helpText := helpStyle.Render("enter/b: back to settings • esc: menu")

		fullContent := lipgloss.JoinVertical(
			lipgloss.Center,
			header,
			"",
			content,
			"",
			helpText,
		)

		return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, fullContent)
	}

	// Success - show channel info and playlists
	checkStyle := lipgloss.NewStyle().
		Foreground(ColorGreen).
		Bold(true)

	titleStyle := lipgloss.NewStyle().
		Foreground(ColorGreen).
		Bold(true)

	containerStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorGreen).
		Padding(1, 3).
		Width(60)

	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	valueStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Bold(true)

	sectionStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	// Channel info section
	channelInfo := lipgloss.JoinVertical(lipgloss.Left,
		checkStyle.Render("✓")+" "+titleStyle.Render("Credentials Verified!"),
		"",
		labelStyle.Render("Channel Name: ")+valueStyle.Render(m.channelName),
		labelStyle.Render("Channel ID:   ")+valueStyle.Render(m.channelID),
	)

	// Playlists section
	var playlistsContent string
	if len(m.playlists) == 0 {
		playlistsContent = lipgloss.JoinVertical(lipgloss.Left,
			"",
			sectionStyle.Render("Playlists:"),
			labelStyle.Render("  No playlists found"),
		)
	} else {
		var playlistRows []string
		playlistRows = append(playlistRows, "")
		playlistRows = append(playlistRows, sectionStyle.Render("Playlists:")+" "+labelStyle.Render(fmt.Sprintf("(%d total)", len(m.playlists))))

		// Show playlists with pagination (5 per page)
		startIdx := m.playlistPage * 5
		endIdx := startIdx + 5
		if endIdx > len(m.playlists) {
			endIdx = len(m.playlists)
		}

		for i := startIdx; i < endIdx; i++ {
			playlist := m.playlists[i]
			itemCount := ""
			if playlist.ItemCount > 0 {
				itemCount = fmt.Sprintf(" (%d videos)", playlist.ItemCount)
			}
			playlistRows = append(playlistRows, labelStyle.Render("  • ")+valueStyle.Render(playlist.Title)+labelStyle.Render(itemCount))
		}

		// Show pagination info if needed
		if len(m.playlists) > 5 {
			totalPages := (len(m.playlists) + 4) / 5
			pageInfo := labelStyle.Render(fmt.Sprintf("  Page %d of %d", m.playlistPage+1, totalPages))
			playlistRows = append(playlistRows, "")
			playlistRows = append(playlistRows, pageInfo)
		}

		playlistsContent = lipgloss.JoinVertical(lipgloss.Left, playlistRows...)
	}

	fullInfo := lipgloss.JoinVertical(lipgloss.Left, channelInfo, playlistsContent)
	content := containerStyle.Render(fullInfo)

	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	var helpText string
	if len(m.playlists) > 5 {
		helpText = helpStyle.Render("↑/↓: scroll playlists • enter/b: back • esc: menu")
	} else {
		helpText = helpStyle.Render("enter/b: back to settings • esc: menu")
	}

	fullContent := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		content,
		"",
		helpText,
	)

	return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, fullContent)
}

// renderPlaylists renders the playlist management screen
func (m *YouTubeSetupModel) renderPlaylists() string {
	header := RenderHeader("YouTube - Manage Playlists")

	// Show loading spinner if still loading
	if m.isLoadingPlaylists {
		spinnerFrames := []string{"◐", "◓", "◑", "◒"}
		frame := spinnerFrames[int(time.Now().UnixMilli()/200)%len(spinnerFrames)]

		spinnerStyle := lipgloss.NewStyle().
			Foreground(ColorOrange).
			Bold(true)

		messageStyle := lipgloss.NewStyle().
			Foreground(ColorWhite).
			Bold(true)

		content := lipgloss.JoinVertical(
			lipgloss.Center,
			spinnerStyle.Render(frame)+" "+messageStyle.Render("Loading playlists..."),
		)

		helpText := "Please wait..."
		footer := RenderHelpFooter(helpText, m.width)
		return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
	}

	containerStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorOrange).
		Padding(1, 2).
		Width(60)

	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	valueStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Bold(true)

	sectionStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	// Error display
	if m.playlistsError != "" {
		errorStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Bold(true)

		errorContent := lipgloss.JoinVertical(lipgloss.Left,
			errorStyle.Render("Error: "+m.playlistsError),
		)
		content := containerStyle.Render(errorContent)

		helpStyle := lipgloss.NewStyle().
			Foreground(ColorGray).
			Italic(true)

		helpText := helpStyle.Render("r: retry • n: new playlist • enter/b: back • esc: menu")

		fullContent := lipgloss.JoinVertical(
			lipgloss.Center,
			header,
			"",
			content,
			"",
			helpText,
		)
		return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, fullContent)
	}

	// Build playlists list
	var rows []string
	rows = append(rows, sectionStyle.Render("Your Playlists")+" "+labelStyle.Render(fmt.Sprintf("(%d total)", len(m.playlists))))
	rows = append(rows, "")

	if len(m.playlists) == 0 {
		rows = append(rows, labelStyle.Render("No playlists found"))
		rows = append(rows, "")
		rows = append(rows, labelStyle.Render("n: create your first playlist"))
	} else {
		// Show playlists with pagination (10 per page)
		startIdx := m.playlistPage * 10
		endIdx := startIdx + 10
		if endIdx > len(m.playlists) {
			endIdx = len(m.playlists)
		}

		for i := startIdx; i < endIdx; i++ {
			playlist := m.playlists[i]
			itemCount := ""
			if playlist.ItemCount > 0 {
				itemCount = fmt.Sprintf(" (%d videos)", playlist.ItemCount)
			}
			rows = append(rows, labelStyle.Render("  • ")+valueStyle.Render(playlist.Title)+labelStyle.Render(itemCount))
		}

		// Show pagination info if needed
		if len(m.playlists) > 10 {
			totalPages := (len(m.playlists) + 9) / 10
			rows = append(rows, "")
			rows = append(rows, labelStyle.Render(fmt.Sprintf("  Page %d of %d (↑/↓ to navigate)", m.playlistPage+1, totalPages)))
		}
	}

	content := containerStyle.Render(lipgloss.JoinVertical(lipgloss.Left, rows...))

	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	helpText := helpStyle.Render("n: new playlist • r: refresh • enter/b: back • esc: menu")

	fullContent := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		content,
		"",
		helpText,
	)

	return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, fullContent)
}

// renderCreatePlaylist renders the create playlist form
func (m *YouTubeSetupModel) renderCreatePlaylist() string {
	header := RenderHeader("YouTube - Create Playlist")

	// Show creating spinner if in progress
	if m.isCreatingPlaylist {
		spinnerFrames := []string{"◐", "◓", "◑", "◒"}
		frame := spinnerFrames[int(time.Now().UnixMilli()/200)%len(spinnerFrames)]

		spinnerStyle := lipgloss.NewStyle().
			Foreground(ColorOrange).
			Bold(true)

		messageStyle := lipgloss.NewStyle().
			Foreground(ColorWhite).
			Bold(true)

		content := lipgloss.JoinVertical(
			lipgloss.Center,
			spinnerStyle.Render(frame)+" "+messageStyle.Render("Creating playlist..."),
		)

		helpText := "Please wait..."
		footer := RenderHelpFooter(helpText, m.width)
		return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
	}

	titleStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	focusedLabelStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	valueStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Bold(true)

	var rows []string

	rows = append(rows, titleStyle.Render("Create a new playlist:"))
	rows = append(rows, "")

	// Title field
	if m.createPlaylistFocus == 0 {
		rows = append(rows, focusedLabelStyle.Render("▶ Title:"))
	} else {
		rows = append(rows, labelStyle.Render("  Title:"))
	}
	rows = append(rows, "  "+m.newPlaylistTitle.View())
	rows = append(rows, "")

	// Description field
	if m.createPlaylistFocus == 1 {
		rows = append(rows, focusedLabelStyle.Render("▶ Description:"))
	} else {
		rows = append(rows, labelStyle.Render("  Description:"))
	}
	rows = append(rows, "  "+m.newPlaylistDesc.View())
	rows = append(rows, "")

	// Privacy field
	var privacyLabel string
	if m.createPlaylistFocus == 2 {
		privacyLabel = focusedLabelStyle.Render("▶ Privacy: ")
	} else {
		privacyLabel = labelStyle.Render("  Privacy: ")
	}

	var privacyOptions []string
	privacies := []youtube.PrivacyStatus{youtube.PrivacyPublic, youtube.PrivacyUnlisted, youtube.PrivacyPrivate}
	labels := []string{"Public", "Unlisted", "Private"}
	for i, p := range privacies {
		if p == m.newPlaylistPrivacy {
			privacyOptions = append(privacyOptions, valueStyle.Render("["+labels[i]+"]"))
		} else {
			privacyOptions = append(privacyOptions, labelStyle.Render(" "+labels[i]+" "))
		}
	}
	rows = append(rows, privacyLabel+strings.Join(privacyOptions, " "))

	if m.createPlaylistFocus == 2 {
		rows = append(rows, labelStyle.Render("  (Use ← → to change, Enter to create)"))
	}

	// Error message
	if m.playlistsError != "" {
		errorStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Bold(true)
		rows = append(rows, "")
		rows = append(rows, errorStyle.Render("Error: "+m.playlistsError))
	}

	content := lipgloss.JoinVertical(lipgloss.Left, rows...)

	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	helpText := helpStyle.Render("tab: next field • ←/→: change privacy • enter: create • esc: cancel")

	footer := RenderHelpFooter(helpText, m.width)

	return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
}

// renderAccounts renders the account list screen
func (m *YouTubeSetupModel) renderAccounts() string {
	header := RenderHeader("YouTube - Manage Accounts")

	// Show authenticating spinner if in progress
	if m.isAuthenticatingAccount {
		spinnerFrames := []string{"◐", "◓", "◑", "◒"}
		frame := spinnerFrames[int(time.Now().UnixMilli()/200)%len(spinnerFrames)]

		spinnerStyle := lipgloss.NewStyle().
			Foreground(ColorOrange).
			Bold(true)

		messageStyle := lipgloss.NewStyle().
			Foreground(ColorWhite).
			Bold(true)

		subMessageStyle := lipgloss.NewStyle().
			Foreground(ColorGray)

		linkStyle := lipgloss.NewStyle().
			Foreground(ColorBlue)

		var rows []string
		rows = append(rows, spinnerStyle.Render(frame)+" "+messageStyle.Render("Authenticating account..."))
		rows = append(rows, "")
		rows = append(rows, subMessageStyle.Render("A browser window should have opened."))
		rows = append(rows, subMessageStyle.Render("Please sign in and grant access."))

		if m.accountAuthURL != "" {
			rows = append(rows, "")
			rows = append(rows, subMessageStyle.Render("If browser didn't open, visit:"))
			url := m.accountAuthURL
			if len(url) > 60 {
				url = url[:60] + "..."
			}
			rows = append(rows, linkStyle.Render(url))
		}

		content := lipgloss.JoinVertical(lipgloss.Center, rows...)

		helpText := "Waiting for browser authentication..."
		footer := RenderHelpFooter(helpText, m.width)
		return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
	}

	containerStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorOrange).
		Padding(1, 2).
		Width(65)

	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	valueStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Bold(true)

	selectedStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	connectedStyle := lipgloss.NewStyle().
		Foreground(ColorGreen)

	notConnectedStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	sectionStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	// Build accounts list
	var rows []string
	rows = append(rows, sectionStyle.Render("Your YouTube Accounts")+" "+labelStyle.Render(fmt.Sprintf("(%d total)", len(m.accounts))))
	rows = append(rows, "")

	if len(m.accounts) == 0 {
		rows = append(rows, labelStyle.Render("No accounts configured"))
		rows = append(rows, "")
		rows = append(rows, labelStyle.Render("n: add your first account"))
	} else {
		configDir := config.GetConfigDir()
		for i, acc := range m.accounts {
			// Show selection indicator
			var prefix string
			var nameStyle lipgloss.Style
			if i == m.selectedAccountIndex {
				prefix = "▶ "
				nameStyle = selectedStyle
			} else {
				prefix = "  "
				nameStyle = valueStyle
			}

			// Get display name
			displayName := acc.Name
			if displayName == "" {
				displayName = acc.ChannelName
			}
			if displayName == "" {
				displayName = "Unnamed Account"
			}

			// Check connection status
			var statusText string
			if youtube.IsAccountAuthenticated(&m.cfg.YouTube, configDir, acc.ID) {
				channelInfo := ""
				if acc.ChannelName != "" {
					channelInfo = " (" + acc.ChannelName + ")"
				}
				statusText = connectedStyle.Render("✓ Connected") + labelStyle.Render(channelInfo)
			} else if acc.IsConfigured() {
				statusText = notConnectedStyle.Render("○ Not connected")
			} else {
				statusText = notConnectedStyle.Render("○ Not configured")
			}

			row := prefix + nameStyle.Render(displayName) + "  " + statusText
			rows = append(rows, row)
		}
	}

	// Error message
	if m.errorMessage != "" {
		errorStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Bold(true)
		rows = append(rows, "")
		rows = append(rows, errorStyle.Render("Error: "+m.errorMessage))
	}

	content := containerStyle.Render(lipgloss.JoinVertical(lipgloss.Left, rows...))

	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	helpText := helpStyle.Render("n: add • e: edit • d: delete • c: connect • enter: back")

	fullContent := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		content,
		"",
		helpText,
	)

	return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, fullContent)
}

// renderAccountForm renders the add/edit account form
func (m *YouTubeSetupModel) renderAccountForm() string {
	var title string
	if m.step == YouTubeStepAccountAdd {
		title = "YouTube - Add Account"
	} else {
		title = "YouTube - Edit Account"
	}
	header := RenderHeader(title)

	titleStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	focusedLabelStyle := lipgloss.NewStyle().
		Foreground(ColorOrange).
		Bold(true)

	hintStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	var rows []string

	if m.step == YouTubeStepAccountAdd {
		rows = append(rows, titleStyle.Render("Add a new YouTube account:"))
	} else {
		rows = append(rows, titleStyle.Render("Edit account details:"))
	}
	rows = append(rows, "")

	// Account name
	if m.accountFormFocus == 0 {
		rows = append(rows, focusedLabelStyle.Render("▶ Account Name:"))
	} else {
		rows = append(rows, labelStyle.Render("  Account Name:"))
	}
	rows = append(rows, "  "+m.accountName.View())
	rows = append(rows, hintStyle.Render("  (A friendly name to identify this account)"))
	rows = append(rows, "")

	// Client ID
	if m.accountFormFocus == 1 {
		rows = append(rows, focusedLabelStyle.Render("▶ Client ID:"))
	} else {
		rows = append(rows, labelStyle.Render("  Client ID:"))
	}
	rows = append(rows, "  "+m.accountClientID.View())
	rows = append(rows, hintStyle.Render("  (ends with .apps.googleusercontent.com)"))
	rows = append(rows, "")

	// Client Secret
	if m.accountFormFocus == 2 {
		rows = append(rows, focusedLabelStyle.Render("▶ Client Secret:"))
	} else {
		rows = append(rows, labelStyle.Render("  Client Secret:"))
	}
	rows = append(rows, "  "+m.accountClientSecret.View())
	rows = append(rows, hintStyle.Render("  (starts with GOCSPX-)"))

	// Error message
	if m.errorMessage != "" {
		errorStyle := lipgloss.NewStyle().
			Foreground(ColorRed).
			Bold(true)
		rows = append(rows, "")
		rows = append(rows, errorStyle.Render("Error: "+m.errorMessage))
	}

	content := lipgloss.JoinVertical(lipgloss.Left, rows...)

	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	helpText := helpStyle.Render("tab: next field • enter: save • esc: cancel")

	footer := RenderHelpFooter(helpText, m.width)

	return LayoutWithHeaderFooter(header, content, footer, m.width, m.height)
}

// renderAccountDelete renders the delete confirmation screen
func (m *YouTubeSetupModel) renderAccountDelete() string {
	header := RenderHeader("YouTube - Delete Account")

	if m.selectedAccountIndex >= len(m.accounts) {
		return LayoutWithHeaderFooter(header, "No account selected", "", m.width, m.height)
	}

	acc := m.accounts[m.selectedAccountIndex]

	warningStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorRed).
		Padding(1, 3).
		Width(60)

	titleStyle := lipgloss.NewStyle().
		Foreground(ColorRed).
		Bold(true)

	labelStyle := lipgloss.NewStyle().
		Foreground(ColorGray)

	valueStyle := lipgloss.NewStyle().
		Foreground(ColorWhite).
		Bold(true)

	displayName := acc.Name
	if displayName == "" {
		displayName = acc.ChannelName
	}
	if displayName == "" {
		displayName = "Unnamed Account"
	}

	warningContent := lipgloss.JoinVertical(lipgloss.Left,
		titleStyle.Render("⚠ Delete Account?"),
		"",
		labelStyle.Render("Account: ")+valueStyle.Render(displayName),
		labelStyle.Render("ID:      ")+valueStyle.Render(acc.ID),
		"",
		labelStyle.Render("This will remove the account and its stored credentials."),
		labelStyle.Render("You will need to re-authenticate if you add it again."),
	)

	content := warningStyle.Render(warningContent)

	buttonRow := lipgloss.JoinHorizontal(lipgloss.Center,
		lipgloss.NewStyle().
			Padding(0, 2).
			Background(ColorRed).
			Foreground(ColorWhite).
			Bold(true).
			Render("Y - Delete"),
		"    ",
		lipgloss.NewStyle().
			Padding(0, 2).
			Background(ColorGray).
			Foreground(ColorWhite).
			Bold(true).
			Render("N - Cancel"),
	)

	helpStyle := lipgloss.NewStyle().
		Foreground(ColorGray).
		Italic(true)

	helpText := helpStyle.Render("y: confirm delete • n/esc: cancel")

	fullContent := lipgloss.JoinVertical(
		lipgloss.Center,
		header,
		"",
		content,
		"",
		buttonRow,
		"",
		helpText,
	)

	return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, fullContent)
}
