#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h> // Using SH110X base class, specific SH1106G is used for object
#include "time.h"           // For NTP
#include "bsec.h"           // Bosch BSEC library
#include <HTTPClient.h>
#include <ArduinoJson.h>    // For creating JSON payloads
#include <EEPROM.h>         // For saving BSEC state

// --- Configuration ---
// WiFi Credentials
const char* ssid = "Vodafone-219C"; // Replace with your SSID
const char* password = "LGmphrheY66m9mkH"; // Replace with your Password
// Server Endpoint
const char* serverName = "http://192.168.0.105:3000/sensor-data";

// OLED Display Settings
#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
#define OLED_RESET -1     // Reset pin # (or -1 if sharing Arduino reset pin)
#define I2C_ADDRESS 0x3C  // OLED I2C address

// I2C Pins (adjust if your board uses different pins)
#define SDA_PIN 21
#define SCL_PIN 22

// LED for error indication (often 2 on ESP32 dev boards)
#define ERROR_LED_PIN 2 // Or specify your pin, e.g., 2

// BSEC State Saving Configuration
#define EEPROM_SIZE (BSEC_MAX_STATE_BLOB_SIZE + 10) // +10 for magic number and future use (ensure BSEC_MAX_STATE_BLOB_SIZE is defined by BSEC lib)
#define BSEC_STATE_EEPROM_ADDR 0
#define BSEC_STATE_MAGIC_NUMBER_ADDR (BSEC_STATE_EEPROM_ADDR + BSEC_MAX_STATE_BLOB_SIZE) // Store magic number after the state
#define BSEC_STATE_VALID_MAGIC 0x42 // 'B' for BSEC - arbitrary magic number

const unsigned long BSEC_STATE_SAVE_INTERVAL_MS = 4 * 60 * 60 * 1000; // Save state every 4 hours if accuracy is good
//const unsigned long BSEC_STATE_SAVE_INTERVAL_MS = 5 * 60 * 1000; // For testing: Save every 5 minutes

// NTP Configuration
const char* ntpServer = "pool.ntp.org";
const char* timeZone = "CET-1CEST,M3.5.0,M10.5.0/3"; // Central European Time
bool timeSynchronized = false;
unsigned long lastNtpSyncAttempt = 0;
const unsigned long ntpSyncInterval = 60 * 60 * 1000; // Try to sync NTP every hour if not synced

// Loop Interval
const unsigned long LOOP_INTERVAL_MS = 3000; // BSEC LP mode is 3s, ULP is 5min. Adjust based on BSEC sample rate.

// --- Global Objects and Variables ---
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Bsec iaqSensor;
String bsecLogString; // For serial debugging output from BSEC
const char* currentIAQStatusText = "Initializing..."; // To store IAQ textual status

// BSEC state variables
uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE] = {0};
unsigned long lastBsecStateSaveTime = 0;

// --- Function Prototypes ---
void checkBsecStatus(void);
void handleErrorCondition(void);
void loadBsecState(void);
void saveBsecState(void);
void handleBsecStateSaving(void);
void connectToWiFi(void);
void initNTP(void);
void updateNTPTime(void);
void updateAndLogIAQStatusText(void);
void sendSensorDataToServer(void);
void displayDataOnOLED(void);
void printWithLeadingZero(int value);
String buildBsecDebugString(void);

