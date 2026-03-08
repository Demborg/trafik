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

type destinationView struct {
	Destination string  `json:"destination"`
	Departures  []int64 `json:"departures"` // unix timestamps
}

type lineView struct {
	Line         string            `json:"line"`
	Destinations []destinationView `json:"destinations"`
}

type groupView struct {
	Type  string     `json:"type"`
	Lines []lineView `json:"lines"`
}

type departuresResponse struct {
	Groups                []groupView `json:"groups"`
	SuggestedSleepSeconds int         `json:"suggested_sleep_seconds"`
	ServerTime            int64       `json:"server_time"` // unix timestamp for client clock sync
}

func (h *Handler) Departures(w http.ResponseWriter, r *http.Request) {
	departures, err := h.sl.GetDepartures(r.Context(), sl.BagarmossenSiteID)
	if err != nil {
		log.Printf("error fetching departures: %v", err)
		http.Error(w, "upstream error", http.StatusBadGateway)
		return
	}

	now := time.Now()
	groups := buildGroups(departures, now)

	resp := departuresResponse{
		Groups:                groups,
		SuggestedSleepSeconds: suggestedSleep(groups, now),
		ServerTime:            now.Unix(),
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

// buildGroups groups departures by transport type → line → destination,
// preserving first-seen order so earliest departures come first.
func buildGroups(departures []sl.Departure, now time.Time) []groupView {
	var groups []groupView
	groupIdx := map[string]int{}

	for _, d := range departures {
		if d.ExpectedTime.Before(now) {
			continue
		}

		tp := d.Line.TransportMode

		gi, ok := groupIdx[tp]
		if !ok {
			gi = len(groups)
			groupIdx[tp] = gi
			groups = append(groups, groupView{Type: tp})
		}

		lineIdx := map[string]int{}
		for i, l := range groups[gi].Lines {
			lineIdx[l.Line] = i
		}

		li, ok := lineIdx[d.Line.Designation]
		if !ok {
			li = len(groups[gi].Lines)
			groups[gi].Lines = append(groups[gi].Lines, lineView{Line: d.Line.Designation})
		}

		destIdx := map[string]int{}
		for i, dest := range groups[gi].Lines[li].Destinations {
			destIdx[dest.Destination] = i
		}

		di, ok := destIdx[d.Destination]
		if !ok {
			di = len(groups[gi].Lines[li].Destinations)
			groups[gi].Lines[li].Destinations = append(
				groups[gi].Lines[li].Destinations,
				destinationView{Destination: d.Destination},
			)
		}

		groups[gi].Lines[li].Destinations[di].Departures = append(
			groups[gi].Lines[li].Destinations[di].Departures, d.ExpectedTime.Unix(),
		)
	}

	return groups
}

// suggestedSleep returns a recommended deep-sleep duration in seconds,
// waking a couple of minutes before the next departure.
func suggestedSleep(groups []groupView, now time.Time) int {
	const (
		minSleep    = 30
		maxSleep    = 600
		wakeAheadSec = 2 * 60
	)
	nowUnix := now.Unix()
	first := int64(-1)
	for _, g := range groups {
		for _, l := range g.Lines {
			for _, dest := range l.Destinations {
				if len(dest.Departures) > 0 {
					if first < 0 || dest.Departures[0] < first {
						first = dest.Departures[0]
					}
				}
			}
		}
	}
	if first < 0 {
		return maxSleep
	}
	sleep := int(first - nowUnix - wakeAheadSec)
	if sleep < minSleep {
		return minSleep
	}
	if sleep > maxSleep {
		return maxSleep
	}
	return sleep
}
