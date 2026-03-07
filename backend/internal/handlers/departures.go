package handlers

import (
	"encoding/json"
	"log"
	"net/http"
	"time"

	"github.com/demborg/trafik/backend/internal/sl"
)

type Handler struct {
	sl *sl.Client
}

func New(client *sl.Client) *Handler {
	return &Handler{sl: client}
}

type departureView struct {
	Line          string `json:"line"`
	Destination   string `json:"destination"`
	Minutes       int    `json:"minutes"`
	TransportType string `json:"transport_type"`
}

type departuresResponse struct {
	Departures           []departureView `json:"departures"`
	SuggestedSleepSeconds int            `json:"suggested_sleep_seconds"`
}

func (h *Handler) Departures(w http.ResponseWriter, r *http.Request) {
	departures, err := h.sl.GetDepartures(r.Context(), sl.BagarmossenSiteID)
	if err != nil {
		log.Printf("error fetching departures: %v", err)
		http.Error(w, "upstream error", http.StatusBadGateway)
		return
	}

	now := time.Now()
	views := make([]departureView, 0, len(departures))
	for _, d := range departures {
		minutes := int(d.ExpectedTime.Sub(now).Minutes())
		if minutes < 0 {
			continue
		}
		views = append(views, departureView{
			Line:          d.Line,
			Destination:   d.Destination,
			Minutes:       minutes,
			TransportType: d.TransportType,
		})
	}

	resp := departuresResponse{
		Departures:           views,
		SuggestedSleepSeconds: suggestedSleep(views),
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

// suggestedSleep returns a recommended deep-sleep duration in seconds.
// The device wakes a couple of minutes before the next departure so the
// display is always fresh when it matters.
func suggestedSleep(departures []departureView) int {
	const (
		minSleep    = 30
		maxSleep    = 300
		wakeAheadMin = 2
	)
	if len(departures) == 0 {
		return maxSleep
	}
	sleep := (departures[0].Minutes - wakeAheadMin) * 60
	if sleep < minSleep {
		return minSleep
	}
	if sleep > maxSleep {
		return maxSleep
	}
	return sleep
}