// --- Setup ---
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10); // Wait for serial port to connect (for debugging)
  Serial.println("\n[INFO] Booting up sensor node...");

  pinMode(ERROR_LED_PIN, OUTPUT);
  digitalWrite(ERROR_LED_PIN, LOW); // LED Off

  // Initialize I2C
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize OLED Display
  if (!display.begin(I2C_ADDRESS, true)) { // Address 0x3C default, init i2c if not already
    Serial.println("[ERROR] SH1106G allocation failed or display not found");
    handleErrorCondition();
  }
  Serial.println("[INFO] OLED initialized.");
  display.display(); // show Adafruit splash screen
  delay(1000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setRotation(0); // Adjust if needed
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();

  // Initialize EEPROM for BSEC state
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("[ERROR] Failed to initialize EEPROM!");
    // Not necessarily a fatal error for BSEC if it can't save state, but good to know.
    // BSEC will just start fresh each time.
  } else {
    Serial.println("[INFO] EEPROM initialized.");
  }
  
  // Connect to WiFi
  connectToWiFi();

  // Initialize NTP
  initNTP();

  // Initialize BSEC Sensor
  iaqSensor.begin(BME68X_I2C_ADDR_HIGH, Wire); // Or BME68X_I2C_ADDR_LOW if configured
  Serial.println("[INFO] BSEC library version " + String(iaqSensor.version.major) + "." +
                   String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) +
                   "." + String(iaqSensor.version.minor_bugfix));
  checkBsecStatus();

  // Load BSEC state from EEPROM
  loadBsecState(); 

  // Define the BSEC virtual sensors to subscribe to
  bsec_virtual_sensor_t sensorList[] = {
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
    BSEC_OUTPUT_IAQ,                            // Main IAQ index
    BSEC_OUTPUT_STATIC_IAQ,                     // IAQ value with slower baseline changes
    BSEC_OUTPUT_CO2_EQUIVALENT,                 // CO2 equivalent in ppm
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,          // VOC equivalent in ppm
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE, // Compensated temperature
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,   // Compensated humidity
    // BSEC_OUTPUT_RAW_GAS, // Gas resistance - useful for debugging
    // BSEC_OUTPUT_STABILIZATION_STATUS, // To see if sensor is stable
    // BSEC_OUTPUT_RUN_IN_STATUS, // To see if run-in phase is complete
  };
  // Using BSEC_SAMPLE_RATE_LP (Low Power, data every 3 seconds)
  // For ULP (Ultra Low Power, data every 300s), use BSEC_SAMPLE_RATE_ULP
  iaqSensor.updateSubscription(sensorList, sizeof(sensorList)/sizeof(sensorList[0]), BSEC_SAMPLE_RATE_LP);
  checkBsecStatus();

  Serial.println("[INFO] Setup complete. Starting main loop.");
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Setup Complete!");
  display.display();
  delay(1000);
}

// --- Main Loop ---
unsigned long lastLoopTime = 0;

void loop() {
  unsigned long currentTime = millis();

  // 1. Let BSEC run continuously. It manages its own 3-second timing.
  // It will only return true when a new measurement is ready.
  if (iaqSensor.run()) { 
    bsecLogString = buildBsecDebugString();
    Serial.println(bsecLogString);

    updateAndLogIAQStatusText();
    
    // Send data to server and update screen ONLY when new data exists
    sendSensorDataToServer();   
    displayDataOnOLED();
    
    handleBsecStateSaving();    
  }

  // 2. Handle NTP sync independently (don't block BSEC)
  if (!timeSynchronized) {
    if (currentTime - lastNtpSyncAttempt >= 60000 || lastNtpSyncAttempt == 0) {
      updateNTPTime();
    }
  }

  // 3. Keep yielding so the ESP32 WiFi stack doesn't crash
  yield(); 
}

// --- BSEC Functions ---
String buildBsecDebugString() {
  String log = "Time:" + String(millis() / 1000) + "s";
  log += ", IAQ:" + String(iaqSensor.iaq);
  log += " (" + String(iaqSensor.iaqAccuracy) + ")"; // Accuracy: 0=stabilizing, 1=low, 2=medium, 3=high
  log += ", sIAQ:" + String(iaqSensor.staticIaq);
  log += ", CO2eq:" + String(iaqSensor.co2Equivalent);
  log += ", bVOC:" + String(iaqSensor.breathVocEquivalent);
  log += ", Temp:" + String(iaqSensor.temperature) + "C"; // Compensated Temp
  log += ", Hum:" + String(iaqSensor.humidity) + "%";    // Compensated Hum
  log += ", Pres:" + String(iaqSensor.pressure / 100.0) + "hPa";
  // log += ", rGas:" + String(iaqSensor.gasResistance) + "Ohm"; // Raw gas resistance
  // log += ", Stab:" + String(iaqSensor.stabStatus);
  // log += ", RunIn:" + String(iaqSensor.runInStatus);
  return log;
}

