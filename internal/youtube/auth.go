package youtube

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"net/url"
	"os/exec"
	"runtime"
	"strings"
	"time"

	"golang.org/x/oauth2"
	"golang.org/x/oauth2/google"
	"google.golang.org/api/option"
	"google.golang.org/api/youtube/v3"
)

// OAuth2 scopes required for YouTube upload
var oauthScopes = []string{
	"https://www.googleapis.com/auth/youtube.upload",
	"https://www.googleapis.com/auth/youtube", // For playlist management
}

// Auth handles YouTube OAuth2 authentication
type Auth struct {
	config    *oauth2.Config
	configDir string
	accountID string // Account ID for multi-account support
	token     *oauth2.Token
}

// NewAuth creates a new YouTube authenticator (legacy, uses default account)
func NewAuth(clientID, clientSecret, configDir string) *Auth {
	return NewAuthForAccount(clientID, clientSecret, configDir, "legacy")
}

// NewAuthForAccount creates a new YouTube authenticator for a specific account
func NewAuthForAccount(clientID, clientSecret, configDir, accountID string) *Auth {
	return &Auth{
		config: &oauth2.Config{
			ClientID:     clientID,
			ClientSecret: clientSecret,
			Scopes:       oauthScopes,
			Endpoint:     google.Endpoint,
			// RedirectURL will be set dynamically when starting auth
		},
		configDir: configDir,
		accountID: accountID,
	}
}

// IsAuthenticated returns true if we have valid tokens
func (a *Auth) IsAuthenticated() bool {
	if a.token != nil && a.token.Valid() {
		return true
	}

	// Try to load token from disk
	token, err := a.loadToken()
	if err != nil {
		return false
	}

	a.token = token
	return token.Valid() || token.RefreshToken != ""
}

// GetChannelName returns the authenticated channel name
func (a *Auth) GetChannelName(ctx context.Context) (string, error) {
	client, err := a.GetClient(ctx)
	if err != nil {
		return "", err
	}

	service, err := youtube.NewService(ctx, option.WithHTTPClient(client))
	if err != nil {
		return "", err
	}

	call := service.Channels.List([]string{"snippet"}).Mine(true)
	response, err := call.Do()
	if err != nil {
		return "", err
	}

	if len(response.Items) == 0 {
		return "", fmt.Errorf("no channel found")
	}

	return response.Items[0].Snippet.Title, nil
}

// GetChannelID returns the authenticated channel ID
func (a *Auth) GetChannelID(ctx context.Context) (string, error) {
	client, err := a.GetClient(ctx)
	if err != nil {
		return "", err
	}

	service, err := youtube.NewService(ctx, option.WithHTTPClient(client))
	if err != nil {
		return "", err
	}

	call := service.Channels.List([]string{"id"}).Mine(true)
	response, err := call.Do()
	if err != nil {
		return "", err
	}

	if len(response.Items) == 0 {
		return "", fmt.Errorf("no channel found")
	}

	return response.Items[0].Id, nil
}

// Channel represents a YouTube channel that the user can manage
type Channel struct {
	ID          string
	Title       string
	Description string
	Thumbnail   string
	IsBrand     bool // True if this is a brand account channel
}

// GetManagedChannels returns all channels the user can manage (including brand accounts)
// This should be called after authentication to let the user choose which channel to use
func (a *Auth) GetManagedChannels(ctx context.Context) ([]Channel, error) {
	client, err := a.GetClient(ctx)
	if err != nil {
		return nil, err
	}

	service, err := youtube.NewService(ctx, option.WithHTTPClient(client))
	if err != nil {
		return nil, err
	}

	var channels []Channel

	// First, get the user's own channel
	ownCall := service.Channels.List([]string{"snippet", "brandingSettings"}).Mine(true)
	ownResponse, err := ownCall.Do()
	if err != nil {
		return nil, fmt.Errorf("failed to get own channel: %w", err)
	}

	for _, item := range ownResponse.Items {
		ch := Channel{
			ID:          item.Id,
			Title:       item.Snippet.Title,
			Description: item.Snippet.Description,
			IsBrand:     false,
		}
		if item.Snippet.Thumbnails != nil && item.Snippet.Thumbnails.Default != nil {
			ch.Thumbnail = item.Snippet.Thumbnails.Default.Url
		}
		channels = append(channels, ch)
	}

	// Then, get channels managed by the user (brand accounts)
	// Note: This requires the user to have granted access to these channels
	managedCall := service.Channels.List([]string{"snippet", "brandingSettings"}).ManagedByMe(true)
	managedResponse, err := managedCall.Do()
	if err != nil {
		// Some accounts might not have access to managedByMe, that's OK
		// Just return the own channel
		if len(channels) > 0 {
			return channels, nil
		}
		return nil, fmt.Errorf("failed to get managed channels: %w", err)
	}

	for _, item := range managedResponse.Items {
		// Check if we already have this channel (avoid duplicates)
		alreadyAdded := false
		for _, existing := range channels {
			if existing.ID == item.Id {
				alreadyAdded = true
				break
			}
		}
		if alreadyAdded {
			continue
		}

		ch := Channel{
			ID:          item.Id,
			Title:       item.Snippet.Title,
			Description: item.Snippet.Description,
			IsBrand:     true,
		}
		if item.Snippet.Thumbnails != nil && item.Snippet.Thumbnails.Default != nil {
			ch.Thumbnail = item.Snippet.Thumbnails.Default.Url
		}
		channels = append(channels, ch)
	}

	return channels, nil
}

