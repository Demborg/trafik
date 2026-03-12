#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <sys/time.h>

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "config.h"

GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT / 4> display(
    GxEPD2_426_GDEQ0426T82(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Send a raw command+data to the display via SPI (bypasses GxEPD2)
static void epdCommand(uint8_t cmd, const uint8_t* data, size_t len) {
    digitalWrite(EPD_CS, LOW);
    digitalWrite(EPD_DC, LOW);
    SPI.transfer(cmd);
    digitalWrite(EPD_DC, HIGH);
    for (size_t i = 0; i < len; i++) SPI.transfer(data[i]);
    digitalWrite(EPD_CS, HIGH);
}

// Trigger a Waveshare-style full refresh (without the 0x21/0x40 bypass RED)
static void waveshareRefresh() {
    // Wait for any previous operation
    while (digitalRead(EPD_BUSY) == HIGH) delay(1);

    uint8_t updateCtrl[] = {0xF7};
    epdCommand(0x22, updateCtrl, 1);
    epdCommand(0x20, nullptr, 0);

    // Wait for refresh to complete
    delay(100);
    while (digitalRead(EPD_BUSY) == HIGH) delay(10);
}

U8G2_FOR_ADAFRUIT_GFX u8g2;

// --- Display ---

static const int MARGIN        = 16;
static const int COL_LINE      = MARGIN;
static const int COL_LINE_W    = 70;
static const int COL_DEST      = COL_LINE + COL_LINE_W;
static const int COL_DEST_W    = 430;
static const int COL_T1        = COL_DEST + COL_DEST_W;
static const int COL_T_W       = 90;
static const int ROW_H_MAIN    = 54;
static const int ROW_H_GROUP   = 40;
static const int MAX_TIMES     = 3;

static void drawMinutesU8(int x, int w, int y, int minutes) {
    char label[16];
    if (minutes == 0) {
        strcpy(label, "Nu");
    } else {
        snprintf(label, sizeof(label), "%d", minutes);
    }
    int tw = u8g2.getUTF8Width(label);
    u8g2.setCursor(x + w - tw - 4, y);
    u8g2.print(label);
}

static void drawContent(JsonDocument& doc, time_t now) {
    display.fillScreen(GxEPD_WHITE);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setBackgroundColor(GxEPD_WHITE);

    int y = MARGIN;

    // Time & date — top-left
    {
        struct tm tm;
        localtime_r(&now, &tm);
        char timeBuf[6];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", tm.tm_hour, tm.tm_min);
        char dateBuf[16];
        snprintf(dateBuf, sizeof(dateBuf), "%d/%d", tm.tm_mday, tm.tm_mon + 1);

        u8g2.setFont(u8g2_font_helvB24_te);
        int timeW = u8g2.getUTF8Width(timeBuf);
        u8g2.setCursor(MARGIN, MARGIN + 28);
        u8g2.print(timeBuf);

        u8g2.setFont(u8g2_font_helvR14_te);
        u8g2.setCursor(MARGIN + timeW + 8, MARGIN + 28);
        u8g2.print(dateBuf);
    }

    // Weather — top-right corner, large
    const char* weather = doc["weather"] | "";
    if (weather[0] != '\0') {
        u8g2.setFont(u8g2_font_helvB24_te);
        int tw = u8g2.getUTF8Width(weather);
        u8g2.setCursor(display.width() - MARGIN - tw, MARGIN + 28);
        u8g2.print(weather);
    }

    y = MARGIN + 36;

    JsonArray groups = doc["groups"].as<JsonArray>();
    for (JsonObject group : groups) {
        if (y > display.height() - ROW_H_GROUP) break;

        // Group label
        u8g2.setFont(u8g2_font_helvB18_te);
        y += ROW_H_GROUP - 4;
        u8g2.setCursor(MARGIN, y);
        u8g2.print(group["label"].as<const char*>());
        y += 4;
        display.drawFastHLine(MARGIN, y, display.width() - 2 * MARGIN, GxEPD_BLACK);
        y += 6;

        // "min" label right-aligned just below the separator
        u8g2.setFont(u8g2_font_helvR12_te);
        {
            const char* minLabel = "min";
            int tw = u8g2.getUTF8Width(minLabel);
            u8g2.setCursor(display.width() - MARGIN - tw, y + 12);
            u8g2.print(minLabel);
        }

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

                // Line number (bold)
                u8g2.setFont(u8g2_font_helvB24_te);
                u8g2.setCursor(COL_LINE, baseline);
                u8g2.print(lineNum);

                // Destination
                u8g2.setFont(u8g2_font_helvR24_te);
                u8g2.setCursor(COL_DEST, baseline);
                u8g2.print(dest["destination"].as<const char*>());

                // Departure times
                u8g2.setFont(u8g2_font_helvB24_te);
                for (int i = 0; i < count; i++) {
                    drawMinutesU8(COL_T1 + i * COL_T_W, COL_T_W, baseline, times[i]);
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

    // Write image data to RAM (but GxEPD2's refresh will run with 0x21/0x40)
    display.firstPage();
    do {
        drawContent(doc, now);
    } while (display.nextPage());

    // Re-trigger refresh without the "bypass RED" flag
    waveshareRefresh();

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

    // Init SPI and display helper
    SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
    u8g2.begin(display);

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
