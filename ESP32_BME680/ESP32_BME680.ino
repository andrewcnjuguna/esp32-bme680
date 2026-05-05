#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "time.h"
#include "bsec.h"
#include <HTTPClient.h>
#include <ArduinoOTA.h>
#include <Preferences.h>

// ── Pins ─────────────────────────────────────────────────────────────────────
#define LED_BUILTIN    2
#define SDA_PIN        21
#define SCL_PIN        22

// ── OLED ─────────────────────────────────────────────────────────────────────
#define I2C_ADDR_OLED  0x3C
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1

// ── Timing ───────────────────────────────────────────────────────────────────
#define SLEEP_DURATION_S   30          // seconds between readings
#define OTA_WINDOW_BOOT_MS 20000       // OTA window on first/power-on boot
#define OTA_WINDOW_WAKE_MS 4000        // OTA window on each deep-sleep wake
#define BSEC_READ_TIMEOUT  12000       // max ms to wait for a BSEC reading
#define WIFI_TIMEOUT_MS    15000

// ── Credentials / server ─────────────────────────────────────────────────────
const char* ssid       = "YOUR_WIFI_SSID";
const char* password   = "YOUR_WIFI_PASSWORD";
const char* serverName = "http://<rpi-ip>:3000/sensor-data";

// ── Objects ───────────────────────────────────────────────────────────────────
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Bsec             iaqSensor;
Preferences      prefs;

// ── RTC memory (survives deep sleep) ─────────────────────────────────────────
RTC_DATA_ATTR uint32_t bootCount    = 0;
RTC_DATA_ATTR bool     hasBsecState = false;

// ── Sensor snapshot ───────────────────────────────────────────────────────────
struct SensorData {
    float temperature;
    float humidity;
    float pressure;   // Pa
    float iaq;
    float co2;
    float voc;
    int   iaqAccuracy;
} data = {};

// ── IAQ label helpers ─────────────────────────────────────────────────────────
static const char* iaqStatusFull(float iaq) {
    if (isnan(iaq) || iaq < 0) return "Invalid";
    if (iaq <= 50)   return "Excellent";
    if (iaq <= 100)  return "Good";
    if (iaq <= 150)  return "Lightly polluted";
    if (iaq <= 200)  return "Moderately polluted";
    if (iaq <= 250)  return "Heavily polluted";
    if (iaq <= 350)  return "Severely polluted";
    return "Extremely polluted";
}

static const char* iaqStatusShort(float iaq) {
    if (isnan(iaq) || iaq < 0) return "Invalid";
    if (iaq <= 50)   return "Excellent";
    if (iaq <= 100)  return "Good";
    if (iaq <= 150)  return "Lt. Polluted";
    if (iaq <= 200)  return "Mod. Polluted";
    if (iaq <= 250)  return "Hvy. Polluted";
    if (iaq <= 350)  return "Svr. Polluted";
    return "Ext. Polluted";
}

// ── Display helpers ───────────────────────────────────────────────────────────
static void printZ(int v) {
    if (v < 10) display.print('0');
    display.print(v, DEC);
}

static void showStatus(const char* msg) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(msg);
    display.display();
}

// Draw the main sensor screen.
// sleeping=true shows the deep-sleep countdown; false shows "OTA on".
static void updateDisplay(bool sleeping) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);

    // Row 0 (y=0) — Date / Time
    time_t now;
    struct tm t;
    time(&now);
    display.setCursor(0, 0);
    if (now > 24 * 3600) {
        localtime_r(&now, &t);
        printZ(t.tm_mday);       display.print('/');
        printZ(t.tm_mon + 1);    display.print('/');
        printZ((t.tm_year + 1900) % 100); display.print(' ');
        printZ(t.tm_hour);       display.print(':');
        printZ(t.tm_min);        display.print(':');
        printZ(t.tm_sec);
    } else {
        display.print("Syncing time...");
    }

    // Row 1 (y=9) — Temperature | Humidity
    display.setCursor(0, 9);
    display.print("T:");
    display.print(data.temperature, 1);
    display.print("C  H:");
    display.print(data.humidity, 1);
    display.print('%');

    // Row 2 (y=18) — Pressure
    display.setCursor(0, 18);
    display.print("P:");
    display.print(data.pressure / 100.0, 1);
    display.print(" hPa");

    // Row 3 (y=27) — IAQ | CO2
    display.setCursor(0, 27);
    display.print("IAQ:");
    display.print(data.iaq, 0);
    display.print("  CO2:");
    display.print(data.co2, 0);

    // Row 4 (y=36) — VOC
    display.setCursor(0, 36);
    display.print("VOC:");
    display.print(data.voc, 2);
    display.print(" ppm");

    // Row 5 (y=45) — Air quality label
    display.setCursor(0, 45);
    display.print("Air: ");
    display.print(iaqStatusShort(data.iaq));

    // Row 6 (y=54) — Accuracy | sleep/OTA indicator
    display.setCursor(0, 54);
    display.print("Acc:");
    display.print(data.iaqAccuracy);
    if (sleeping) {
        display.print("  Zzz ");
        display.print(SLEEP_DURATION_S);
        display.print('s');
    } else {
        display.print("  OTA:on");
    }

    display.display();
}