// AuthenticateWithCallback starts the OAuth2 flow and calls the callback with the auth URL
// This allows the UI to display the URL while authentication proceeds
func (a *Auth) AuthenticateWithCallback(ctx context.Context, onURL func(string)) error {
	return a.authenticateInternal(ctx, onURL)
}

// Authenticate starts the OAuth2 flow and returns when complete
// This opens a browser for user consent and waits for the callback
func (a *Auth) Authenticate(ctx context.Context) error {
	return a.authenticateInternal(ctx, nil)
}

// authenticateInternal is the internal implementation of authentication
func (a *Auth) authenticateInternal(ctx context.Context, onURL func(string)) error {
	// Find an available port for the callback server
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return fmt.Errorf("failed to start callback server: %w", err)
	}
	defer func() { _ = listener.Close() }()

	port := listener.Addr().(*net.TCPAddr).Port
	redirectURL := fmt.Sprintf("http://127.0.0.1:%d/callback", port)
	a.config.RedirectURL = redirectURL

	// Generate PKCE code verifier and challenge
	codeVerifier, err := generateCodeVerifier()
	if err != nil {
		return fmt.Errorf("failed to generate code verifier: %w", err)
	}
	codeChallenge := generateCodeChallenge(codeVerifier)

	// Generate state for CSRF protection
	state, err := generateState()
	if err != nil {
		return fmt.Errorf("failed to generate state: %w", err)
	}

	// Build authorization URL with PKCE
	// Note: We explicitly set response_type=code even though AuthCodeURL should add it,
	// because some versions may have issues with parameter ordering
	authURL := a.config.AuthCodeURL(state,
		oauth2.AccessTypeOffline,
		oauth2.SetAuthURLParam("response_type", "code"),
		oauth2.SetAuthURLParam("code_challenge", codeChallenge),
		oauth2.SetAuthURLParam("code_challenge_method", "S256"),
	)

	// Debug: Log the auth URL for troubleshooting
	fmt.Printf("DEBUG: Generated auth URL: %s\n", authURL)

	// Call the URL callback if provided (for UI to display)
	if onURL != nil {
		onURL(authURL)
	}

	// Channel to receive the authorization code
	codeChan := make(chan string, 1)
	errChan := make(chan error, 1)

	// Start HTTP server to handle callback
	server := &http.Server{
		Handler: http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			if r.URL.Path != "/callback" {
				http.NotFound(w, r)
				return
			}

			// Verify state
			if r.URL.Query().Get("state") != state {
				errChan <- fmt.Errorf("invalid state parameter")
				http.Error(w, "Invalid state", http.StatusBadRequest)
				return
			}

			// Check for error
			if errParam := r.URL.Query().Get("error"); errParam != "" {
				errDesc := r.URL.Query().Get("error_description")
				errChan <- fmt.Errorf("authorization error: %s - %s", errParam, errDesc)
				_, _ = fmt.Fprintf(w, `<!DOCTYPE html>
<html><head><title>Authorization Failed</title>
<style>body{font-family:sans-serif;text-align:center;padding:50px;}</style>
</head><body>
<h1>Authorization Failed</h1>
<p>%s</p>
<p>You can close this window.</p>
</body></html>`, errDesc)
				return
			}

			// Get authorization code
			code := r.URL.Query().Get("code")
			if code == "" {
				errChan <- fmt.Errorf("no authorization code received")
				http.Error(w, "No code", http.StatusBadRequest)
				return
			}

			codeChan <- code

			// Show success page
			_, _ = fmt.Fprint(w, `<!DOCTYPE html>
<html><head><title>Authorization Successful</title>
<style>
body{font-family:sans-serif;text-align:center;padding:50px;background:#1a1a2e;color:#fff;}
.success{color:#4ade80;font-size:48px;}
h1{color:#f97316;}
</style>
</head><body>
<div class="success">✓</div>
<h1>Authorization Successful!</h1>
<p>You can close this window and return to Kartoza Video Processor.</p>
</body></html>`)
		}),
	}

	go func() {
		if err := server.Serve(listener); err != http.ErrServerClosed {
			errChan <- err
		}
	}()

	// Open browser to authorization URL
	if err := openBrowser(authURL); err != nil {
		return fmt.Errorf("failed to open browser: %w (please manually visit: %s)", err, authURL)
	}

	// Wait for authorization code or error
	var code string
	select {
	case code = <-codeChan:
		// Success
	case err := <-errChan:
		return err
	case <-ctx.Done():
		return ctx.Err()
	case <-time.After(5 * time.Minute):
		return fmt.Errorf("authorization timeout")
	}

	// Shutdown server
	shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	_ = server.Shutdown(shutdownCtx)

	// Exchange code for token with PKCE verifier
	token, err := a.config.Exchange(ctx, code,
		oauth2.SetAuthURLParam("code_verifier", codeVerifier),
	)
	if err != nil {
		return fmt.Errorf("failed to exchange code for token: %w", err)
	}

	a.token = token

	// Save token to disk
	if err := a.saveToken(token); err != nil {
		return fmt.Errorf("failed to save token: %w", err)
	}

	return nil
}

