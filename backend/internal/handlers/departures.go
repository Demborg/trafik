package handlers

import (
	"context"
	"encoding/json"
	"log"
	"net/http"
	"sort"
	"strconv"
	"strings"
	"time"

	"cloud.google.com/go/firestore"
	"github.com/demborg/trafik/backend/internal/sl"
)

type Handler struct {
	sl        *sl.Client
	firestore *firestore.Client
}

func New(client *sl.Client, fs *firestore.Client) *Handler {
	return &Handler{
		sl:        client,
		firestore: fs,
	}
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
	Label string     `json:"label"`
	Lines []lineView `json:"lines"`
}

type departuresResponse struct {
	Groups                []groupView `json:"groups"`
	Weather               string      `json:"weather,omitempty"`
	SuggestedSleepSeconds int         `json:"suggested_sleep_seconds"`
	ServerTime            int64       `json:"server_time"` // unix timestamp for client clock sync
}

type departuresResult struct {
	departures []sl.Departure
	err        error
}

var typeOrder = map[string]int{
	"METRO": 0,
	"TRAIN": 1,
	"TRAM":  2,
	"BUS":   3,
	"FERRY": 4,
}

var typeLabels = map[string]string{
	"METRO": "Tunnelbana",
	"TRAIN": "Pendeltåg",
	"TRAM":  "Spårvagn",
	"BUS":   "Buss",
	"FERRY": "Färja",
}

func typeLabel(tp string) string {
	if label, ok := typeLabels[strings.ToUpper(tp)]; ok {
		return label
	}
	if len(tp) > 0 {
		return strings.ToUpper(tp[:1]) + strings.ToLower(tp[1:])
	}
	return tp
}

func (h *Handler) Departures(w http.ResponseWriter, r *http.Request) {
	vBat, _ := strconv.ParseFloat(r.URL.Query().Get("v_bat"), 64)
	pBat, _ := strconv.Atoi(r.URL.Query().Get("p_bat"))
	version := r.URL.Query().Get("version")

	telemetryDone := h.logTelemetryAsync(vBat, pBat, version)
	departuresCh := h.fetchDeparturesAsync(r.Context())
	weatherCh := h.fetchWeatherAsync(r.Context())

	dr := <-departuresCh
	weather := <-weatherCh
	<-telemetryDone

	if dr.err != nil {
		log.Printf("error fetching departures: %v", dr.err)
		http.Error(w, "upstream error", http.StatusBadGateway)
		return
	}

	now := time.Now()
	groups := buildGroups(dr.departures, now)

	resp := departuresResponse{
		Groups:                groups,
		Weather:               weather,
		SuggestedSleepSeconds: suggestedSleep(groups, now),
		ServerTime:            now.Unix(),
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

func (h *Handler) logTelemetryAsync(vBat float64, pBat int, version string) <-chan struct{} {
	done := make(chan struct{})
	go func() {
		defer close(done)
		if (vBat == 0 && pBat == 0) || h.firestore == nil {
			return
		}

		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()

		_, _, err := h.firestore.Collection("battery_telemetry").Add(ctx, map[string]interface{}{
			"v_bat":     vBat,
			"p_bat":     pBat,
			"version":   version,
			"timestamp": firestore.ServerTimestamp,
		})
		if err != nil {
			log.Printf("failed to log battery telemetry to firestore: %v", err)
		}
	}()
	return done
}

func (h *Handler) fetchDeparturesAsync(ctx context.Context) <-chan departuresResult {
	ch := make(chan departuresResult, 1)
	go func() {
		d, err := h.sl.GetDepartures(ctx, sl.BagarmossenSiteID)
		ch <- departuresResult{d, err}
	}()
	return ch
}

func (h *Handler) fetchWeatherAsync(ctx context.Context) <-chan string {
	ch := make(chan string, 1)
	go func() {
		w, err := fetchWeather()
		if err != nil {
			log.Printf("weather fetch error: %v", err)
			w = ""
		}
		ch <- w
	}()
	return ch
}

// buildGroups groups departures by transport type → line → destination,
// sorted by type priority (Tunnelbana first).
func buildGroups(departures []sl.Departure, now time.Time) []groupView {
	var typeOrder_ []string
	groupData := map[string]*groupView{}

	for _, d := range departures {
		if d.ExpectedTime.Before(now) {
			continue
		}

		tp := d.Line.TransportMode
		if _, ok := groupData[tp]; !ok {
			typeOrder_ = append(typeOrder_, tp)
			gv := &groupView{Label: typeLabel(tp)}
			groupData[tp] = gv
		}
		g := groupData[tp]

		li := -1
		for i, l := range g.Lines {
			if l.Line == d.Line.Designation {
				li = i
				break
			}
		}
		if li < 0 {
			li = len(g.Lines)
			g.Lines = append(g.Lines, lineView{Line: d.Line.Designation})
		}

		di := -1
		for i, dest := range g.Lines[li].Destinations {
			if dest.Destination == d.Destination {
				di = i
				break
			}
		}
		if di < 0 {
			di = len(g.Lines[li].Destinations)
			g.Lines[li].Destinations = append(
				g.Lines[li].Destinations,
				destinationView{Destination: d.Destination},
			)
		}

		g.Lines[li].Destinations[di].Departures = append(
			g.Lines[li].Destinations[di].Departures, d.ExpectedTime.Unix(),
		)
	}

	for _, g := range groupData {
		for i := range g.Lines {
			for j := range g.Lines[i].Destinations {
				deps := g.Lines[i].Destinations[j].Departures
				if len(deps) > 3 {
					g.Lines[i].Destinations[j].Departures = deps[:3]
				}
			}
		}
	}

	sort.SliceStable(typeOrder_, func(i, j int) bool {
		pi := typeOrder[strings.ToUpper(typeOrder_[i])]
		pj := typeOrder[strings.ToUpper(typeOrder_[j])]
		return pi < pj
	})

	groups := make([]groupView, 0, len(typeOrder_))
	for _, tp := range typeOrder_ {
		groups = append(groups, *groupData[tp])
	}
	return groups
}

// suggestedSleep returns a recommended deep-sleep duration in seconds,
// waking a couple of minutes before the next departure.
func suggestedSleep(groups []groupView, now time.Time) int {
	const (
		minSleep     = 60
		maxSleep     = 600
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