// ── BSEC status check ─────────────────────────────────────────────────────────
static void checkIaqSensorStatus() {
    if (iaqSensor.bsecStatus != BSEC_OK) {
        String msg = (iaqSensor.bsecStatus < BSEC_OK)
            ? "BSEC error: "   + String(iaqSensor.bsecStatus)
            : "BSEC warning: " + String(iaqSensor.bsecStatus);
        Serial.println(msg);
        if (iaqSensor.bsecStatus < BSEC_OK) {
            for (;;) {
                pinMode(LED_BUILTIN, OUTPUT);
                digitalWrite(LED_BUILTIN, HIGH); delay(100);
                digitalWrite(LED_BUILTIN, LOW);  delay(100);
            }
        }
    }
    if (iaqSensor.bme68xStatus != BME68X_OK) {
        String msg = (iaqSensor.bme68xStatus < BME68X_OK)
            ? "BME68X error: "   + String(iaqSensor.bme68xStatus)
            : "BME68X warning: " + String(iaqSensor.bme68xStatus);
        Serial.println(msg);
        if (iaqSensor.bme68xStatus < BME68X_OK) {
            for (;;) {
                pinMode(LED_BUILTIN, OUTPUT);
                digitalWrite(LED_BUILTIN, HIGH); delay(100);
                digitalWrite(LED_BUILTIN, LOW);  delay(100);
            }
        }
    }
}

// ── BSEC state persistence ────────────────────────────────────────────────────
static void saveBsecState() {
    uint8_t state[BSEC_MAX_STATE_BLOB_SIZE];
    iaqSensor.getState(state);
    prefs.begin("bsec", false);
    prefs.putBytes("state", state, BSEC_MAX_STATE_BLOB_SIZE);
    prefs.end();
    hasBsecState = true;
}

static void loadBsecState() {
    if (!hasBsecState) return;
    uint8_t state[BSEC_MAX_STATE_BLOB_SIZE];
    prefs.begin("bsec", true);
    size_t len = prefs.getBytes("state", state, BSEC_MAX_STATE_BLOB_SIZE);
    prefs.end();
    if (len == BSEC_MAX_STATE_BLOB_SIZE) {
        iaqSensor.setState(state);
        checkIaqSensorStatus();
        Serial.println("BSEC state restored");
    }
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
static bool connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    uint32_t start = millis();
    Serial.print("WiFi");
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("IP: " + WiFi.localIP().toString());
        return true;
    }
    Serial.println("WiFi timeout");
    return false;
}

// ── OTA ───────────────────────────────────────────────────────────────────────
static void setupAndRunOTA(uint32_t windowMs) {
    bool otaStarted = false;

    ArduinoOTA.setHostname("esp32-bme680");
    ArduinoOTA.onStart([&]() {
        otaStarted = true;
        showStatus("OTA update...");
        Serial.println("OTA start");
    });
    ArduinoOTA.onEnd([]() {
        showStatus("OTA done. Rebooting.");
        Serial.println("OTA end");
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.println("OTA error: " + String(e));
    });
    ArduinoOTA.begin();

    updateDisplay(false);

    uint32_t otaStart = millis();
    // Keep running until window expires; if OTA starts, stay until reboot.
    while (millis() - otaStart < windowMs || otaStarted) {
        ArduinoOTA.handle();
        delay(10);
    }
}