void checkBsecStatus(void) {
  bool isError = false; // Flag to track if a critical error occurred

  // Check BSEC library status
  if (iaqSensor.bsecStatus < BSEC_OK) { // Errors are typically negative values
    Serial.print("[ERROR] BSEC status: ");
    Serial.println(iaqSensor.bsecStatus);
    isError = true;
  } else if (iaqSensor.bsecStatus > BSEC_OK) { // Warnings are typically positive values
    Serial.print("[WARNING] BSEC status: ");
    Serial.println(iaqSensor.bsecStatus);
    // For warnings, we usually log them but don't halt.
  }

  // Check BME68X sensor status
  if (iaqSensor.bme68xStatus < BME68X_OK) { // Errors are typically negative values
    Serial.print("[ERROR] BME68X sensor status: ");
    Serial.println(iaqSensor.bme68xStatus);
    isError = true;
  } else if (iaqSensor.bme68xStatus > BME68X_OK) { // Warnings are typically positive values
    Serial.print("[WARNING] BME68X sensor status: ");
    Serial.println(iaqSensor.bme68xStatus);
    // For warnings, we usually log them but don't halt.
  }

  if (isError) {
    handleErrorCondition(); // Halt only if a critical error occurred
  }
}

void handleErrorCondition(void) {
  Serial.println("[FATAL] Unrecoverable error. Halting.");
  // Blink LED rapidly to indicate error
  while (true) {
    digitalWrite(ERROR_LED_PIN, HIGH);
    delay(100);
    digitalWrite(ERROR_LED_PIN, LOW);
    delay(100);
  }
}

void loadBsecState(void) {
  if (EEPROM.read(BSEC_STATE_MAGIC_NUMBER_ADDR) == BSEC_STATE_VALID_MAGIC) {
    Serial.println("[INFO] Valid BSEC state found in EEPROM. Loading...");
    for (uint16_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++) {
      bsecState[i] = EEPROM.read(BSEC_STATE_EEPROM_ADDR + i);
    }
    iaqSensor.setState(bsecState);
    checkBsecStatus(); // Check for errors after setting state
    Serial.println("[INFO] BSEC state loaded.");
  } else {
    Serial.println("[INFO] No valid BSEC state found in EEPROM or magic number mismatch. Starting fresh.");
    // Optionally, you could write the magic number here for the *next* save to be valid.
    // However, it's better to write it only when a valid state is saved.
  }
}

void saveBsecState(void) {
  Serial.println("[INFO] Saving BSEC state to EEPROM...");
  iaqSensor.getState(bsecState);
  checkBsecStatus(); // Check for errors after getting state

  for (uint16_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++) {
    EEPROM.write(BSEC_STATE_EEPROM_ADDR + i, bsecState[i]);
  }
  EEPROM.write(BSEC_STATE_MAGIC_NUMBER_ADDR, BSEC_STATE_VALID_MAGIC);

  if (EEPROM.commit()) {
    Serial.println("[INFO] BSEC state saved successfully.");
    lastBsecStateSaveTime = millis();
  } else {
    Serial.println("[ERROR] Failed to commit BSEC state to EEPROM.");
  }
}

void handleBsecStateSaving(void) {
  // Save state if:
  // 1. IAQ accuracy is high (>=3 means fully calibrated)
  // 2. Enough time has passed since the last save
  bool readyToSave = (iaqSensor.iaqAccuracy >= 3); 
  // For BSEC v1.x iaqAccuracy 3 is good. For BSEC v2.x this might be different (e.g. stabStatus & runInStatus)
  // You might also check iaqSensor.stabStatus and iaqSensor.runInStatus for BSEC v1.x.
  // For BSEC v2, refer to Bosch examples, as accuracy/stabilization flags might differ.

  if (readyToSave) {
    if (millis() - lastBsecStateSaveTime >= BSEC_STATE_SAVE_INTERVAL_MS) {
      saveBsecState();
    }
  }
}

// --- WiFi and NTP ---
void connectToWiFi() {
  Serial.print("[INFO] Connecting to WiFi: ");
  Serial.println(ssid);
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Connecting WiFi...");
  display.print(ssid);
  display.display();

  WiFi.begin(ssid, password);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 30) { // Try for ~15 seconds
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[INFO] WiFi connected!");
    Serial.print("[INFO] IP Address: ");
    Serial.println(WiFi.localIP());
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("WiFi Connected!");
    display.println(WiFi.localIP());
    display.display();
    delay(1000);
  } else {
    Serial.println("\n[ERROR] Failed to connect to WiFi.");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("WiFi Failed!");
    display.display();
    // Note: The device will continue to try and operate, but won't send data or sync time.
    // You could implement a more robust retry mechanism or enter a specific mode.
  }
}