// GetClient returns an HTTP client with valid OAuth2 credentials
func (a *Auth) GetClient(ctx context.Context) (*http.Client, error) {
	if a.token == nil {
		token, err := a.loadToken()
		if err != nil {
			return nil, fmt.Errorf("not authenticated: %w", err)
		}
		a.token = token
	}

	// Create token source that auto-refreshes
	tokenSource := a.config.TokenSource(ctx, a.token)

	// Get potentially refreshed token
	newToken, err := tokenSource.Token()
	if err != nil {
		return nil, fmt.Errorf("failed to get valid token: %w", err)
	}

	// Save if token was refreshed
	if newToken.AccessToken != a.token.AccessToken {
		a.token = newToken
		if err := a.saveToken(newToken); err != nil {
			// Log but don't fail
			fmt.Printf("Warning: failed to save refreshed token: %v\n", err)
		}
	}

	return oauth2.NewClient(ctx, tokenSource), nil
}

// Logout removes stored credentials
func (a *Auth) Logout() error {
	a.token = nil
	return DeleteTokenForAccount(a.configDir, a.accountID)
}

// loadToken loads the OAuth token from disk and converts to oauth2.Token
func (a *Auth) loadToken() (*oauth2.Token, error) {
	storedToken, err := LoadTokenForAccount(a.configDir, a.accountID)
	if err != nil {
		return nil, err
	}

	expiry, _ := time.Parse(time.RFC3339, storedToken.Expiry)

	return &oauth2.Token{
		AccessToken:  storedToken.AccessToken,
		RefreshToken: storedToken.RefreshToken,
		TokenType:    storedToken.TokenType,
		Expiry:       expiry,
	}, nil
}

// saveToken saves the OAuth token to disk
func (a *Auth) saveToken(token *oauth2.Token) error {
	storedToken := &Token{
		AccessToken:  token.AccessToken,
		RefreshToken: token.RefreshToken,
		TokenType:    token.TokenType,
		Expiry:       token.Expiry.Format(time.RFC3339),
	}
	return SaveTokenForAccount(a.configDir, a.accountID, storedToken)
}

// generateCodeVerifier generates a random code verifier for PKCE
func generateCodeVerifier() (string, error) {
	b := make([]byte, 32)
	if _, err := rand.Read(b); err != nil {
		return "", err
	}
	return base64.RawURLEncoding.EncodeToString(b), nil
}

// generateCodeChallenge generates a code challenge from the verifier
func generateCodeChallenge(verifier string) string {
	h := sha256.Sum256([]byte(verifier))
	return base64.RawURLEncoding.EncodeToString(h[:])
}

// generateState generates a random state string for CSRF protection
func generateState() (string, error) {
	b := make([]byte, 16)
	if _, err := rand.Read(b); err != nil {
		return "", err
	}
	return base64.RawURLEncoding.EncodeToString(b), nil
}

// openBrowser opens the default browser to the given URL
func openBrowser(urlStr string) error {
	var cmd *exec.Cmd

	// Debug: Print the URL being opened
	fmt.Printf("DEBUG: Opening browser with URL:\n%s\n", urlStr)

	switch runtime.GOOS {
	case "linux":
		// Try xdg-open first, fall back to common browsers
		cmd = exec.Command("xdg-open", urlStr)
	case "darwin":
		cmd = exec.Command("open", urlStr)
	case "windows":
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", urlStr)
	default:
		return fmt.Errorf("unsupported platform")
	}

	return cmd.Start()
}

// ValidateCredentials checks if the provided credentials are valid by making a test API call
func ValidateCredentials(ctx context.Context, clientID, clientSecret string) error {
	// We can't fully validate without going through OAuth, but we can check format
	if clientID == "" || clientSecret == "" {
		return fmt.Errorf("client ID and secret are required")
	}

	// Check if client ID looks valid (ends with .apps.googleusercontent.com)
	if !strings.HasSuffix(clientID, ".apps.googleusercontent.com") {
		return fmt.Errorf("client ID should end with .apps.googleusercontent.com")
	}

	return nil
}