// ── BSEC init ─────────────────────────────────────────────────────────────────
static void setupBsec() {
    iaqSensor.begin(BME68X_I2C_ADDR_HIGH, Wire);
    Serial.println("BSEC v" +
        String(iaqSensor.version.major) + '.' +
        String(iaqSensor.version.minor) + '.' +
        String(iaqSensor.version.major_bugfix) + '.' +
        String(iaqSensor.version.minor_bugfix));
    checkIaqSensorStatus();

    bsec_virtual_sensor_t sensors[10] = {
        BSEC_OUTPUT_RAW_TEMPERATURE,
        BSEC_OUTPUT_RAW_PRESSURE,
        BSEC_OUTPUT_RAW_HUMIDITY,
        BSEC_OUTPUT_RAW_GAS,
        BSEC_OUTPUT_IAQ,
        BSEC_OUTPUT_STATIC_IAQ,
        BSEC_OUTPUT_CO2_EQUIVALENT,
        BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
        BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
        BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    };
    iaqSensor.updateSubscription(sensors, 10, BSEC_SAMPLE_RATE_LP);
    checkIaqSensorStatus();

    loadBsecState();
}

// ── HTTP ──────────────────────────────────────────────────────────────────────
static void sendSensorData() {
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");
    String json =
        "{\"temperature\":"  + String(data.temperature) +
        ",\"humidity\":"     + String(data.humidity) +
        ",\"pressure\":"     + String(data.pressure / 100.0) +
        ",\"IAQ\":"          + String(data.iaq) +
        ",\"carbon\":"       + String(data.co2) +
        ",\"VOC\":"          + String(data.voc) +
        ",\"IAQsts\":\""     + iaqStatusFull(data.iaq) + "\"}";
    int code = http.POST(json);
    Serial.println("HTTP POST: " + String(code));
    http.end();
}

// ── Deep sleep ────────────────────────────────────────────────────────────────
static void enterDeepSleep() {
    Serial.println("Sleeping " + String(SLEEP_DURATION_S) + "s");
    Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_S * 1000000ULL);
    esp_deep_sleep_start();
}

// ── Setup (runs on every wake) ────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(100);
    bootCount++;

    bool timerWake = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER);
    Serial.printf("\nBoot #%u  timer_wake=%d\n", bootCount, timerWake);

    Wire.begin(SDA_PIN, SCL_PIN);
    display.begin(I2C_ADDR_OLED, true);
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setRotation(0);

    showStatus("Connecting WiFi...");
    bool wifiOk = connectWiFi();

    if (wifiOk) {
        configTime(0, 0, "pool.ntp.org");
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();

        uint32_t otaWindow = timerWake ? OTA_WINDOW_WAKE_MS : OTA_WINDOW_BOOT_MS;
        setupAndRunOTA(otaWindow);
    }

    setupBsec();

    // Poll BSEC until a valid reading arrives or timeout
    bool gotReading = false;
    uint32_t bsecStart = millis();
    while (millis() - bsecStart < BSEC_READ_TIMEOUT) {
        if (iaqSensor.run()) {
            data.temperature = iaqSensor.temperature;
            data.humidity    = iaqSensor.humidity;
            data.pressure    = iaqSensor.pressure;
            data.iaq         = iaqSensor.staticIaq;
            data.co2         = iaqSensor.co2Equivalent;
            data.voc         = iaqSensor.breathVocEquivalent;
            data.iaqAccuracy = iaqSensor.iaqAccuracy;
            gotReading = true;
            break;
        }
        delay(50);
    }

    if (gotReading) {
        Serial.printf("T:%.1f H:%.1f P:%.1f IAQ:%.0f CO2:%.0f VOC:%.2f Acc:%d\n",
            data.temperature, data.humidity, data.pressure / 100.0,
            data.iaq, data.co2, data.voc, data.iaqAccuracy);
        saveBsecState();
        sendSensorData();
    } else {
        Serial.println("BSEC: no reading within timeout");
    }

    updateDisplay(true);
    delay(3000);  // display visible before screen goes dark during sleep

    enterDeepSleep();
}

// All logic is in setup(); device sleeps before reaching loop().
void loop() {}
