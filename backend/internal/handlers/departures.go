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
	Destination string `json:"destination"`
	Minutes     []int  `json:"minutes"`
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
		SuggestedSleepSeconds: suggestedSleep(groups),
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

// buildGroups groups departures by transport type → line → destination,
// preserving first-seen order so earliest departures come first.
func buildGroups(departures []sl.Departure, now time.Time) []groupView {
	// Use index maps to preserve insertion order via parallel slices.
	var groups []groupView
	groupIdx := map[string]int{}

	for _, d := range departures {
		minutes := int(d.ExpectedTime.Sub(now).Minutes())
		if minutes < 0 {
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

		groups[gi].Lines[li].Destinations[di].Minutes = append(
			groups[gi].Lines[li].Destinations[di].Minutes, minutes,
		)
	}

	return groups
}

// suggestedSleep returns a recommended deep-sleep duration in seconds,
// waking a couple of minutes before the next departure.
func suggestedSleep(groups []groupView) int {
	const (
		minSleep    = 30
		maxSleep    = 300
		wakeAheadMin = 2
	)
	// Find the earliest departure across all groups.
	first := -1
	for _, g := range groups {
		for _, l := range g.Lines {
			for _, dest := range l.Destinations {
				if len(dest.Minutes) > 0 {
					if first < 0 || dest.Minutes[0] < first {
						first = dest.Minutes[0]
					}
				}
			}
		}
	}
	if first < 0 {
		return maxSleep
	}
	sleep := (first - wakeAheadMin) * 60
	if sleep < minSleep {
		return minSleep
	}
	if sleep > maxSleep {
		return maxSleep
	}
	return sleep
}
