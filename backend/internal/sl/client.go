package sl

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

// slTime parses the SL API's timezone-less timestamps as Stockholm local time.
type slTime struct{ time.Time }

var stockholmLoc *time.Location

func init() {
	var err error
	stockholmLoc, err = time.LoadLocation("Europe/Stockholm")
	if err != nil {
		stockholmLoc = time.UTC
	}
}

func (t *slTime) UnmarshalJSON(b []byte) error {
	s := string(b)
	if s == "null" {
		return nil
	}
	if len(s) >= 2 && s[0] == '"' {
		s = s[1 : len(s)-1]
	}
	parsed, err := time.ParseInLocation("2006-01-02T15:04:05", s, stockholmLoc)
	if err != nil {
		parsed, err = time.Parse(time.RFC3339, s)
		if err != nil {
			return err
		}
	}
	t.Time = parsed
	return nil
}

// BagarmossenSiteID is the Trafiklab site ID for Bagarmossen station.
const BagarmossenSiteID = "9141"

const baseURL = "https://transport.integration.sl.se/v1"

type Client struct {
	httpClient *http.Client
}

func NewClient() *Client {
	return &Client{
		httpClient: &http.Client{Timeout: 10 * time.Second},
	}
}

type line struct {
	Designation   string `json:"designation"`
	TransportMode string `json:"transport_mode"`
}

type Departure struct {
	Line          line   `json:"line"`
	Destination   string `json:"destination"`
	ScheduledTime slTime `json:"scheduled"`
	ExpectedTime  slTime `json:"expected"`
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