// GetSetupInstructions returns instructions for setting up YouTube API credentials
func GetSetupInstructions() string {
	return `To upload videos to YouTube, you need to create OAuth credentials:

1. Go to the Google Cloud Console:
   https://console.cloud.google.com/

2. Create a new project or select an existing one

3. Enable the YouTube Data API v3:
   - Go to "APIs & Services" > "Library"
   - Search for "YouTube Data API v3"
   - Click "Enable"

4. Create OAuth credentials:
   - Go to "APIs & Services" > "Credentials"
   - Click "Create Credentials" > "OAuth client ID"
   - Select "Desktop app" as application type
   - Give it a name (e.g., "Kartoza Video Processor")
   - Click "Create"

5. Copy the Client ID and Client Secret

6. Configure OAuth consent screen:
   - Go to "APIs & Services" > "OAuth consent screen"
   - Choose "External" user type
   - Fill in the required fields
   - Add your email to test users (while in testing mode)

Note: While your app is in "Testing" mode, only users you
explicitly add as test users can authenticate.`
}

// AuthStatus represents the current authentication status
type AuthStatus int

const (
	AuthStatusNotConfigured AuthStatus = iota // No credentials configured
	AuthStatusConfigured                      // Credentials set but not authenticated
	AuthStatusAuthenticated                   // Fully authenticated with valid token
	AuthStatusExpired                         // Token expired, needs refresh
)

// GetAuthStatus returns the current authentication status (legacy, checks default account)
func GetAuthStatus(cfg *Config, configDir string) AuthStatus {
	return GetAuthStatusForAccount(cfg, configDir, "legacy")
}

// GetAuthStatusForAccount returns the authentication status for a specific account
func GetAuthStatusForAccount(cfg *Config, configDir, accountID string) AuthStatus {
	var account *Account

	if accountID == "legacy" {
		// Check legacy config
		if cfg.ClientID == "" || cfg.ClientSecret == "" {
			return AuthStatusNotConfigured
		}
	} else {
		account = cfg.GetAccount(accountID)
		if account == nil || !account.IsConfigured() {
			return AuthStatusNotConfigured
		}
	}

	if !HasTokenForAccount(configDir, accountID) {
		return AuthStatusConfigured
	}

	token, err := LoadTokenForAccount(configDir, accountID)
	if err != nil {
		return AuthStatusConfigured
	}

	expiry, err := time.Parse(time.RFC3339, token.Expiry)
	if err != nil {
		return AuthStatusConfigured
	}

	if time.Now().After(expiry) && token.RefreshToken == "" {
		return AuthStatusExpired
	}

	return AuthStatusAuthenticated
}

// IsAccountAuthenticated checks if a specific account is authenticated
func IsAccountAuthenticated(cfg *Config, configDir, accountID string) bool {
	return GetAuthStatusForAccount(cfg, configDir, accountID) == AuthStatusAuthenticated
}

// AuthStatusString returns a human-readable status string
func AuthStatusString(status AuthStatus) string {
	switch status {
	case AuthStatusNotConfigured:
		return "Not Configured"
	case AuthStatusConfigured:
		return "Not Connected"
	case AuthStatusAuthenticated:
		return "Connected"
	case AuthStatusExpired:
		return "Expired"
	default:
		return "Unknown"
	}
}

// TestConnection tests if the current credentials work
func (a *Auth) TestConnection(ctx context.Context) error {
	client, err := a.GetClient(ctx)
	if err != nil {
		return err
	}

	service, err := youtube.NewService(ctx, option.WithHTTPClient(client))
	if err != nil {
		return err
	}

	// Try to get channel info
	_, err = service.Channels.List([]string{"id"}).Mine(true).Do()
	return err
}

// RevokeToken revokes the current access token
func (a *Auth) RevokeToken(ctx context.Context) error {
	if a.token == nil {
		token, err := a.loadToken()
		if err != nil {
			return nil // No token to revoke
		}
		a.token = token
	}

	// Revoke the token
	revokeURL := fmt.Sprintf("https://oauth2.googleapis.com/revoke?token=%s",
		url.QueryEscape(a.token.AccessToken))

	resp, err := http.Post(revokeURL, "application/x-www-form-urlencoded", nil)
	if err != nil {
		return err
	}
	defer func() { _ = resp.Body.Close() }()

	if resp.StatusCode != http.StatusOK {
		var result map[string]interface{}
		_ = json.NewDecoder(resp.Body).Decode(&result)
		return fmt.Errorf("revoke failed: %v", result)
	}

	// Delete local token
	return a.Logout()
}
