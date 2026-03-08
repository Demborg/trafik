package handlers

import (
	"encoding/json"
	"fmt"
	"math"
	"net/http"
	"time"
)

const (
	bagarmossenLat = 59.2753
	bagarmossenLon = 18.1328
)

var weatherHTTPClient = &http.Client{Timeout: 5 * time.Second}

// fetchWeather fetches current conditions from SMHI's point forecast API.
// SMHI is the Swedish meteorological authority and provides high-resolution
// forecasts for Sweden.
func fetchWeather() (string, error) {
	url := fmt.Sprintf(
		"https://opendata-download-metfcst.smhi.se/api/category/pmp3g/version/2/geotype/point/lon/%.4f/lat/%.4f/data.json",
		bagarmossenLon, bagarmossenLat,
	)
	resp, err := weatherHTTPClient.Get(url)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	var result struct {
		TimeSeries []struct {
			Parameters []struct {
				Name   string    `json:"name"`
				Values []float64 `json:"values"`
			} `json:"parameters"`
		} `json:"timeSeries"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return "", err
	}
	if len(result.TimeSeries) == 0 {
		return "", fmt.Errorf("empty SMHI response")
	}

	// First entry is the nearest forecast hour.
	var temp float64
	var symbol int
	for _, p := range result.TimeSeries[0].Parameters {
		if p.Name == "t" && len(p.Values) > 0 {
			temp = p.Values[0]
		}
		if p.Name == "Wsymb2" && len(p.Values) > 0 {
			symbol = int(p.Values[0])
		}
	}

	label := smhiSymbolLabel(symbol)
	if label != "" {
		return fmt.Sprintf("%d° %s", int(math.Round(temp)), label), nil
	}
	return fmt.Sprintf("%d°", int(math.Round(temp))), nil
}

// smhiSymbolLabel maps SMHI Wsymb2 weather symbols to Swedish descriptions.
// https://opendata.smhi.se/apidocs/metfcst/parameters.html
func smhiSymbolLabel(symbol int) string {
	switch symbol {
	case 1:
		return "klart"
	case 2:
		return "lätt molnigt"
	case 3:
		return "halvklart"
	case 4:
		return "halvmulnet"
	case 5:
		return "molnigt"
	case 6:
		return "mulet"
	case 7:
		return "dimma"
	case 8, 9, 10:
		return "regnskurar"
	case 11:
		return "åskskurar"
	case 12, 13, 14:
		return "snöblandad regnskur"
	case 15, 16, 17:
		return "snöbyar"
	case 18, 19, 20:
		return "regn"
	case 21:
		return "åska"
	case 22, 23, 24:
		return "snöblandat regn"
	case 25, 26, 27:
		return "snöfall"
	default:
		return ""
	}
}