void initNTP() {
  Serial.println("[INFO] Configuring time from NTP server: " + String(ntpServer));
  configTime(0, 0, ntpServer); // GMT offset 0, daylight offset 0 (handled by TZ string)
  setenv("TZ", timeZone, 1);
  tzset();
  updateNTPTime(); // Initial attempt to sync
}

void updateNTPTime() {
    if (WiFi.status() != WL_CONNECTED) {
        // Serial.println("[NTP] WiFi not connected, cannot sync time.");
        return;
    }

    if (millis() - lastNtpSyncAttempt < 60000 && lastNtpSyncAttempt != 0 && !timeSynchronized) { // Don't spam NTP server if failing
        return;
    }

    Serial.print("[NTP] Attempting to sync time... ");
    lastNtpSyncAttempt = millis();
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10000)) { // Timeout 10 seconds
        Serial.println("Failed to obtain time.");
        timeSynchronized = false;
    } else {
        Serial.println("Time synchronized.");
        Serial.print("[NTP] Current time: ");
        Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
        timeSynchronized = true;
    }
}


// --- Data Handling and Transmission ---
void updateAndLogIAQStatusText(void) {
  float currentIAQ = iaqSensor.staticIaq > 0 ? iaqSensor.staticIaq : iaqSensor.iaq; // Prefer staticIaq if available and valid
  
  if (isnan(currentIAQ) || iaqSensor.iaqAccuracy == 0) { // Also check accuracy
    currentIAQStatusText = "Stabilizing...";
  } else if (currentIAQ <= 50) {
    currentIAQStatusText = "Excellent";
  } else if (currentIAQ <= 100) {
    currentIAQStatusText = "Good";
  } else if (currentIAQ <= 150) {
    currentIAQStatusText = "Lightly Polluted";
  } else if (currentIAQ <= 200) {
    currentIAQStatusText = "Moderately Polluted";
  } else if (currentIAQ <= 250) {
    currentIAQStatusText = "Heavily Polluted";
  } else if (currentIAQ <= 350) {
    currentIAQStatusText = "Severely Polluted";
  } else {
    currentIAQStatusText = "Extremely Polluted";
  }
  // Serial.print("[INFO] Air Quality Status: "); Serial.println(currentIAQStatusText); // Already part of bsecLogString implicitly
}

void sendSensorDataToServer() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] WiFi not connected. Cannot send data.");
    return;
  }

  HTTPClient http;
  if (!http.begin(serverName)) {
    Serial.println("[HTTP] Failed to initialize HTTP client for POST.");
    return;
  }
  
  http.addHeader("Content-Type", "application/json");

  // Create JSON object - Adjust capacity as needed
  StaticJsonDocument<300> jsonDoc; // Increased capacity for more fields if needed

  jsonDoc["temperature"] = float(round(iaqSensor.temperature * 100) / 100.0); // Compensated Temp, 2 decimal places
  jsonDoc["humidity"] = float(round(iaqSensor.humidity * 100) / 100.0);      // Compensated Hum, 2 decimal places
  jsonDoc["pressure"] = float(round(iaqSensor.pressure / 10.0) / 10.0);    // Pressure in hPa, 1 decimal place
  jsonDoc["IAQ"] = iaqSensor.iaq > 0 ? iaqSensor.iaq : 0;                           // Main IAQ index
  jsonDoc["staticIAQ"] = iaqSensor.staticIaq > 0 ? iaqSensor.staticIaq : 0;         // Static IAQ
  jsonDoc["IAQAccuracy"] = iaqSensor.iaqAccuracy;
  jsonDoc["carbon"] = iaqSensor.co2Equivalent > 0 ? iaqSensor.co2Equivalent : 0;   // CO2 equivalent
  jsonDoc["VOC"] = iaqSensor.breathVocEquivalent > 0 ? iaqSensor.breathVocEquivalent : 0; // VOC equivalent
  jsonDoc["IAQsts"] = currentIAQStatusText;
  // jsonDoc["rawGas"] = iaqSensor.gasResistance; // Optional

  String jsonData;
  serializeJson(jsonDoc, jsonData);

  Serial.print("[HTTP] Sending POST request to "); Serial.println(serverName);
  Serial.print("[HTTP] JSON Payload: "); Serial.println(jsonData);

  int httpResponseCode = http.POST(jsonData);

  if (httpResponseCode > 0) {
    Serial.print("[HTTP] Response code: "); Serial.println(httpResponseCode);
    String responsePayload = http.getString();
    Serial.print("[HTTP] Response payload: "); Serial.println(responsePayload);
    if (httpResponseCode != HTTP_CODE_OK && httpResponseCode != 201) { // 200 OK, 201 Created
        Serial.println("[HTTP] POST request failed or server returned error.");
    }
  } else {
    Serial.print("[HTTP] Error on sending POST: ");
    Serial.println(http.errorToString(httpResponseCode).c_str());
  }

  http.end();
}


