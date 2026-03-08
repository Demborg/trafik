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

func fetchWeather() (string, error) {
	url := fmt.Sprintf(
		"https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code",
		bagarmossenLat, bagarmossenLon,
	)
	resp, err := weatherHTTPClient.Get(url)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	var result struct {
		Current struct {
			Temperature float64 `json:"temperature_2m"`
			WeatherCode int     `json:"weather_code"`
		} `json:"current"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return "", err
	}

	temp := int(math.Round(result.Current.Temperature))
	label := weatherCodeLabel(result.Current.WeatherCode)
	if label != "" {
		return fmt.Sprintf("%d° %s", temp, label), nil
	}
	return fmt.Sprintf("%d°", temp), nil
}

func weatherCodeLabel(code int) string {
	switch {
	case code == 0:
		return "klart"
	case code <= 2:
		return "lätt molnigt"
	case code <= 3:
		return "molnigt"
	case code <= 49:
		return "dimma"
	case code <= 57:
		return "duggregn"
	case code <= 67:
		return "regn"
	case code <= 77:
		return "snö"
	case code <= 82:
		return "regnskurar"
	case code <= 86:
		return "snöbyar"
	case code <= 99:
		return "åska"
	default:
		return ""
	}
}
