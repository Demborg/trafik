#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <sys/time.h>

#include "config.h"

// RTC memory survives deep sleep — used to track cycle count and cache data.
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR char cachedPayload[512];

// --- Display ---
// TODO: initialise GxEPD2 display once pin mapping is confirmed on hardware.
// Partial refresh support needs to be validated on the specific panel revision.
void updateDisplay(const char* json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    time_t now = time(NULL);

    // For each departure timestamp compute minutes remaining; skip past ones.
    // TODO: render to GxEPD2 display.
    JsonArray groups = doc["groups"].as<JsonArray>();
    for (JsonObject group : groups) {
        for (JsonObject line : group["lines"].as<JsonArray>()) {
            for (JsonObject dest : line["destinations"].as<JsonArray>()) {
                for (JsonVariant ts : dest["departures"].as<JsonArray>()) {
                    int minutes = (int)((ts.as<int64_t>() - (int64_t)now) / 60);
                    if (minutes >= 0) {
                        // "Nu" when minutes == 0, otherwise "<N> min".
                        // TODO: add to display buffer.
                        (void)minutes;
                    }
                }
            }
        }
    }
}

// --- Network ---
bool connectWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500);
    }
    return WiFi.status() == WL_CONNECTED;
}

struct PollResult {
    bool    success;
    int     suggestedSleepSeconds;
};

PollResult pollBackend() {
    HTTPClient http;
    http.begin(BACKEND_URL);
    int code = http.GET();

    if (code != HTTP_CODE_OK) {
        http.end();
        return {false, POLL_FALLBACK_SLEEP_SECONDS};
    }

    String body = http.getString();
    http.end();

    body.toCharArray(cachedPayload, sizeof(cachedPayload));

    JsonDocument doc;
    deserializeJson(doc, body);

    // Sync RTC clock from server_time.
    int64_t serverTime = doc["server_time"].as<int64_t>();
    if (serverTime > 0) {
        struct timeval tv = { .tv_sec = (time_t)serverTime, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
    }

    int sleep = doc["suggested_sleep_seconds"] | POLL_FALLBACK_SLEEP_SECONDS;
    return {true, sleep};
}

// --- Main ---
void setup() {
    Serial.begin(115200);
    bootCount++;

    bool isNetworkCycle = (bootCount % NETWORK_POLL_EVERY_N_CYCLES == 0)
                       || (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED);

    int sleepSeconds = DISPLAY_REFRESH_SLEEP_SECONDS;

    if (isNetworkCycle) {
        if (connectWiFi()) {
            PollResult result = pollBackend();
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            if (result.success) {
                sleepSeconds = result.suggestedSleepSeconds;
            } else {
                sleepSeconds = POLL_FALLBACK_SLEEP_SECONDS;
            }
        }
    }

    updateDisplay(cachedPayload);

    esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);
    esp_deep_sleep_start();
}

void loop() {
    // Never reached — all logic runs in setup() before deep sleep.
}