// --- OLED Display ---
void displayDataOnOLED() {
  display.clearDisplay();
  display.setCursor(0, 0); // Start at top-left (this will be Line 0)

  // Line 0: Time
  if (timeSynchronized) {
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    printWithLeadingZero(timeinfo.tm_hour); display.print(":");
    printWithLeadingZero(timeinfo.tm_min); // Removed seconds to save space
    display.print(" "); // Separator
    printWithLeadingZero(timeinfo.tm_mday); display.print("/");
    printWithLeadingZero(timeinfo.tm_mon + 1);
  } else {
    display.print("Time N/A");
    if(WiFi.status() != WL_CONNECTED) display.print(" (NoWiFi)");
  }
  display.println(); // Moves cursor to the start of Line 1

  // Line 1: Temp & Humidity
  display.print("T:"); display.print(iaqSensor.temperature, 1); display.print((char)247); // ° symbol
  display.print("C H:"); display.print(iaqSensor.humidity, 1); display.println("%"); // Moves to Line 2

  // Line 2: Pressure
  display.print("P:"); display.print(iaqSensor.pressure / 100.0, 1); display.println("hPa"); // Moves to Line 3

  // Line 3: IAQ (Static or regular) & Accuracy
  float displayIAQ = iaqSensor.staticIaq > 0 && iaqSensor.iaqAccuracy > 0 ? iaqSensor.staticIaq : iaqSensor.iaq;
  display.print("IAQ:"); display.print(displayIAQ, 0);
  display.print("(A:"); display.print(iaqSensor.iaqAccuracy); display.println(")"); // Moves to Line 4

  // Line 4: CO2
  display.print("CO2:"); display.print(iaqSensor.co2Equivalent, 0); display.println("ppm"); // Moves to Line 5

  // Line 5: VOC
  display.print("VOC:"); display.print(iaqSensor.breathVocEquivalent, 0); display.println("ppm"); // Moves to Line 6
                                                                                      // NO empty line should occur now. Cursor is at start of Line 6.
  // Line 6: IAQ Status Text
  display.print("Sts: ");
  String statusToDisplay = currentIAQStatusText;
  // Estimate max characters for status: 128px / ~6px_per_char = ~21 chars total line width
  // "Sts: " is 5 chars. So, 21 - 5 = 16 chars for the status message itself.
  int maxStatusTextLength = 16;
  if (statusToDisplay.length() > maxStatusTextLength) {
      statusToDisplay = statusToDisplay.substring(0, maxStatusTextLength - 2) + ".."; // ".." takes 2 chars
  }
  display.println(statusToDisplay); // Moves to Line 7

  // Line 7: WiFi Status (This is the last line)
  if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi:OK RSSI:");
    display.print(WiFi.RSSI());
    // display.print("dBm"); // Removed "dBm" to ensure it fits
  } else {
    display.print("WiFi: Disconnected");
  }
  // No display.println() here as it's the last line and we don't need to advance the cursor further.

  display.display();
}

void printWithLeadingZero(int value) {
  if (value < 10) {
    display.print('0');
  }
  display.print(value);
}