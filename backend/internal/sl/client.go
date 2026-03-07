package sl

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

// BagarmossenSiteID is the Trafiklab site ID for Bagarmossen station.
// TODO: verify against https://transport.integration.sl.se/v1/sites?expand=true
const BagarmossenSiteID = "9192"

const baseURL = "https://transport.integration.sl.se/v1"

type Client struct {
	apiKey     string
	httpClient *http.Client
}

func NewClient(apiKey string) *Client {
	return &Client{
		apiKey:     apiKey,
		httpClient: &http.Client{Timeout: 10 * time.Second},
	}
}

type Departure struct {
	Line          string    `json:"line"`
	Destination   string    `json:"destination"`
	ScheduledTime time.Time `json:"scheduled"`
	ExpectedTime  time.Time `json:"expected"`
	TransportType string    `json:"transport_type"`
}

// GetDepartures fetches live departures for the given site ID from the SL Transport API.
// API docs: https://www.trafiklab.se/api/trafiklab-apis/sl/transport/
// TODO: map actual response fields once API response shape is confirmed.
func (c *Client) GetDepartures(ctx context.Context, siteID string) ([]Departure, error) {
	url := fmt.Sprintf("%s/sites/%s/departures", baseURL, siteID)

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Authorization", "Bearer "+c.apiKey)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("SL API returned %d", resp.StatusCode)
	}

	var result struct {
		Departures []Departure `json:"departures"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return nil, err
	}

	return result.Departures, nil
}
