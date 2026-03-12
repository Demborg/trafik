#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <sys/time.h>

#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

#include "config.h"

GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT / 4> display(
    GxEPD2_426_GDEQ0426T82(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// --- Display ---

static const int MARGIN        = 12;
static const int COL_LINE      = MARGIN;
static const int COL_LINE_W    = 72;
static const int COL_DEST      = COL_LINE + COL_LINE_W + 8;
static const int COL_DEST_W    = 350;
static const int COL_T1        = COL_DEST + COL_DEST_W + 8;
static const int COL_T_W       = 100;
static const int ROW_H_MAIN    = 42;
static const int ROW_H_GROUP   = 32;
static const int MAX_TIMES     = 3;

static void drawMinutes(int x, int w, int y, int minutes) {
    String label = (minutes == 0) ? "Nu" : (String(minutes) + " min");
    int16_t tx, ty;
    uint16_t tw, th;
    display.getTextBounds(label, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor(x + w - tw, y);
    display.print(label);
}

static void drawContent(JsonDocument& doc, time_t now) {
    display.fillScreen(GxEPD_WHITE);

    int y = MARGIN;

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

        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        y += ROW_H_GROUP - 4;
        display.setCursor(MARGIN, y);
        display.print(group["label"].as<const char*>());
        y += 4;
        display.drawFastHLine(MARGIN, y, display.width() - 2 * MARGIN, GxEPD_BLACK);
        y += 6;

        for (JsonObject line : group["lines"].as<JsonArray>()) {
            const char* lineNum = line["line"] | "";

            for (JsonObject dest : line["destinations"].as<JsonArray>()) {
                if (y > display.height() - ROW_H_MAIN) break;

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

                display.setCursor(COL_LINE, baseline);
                display.print(lineNum);

                const char* destName = dest["destination"] | "";
                int16_t tx, ty; uint16_t tw, th;
                display.getTextBounds(destName, 0, 0, &tx, &ty, &tw, &th);
                display.setCursor(COL_DEST, baseline);
                if ((int)tw <= COL_DEST_W) {
                    display.print(destName);
                } else {
                    String s(destName);
                    while (s.length() > 0) {
                        display.getTextBounds(s.c_str(), 0, 0, &tx, &ty, &tw, &th);
                        if ((int)tw <= COL_DEST_W) break;
                        s.remove(s.length() - 1);
                    }
                    display.print(s);
                }

                for (int i = 0; i < count; i++) {
                    drawMinutes(COL_T1 + i * COL_T_W, COL_T_W, baseline, times[i]);
                }

                y += ROW_H_MAIN;
            }
        }

        y += 6;
    }
}

void updateDisplay(const char* json) {
    if (!json || json[0] == '\0') return;

    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        Serial.println("JSON parse failed");
        return;
    }

    int64_t serverTime = doc["server_time"].as<int64_t>();
    if (serverTime > 0) {
        struct timeval tv = { .tv_sec = (time_t)serverTime, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
    }

    time_t now = time(NULL);

    display.init(0, true, 10, false);
    display.setRotation(0);
    display.setFullWindow();

    display.firstPage();
    do {
        drawContent(doc, now);
    } while (display.nextPage());

    display.hibernate();
}

// --- Main ---

void setup() {
    Serial.begin(115200);
    delay(10000);
    Serial.println("\n=== Trafik Display ===");

    // Power on display
    pinMode(EPD_PWR, OUTPUT);
    digitalWrite(EPD_PWR, HIGH);

    // Init SPI
    SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);

    // Connect WiFi
    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
}

void loop() {
    Serial.println("Fetching departures...");
    HTTPClient http;
    http.begin(BACKEND_URL);
    int code = http.GET();
    Serial.printf("HTTP %d\n", code);

    if (code == HTTP_CODE_OK) {
        String body = http.getString();
        Serial.printf("Got %d bytes, updating display...\n", body.length());
        updateDisplay(body.c_str());
        Serial.println("Display updated!");
    } else {
        Serial.printf("Error: %s\n", http.errorToString(code).c_str());
    }
    http.end();

    delay(60000);
}
