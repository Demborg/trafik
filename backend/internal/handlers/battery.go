package handlers

import (
	"encoding/json"
	"log"
	"net/http"
	"strconv"
	"time"

	"cloud.google.com/go/firestore"
	"google.golang.org/api/iterator"
)

type BatteryPoint struct {
	VBat      float64   `json:"v_bat" firestore:"v_bat"`
	PBat      int       `json:"p_bat" firestore:"p_bat"`
	Version   string    `json:"version" firestore:"version"`
	Timestamp time.Time `json:"timestamp" firestore:"timestamp"`
}

func (h *Handler) Battery(w http.ResponseWriter, r *http.Request) {
	if h.firestore == nil {
		http.Error(w, "firestore not initialized", http.StatusInternalServerError)
		return
	}

	days, _ := strconv.Atoi(r.URL.Query().Get("days"))
	if days <= 0 {
		days = 1 // Default to 24h
	}
	since := time.Now().AddDate(0, 0, -days)

	ctx := r.Context()
	// Fetch points since the requested duration
	iter := h.firestore.Collection("battery_telemetry").
		Where("timestamp", ">=", since).
		OrderBy("timestamp", firestore.Asc). // Ascending for easier downsampling
		Documents(ctx)

	var allPoints []BatteryPoint
	for {
		doc, err := iter.Next()
		if err == iterator.Done {
			break
		}
		if err != nil {
			log.Printf("failed to iterate battery telemetry: %v", err)
			http.Error(w, "database error", http.StatusInternalServerError)
			return
		}

		var p BatteryPoint
		if err := doc.DataTo(&p); err != nil {
			log.Printf("failed to parse battery point: %v", err)
			continue
		}
		allPoints = append(allPoints, p)
	}

	// Downsample if we have too many points (e.g., > 300) to keep the frontend snappy
	maxPoints := 300
	var points []BatteryPoint
	if len(allPoints) > maxPoints {
		step := len(allPoints) / maxPoints
		for i := 0; i < len(allPoints); i += step {
			points = append(points, allPoints[i])
		}
		// Always include the latest point
		if len(points) > 0 && points[len(points)-1].Timestamp.Before(allPoints[len(allPoints)-1].Timestamp) {
			points = append(points, allPoints[len(allPoints)-1])
		}
	} else {
		points = allPoints
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(points)
}
