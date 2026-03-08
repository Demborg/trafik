#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <sys/time.h>

#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

#include "config.h"

// RTC memory survives deep sleep — used to track cycle count and cache data.
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR char cachedPayload[4096];

GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT / 4> display(
    GxEPD2_426_GDEQ0426T82(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// --- Display ---

// Layout constants
static const int MARGIN        = 12;
static const int COL_LINE      = MARGIN;         // line number column x
static const int COL_LINE_W    = 72;             // width reserved for line number
static const int COL_DEST      = COL_LINE + COL_LINE_W + 8;  // destination x
static const int COL_DEST_W    = 350;
static const int COL_T1        = COL_DEST + COL_DEST_W + 8;  // first time column x
static const int COL_T_W       = 100;            // width of each time column
static const int ROW_H_MAIN    = 42;             // row height for 18pt rows
static const int ROW_H_GROUP   = 32;             // row height for group headers
static const int MAX_TIMES     = 3;              // departure times shown per dest

static void drawMinutes(int x, int w, int y, int minutes) {
    String label = (minutes == 0) ? "Nu" : (String(minutes) + " min");
    int16_t tx, ty;
    uint16_t tw, th;
    display.getTextBounds(label, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor(x + w - tw, y);  // right-align within column
    display.print(label);
}

static void drawContent(JsonDocument& doc, time_t now) {
    display.fillScreen(GxEPD_WHITE);

    int y = MARGIN;

    // Weather — top-right corner in small font
    const char* weather = doc["weather"] | "";
    if (weather[0] != '\0') {
        display.setFont(&FreeSans9pt7b);
        display.setTextColor(GxEPD_BLACK);
        int16_t tx, ty; uint16_t tw, th;
        display.getTextBounds(weather, 0, 0, &tx, &ty, &tw, &th);
        display.setCursor(display.width() - MARGIN - tw, MARGIN + th);
        display.print(weather);
    }

    JsonArray groups = doc["groups"].as<JsonArray>();
    for (JsonObject group : groups) {
        if (y > display.height() - ROW_H_GROUP) break;

        // Group label (e.g. "Tunnelbana", "Buss")
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        y += ROW_H_GROUP - 4;
        display.setCursor(MARGIN, y);
        display.print(group["label"].as<const char*>());
        y += 4;
        // Separator line
        display.drawFastHLine(MARGIN, y, display.width() - 2 * MARGIN, GxEPD_BLACK);
        y += 6;

        for (JsonObject line : group["lines"].as<JsonArray>()) {
            const char* lineNum = line["line"] | "";

            for (JsonObject dest : line["destinations"].as<JsonArray>()) {
                if (y > display.height() - ROW_H_MAIN) break;

                // Collect up to MAX_TIMES future departure minutes
                int times[MAX_TIMES];
                int count = 0;
                for (JsonVariant ts : dest["departures"].as<JsonArray>()) {
                    int mins = (int)((ts.as<int64_t>() - (int64_t)now) / 60);
                    if (mins >= 0 && count < MAX_TIMES) {
                        times[count++] = mins;
                    }
                }
                if (count == 0) continue;

                int baseline = y + ROW_H_MAIN - 8;

                display.setFont(&FreeSansBold18pt7b);
                display.setTextColor(GxEPD_BLACK);

                // Line number
                display.setCursor(COL_LINE, baseline);
                display.print(lineNum);

                // Destination — truncate if too wide
                const char* destName = dest["destination"] | "";
                int16_t tx, ty; uint16_t tw, th;
                display.getTextBounds(destName, 0, 0, &tx, &ty, &tw, &th);
                display.setCursor(COL_DEST, baseline);
                if ((int)tw <= COL_DEST_W) {
                    display.print(destName);
                } else {
                    // Print characters until we'd overflow
                    String s(destName);
                    while (s.length() > 0) {
                        display.getTextBounds(s.c_str(), 0, 0, &tx, &ty, &tw, &th);
                        if ((int)tw <= COL_DEST_W) break;
                        s.remove(s.length() - 1);
                    }
                    display.print(s);
                }

                // Departure times
                for (int i = 0; i < count; i++) {
                    drawMinutes(COL_T1 + i * COL_T_W, COL_T_W, baseline, times[i]);
                }

                y += ROW_H_MAIN;
            }
        }

        y += 6;  // gap between groups
    }
}

void updateDisplay(const char* json) {
    if (!json || json[0] == '\0') return;

    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    time_t now = time(NULL);

    display.init(115200, true, 2, false);
    display.setRotation(0);
    display.setFullWindow();

    display.firstPage();
    do {
        drawContent(doc, now);
    } while (display.nextPage());

    display.hibernate();
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
