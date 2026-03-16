#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <sys/time.h>

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "config.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif

#define BAT_SENSE_PIN 3

static float readBatteryVoltage() {
    uint32_t mv = analogReadMilliVolts(BAT_SENSE_PIN);
    return (mv * 2) / 1000.0f;
}

static int calculateBatteryPercentage(float voltage) {
    if (voltage >= 4.15f) return 100;
    if (voltage <= 3.30f) return 0;
    return (int)((voltage - 3.30f) / (4.15f - 3.30f) * 100.0f);
}

RTC_DATA_ATTR static int bootCount = 0;
RTC_DATA_ATTR static char cachedPayload[4096] = {0};
RTC_DATA_ATTR static int cyclesUntilNextPoll = 0;

GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT> display(
    GxEPD2_426_GDEQ0426T82(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

U8G2_FOR_ADAFRUIT_GFX u8g2;

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

static void epdCommand(uint8_t cmd, const uint8_t* data, size_t len) {
    digitalWrite(EPD_CS, LOW);
    digitalWrite(EPD_DC, LOW);
    SPI.transfer(cmd);
    digitalWrite(EPD_DC, HIGH);
    for (size_t i = 0; i < len; i++) SPI.transfer(data[i]);
    digitalWrite(EPD_CS, HIGH);
}

static void waveshareRefresh(bool partial) {
    while (digitalRead(EPD_BUSY) == HIGH) delay(1);

    // 0xF7 is full update, 0xFF is partial/fast update for many Waveshare panels.
    // GDEQ0426T82 specifically uses 0xF7 for full, and can use 0xFF for fast.
    uint8_t updateCtrl[] = { (uint8_t)(partial ? 0xFF : 0xF7) };
    epdCommand(0x22, updateCtrl, 1);
    epdCommand(0x20, nullptr, 0);

    delay(100);
    while (digitalRead(EPD_BUSY) == HIGH) delay(10);
}

static void drawMinutes(int x, int w, int y, int minutes) {
    char label[16];
    if (minutes == 0) {
        strcpy(label, "Nu");
    } else {
        snprintf(label, sizeof(label), "%d", minutes);
    }
    int textWidth = u8g2.getUTF8Width(label);
    u8g2.setCursor(x + w - textWidth - 4, y);
    u8g2.print(label);
}

static void drawHeader(time_t now) {
    struct tm tm;
    localtime_r(&now, &tm);
    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    char dateBuf[16];
    snprintf(dateBuf, sizeof(dateBuf), "%d/%d", tm.tm_mday, tm.tm_mon + 1);

    u8g2.setFont(u8g2_font_helvB24_te);
    int timeWidth = u8g2.getUTF8Width(timeBuf);
    u8g2.setCursor(MARGIN, MARGIN + 28);
    u8g2.print(timeBuf);

    u8g2.setFont(u8g2_font_helvR14_te);
    u8g2.setCursor(MARGIN + timeWidth + 8, MARGIN + 28);
    u8g2.print(dateBuf);
}

static void drawWeather(JsonDocument& doc) {
    const char* weather = doc["weather"] | "";
    if (weather[0] != '\0') {
        u8g2.setFont(u8g2_font_helvB24_te);
        int textWidth = u8g2.getUTF8Width(weather);
        u8g2.setCursor(display.width() - MARGIN - textWidth, MARGIN + 28);
        u8g2.print(weather);
    }
}

static void drawGroupSeparator(int y) {
    display.drawFastHLine(MARGIN, y, display.width() - 2 * MARGIN, GxEPD_BLACK);
    
    u8g2.setFont(u8g2_font_helvR12_te);
    const char* minLabel = "min";
    int labelWidth = u8g2.getUTF8Width(minLabel);
    u8g2.setCursor(display.width() - MARGIN - labelWidth, y + 12);
    u8g2.print(minLabel);
}

static void drawDepartures(JsonObject& dest, time_t now, int baseline) {
    int times[MAX_TIMES];
    int count = 0;
    for (JsonVariant ts : dest["departures"].as<JsonArray>()) {
        int minutesUntil = (int)((ts.as<int64_t>() - (int64_t)now) / 60);
        if (minutesUntil >= 0 && count < MAX_TIMES) {
            times[count++] = minutesUntil;
        }
    }
    if (count == 0) return;

    u8g2.setFont(u8g2_font_helvB24_te);
    for (int i = 0; i < count; i++) {
        drawMinutes(COL_T1 + i * COL_T_W, COL_T_W, baseline, times[i]);
    }
}

static void drawGroups(JsonDocument& doc, time_t now) {
    int y = MARGIN + 36;

    JsonArray groups = doc["groups"].as<JsonArray>();
    for (JsonObject group : groups) {
        if (y > display.height() - ROW_H_GROUP) break;

        u8g2.setFont(u8g2_font_helvB18_te);
        y += ROW_H_GROUP - 4;
        u8g2.setCursor(MARGIN, y);
        u8g2.print(group["label"].as<const char*>());
        
        y += 4;
        drawGroupSeparator(y);
        y += 6;

        for (JsonObject line : group["lines"].as<JsonArray>()) {
            const char* lineNum = line["line"] | "";

            for (JsonObject dest : line["destinations"].as<JsonArray>()) {
                if (y > display.height() - ROW_H_MAIN) break;

                int baseline = y + ROW_H_MAIN - 8;

                u8g2.setFont(u8g2_font_helvB24_te);
                u8g2.setCursor(COL_LINE, baseline);
                u8g2.print(lineNum);

                u8g2.setFont(u8g2_font_helvR24_te);
                u8g2.setCursor(COL_DEST, baseline);
                u8g2.print(dest["destination"].as<const char*>());

                drawDepartures(dest, now, baseline);
                y += ROW_H_MAIN;
            }
        }
        y += 6;
    }
}

static void drawBattery(float vBat, int pBat) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", pBat);
    
    u8g2.setFont(u8g2_font_helvR14_te);
    int textWidth = u8g2.getUTF8Width(buf);
    u8g2.setCursor((display.width() - textWidth) / 2, MARGIN + 28);
    u8g2.print(buf);
}

static void drawContent(JsonDocument& doc, time_t now, float vBat, int pBat) {
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setBackgroundColor(GxEPD_WHITE);

    drawHeader(now);
    drawBattery(vBat, pBat);
    drawWeather(doc);
    drawGroups(doc, now);
}

static void syncSystemTime(JsonDocument& doc) {
    int64_t serverTime = doc["server_time"].as<int64_t>();
    if (serverTime > 0) {
        struct timeval tv = { .tv_sec = (time_t)serverTime, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
    }
}

static void prepareDisplay() {
    display.init(0, true, 10, false);
    display.setRotation(0);
    display.setFullWindow();
}

static void renderFrame(JsonDocument& doc, time_t now, float vBat, int pBat, bool isPartial) {
    display.fillScreen(GxEPD_WHITE);
    drawContent(doc, now, vBat, pBat);
    display.display(isPartial);
    waveshareRefresh(isPartial);
}

static void connectToWiFi() {
    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi connection failed!");
    }
}

static bool fetchDepartures() {
    float vBat = readBatteryVoltage();
    int pBat = calculateBatteryPercentage(vBat);
    Serial.printf("Battery: %.2fV (%d%%)\n", vBat, pBat);

    connectToWiFi();
    if (WiFi.status() != WL_CONNECTED) return false;

    Serial.println("Fetching departures...");
    HTTPClient http;
    char url[256];
    snprintf(url, sizeof(url), "%s?v_bat=%.2f&p_bat=%d&version=%s", BACKEND_URL, vBat, pBat, FIRMWARE_VERSION);
    http.begin(url);
    int code = http.GET();
    Serial.printf("HTTP %d\n", code);

    bool success = false;
    if (code == HTTP_CODE_OK) {
        String body = http.getString();
        if (body.length() < sizeof(cachedPayload)) {
            strncpy(cachedPayload, body.c_str(), sizeof(cachedPayload) - 1);
            cachedPayload[sizeof(cachedPayload) - 1] = '\0';
            success = true;
        } else {
            Serial.println("Response too large for cache");
        }
    } else {
        Serial.printf("Error: %s\n", http.errorToString(code).c_str());
    }
    http.end();
    WiFi.disconnect(true);
    return success;
}

void setup() {
    Serial.begin(115200);
    Serial.printf("\n=== Trafik Display (Boot: %d) ===\n", bootCount);

    bool needsNetwork = (bootCount == 0 || cyclesUntilNextPoll <= 0 || cachedPayload[0] == '\0');
    bool networkSuccess = false;

    if (needsNetwork) {
        networkSuccess = fetchDepartures();
        if (!networkSuccess && bootCount == 0) {
            // Initial boot must have network
            Serial.println("Initial fetch failed, sleeping...");
            esp_sleep_enable_timer_wakeup(POLL_FALLBACK_SLEEP_SECONDS * 1000000ULL);
            esp_deep_sleep_start();
        }
    }

    JsonDocument doc;
    if (deserializeJson(doc, cachedPayload) == DeserializationError::Ok) {
        if (needsNetwork && networkSuccess) {
            syncSystemTime(doc);
            int suggested = doc["suggested_sleep_seconds"] | (NETWORK_POLL_EVERY_N_CYCLES * 60);
            cyclesUntilNextPoll = suggested / 60;
        } else {
            cyclesUntilNextPoll--;
        }

        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();

        time_t now = time(NULL);
        float vBat = readBatteryVoltage();
        int pBat = calculateBatteryPercentage(vBat);

        pinMode(EPD_PWR, OUTPUT);
        digitalWrite(EPD_PWR, HIGH);
        SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
        u8g2.begin(display);

        prepareDisplay();
        
        // Full refresh every 10 cycles or on network update to prevent ghosting
        bool isPartial = (bootCount % 10 != 0) && !needsNetwork;
        renderFrame(doc, now, vBat, pBat, isPartial);
        
        display.hibernate();
        digitalWrite(EPD_PWR, LOW);
    } else {
        Serial.println("JSON parse failed");
    }

    bootCount++;
    Serial.printf("Sleeping for %d seconds...\n", DISPLAY_REFRESH_SLEEP_SECONDS);
    esp_sleep_enable_timer_wakeup(DISPLAY_REFRESH_SLEEP_SECONDS * 1000000ULL);
    esp_deep_sleep_start();
}

void loop() {
    // Never reached
}

