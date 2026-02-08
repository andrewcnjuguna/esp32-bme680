#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <DHT.h>
#include "time.h"
#include "bsec.h"
#include <WebServer.h>
//#include "ESPAsyncWebServer.h"
#include <HTTPClient.h>
#include <stdio.h>

#define SEALEVELPRESSURE_HPA (1013.25)
#define LED_BUILTIN 2

#define i2c_Address 0x3c // Initialize with the I2C addr 0x3C, typically eBay OLEDs
#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels

// Replace SDA_PIN and SCL_PIN with the corresponding pins for your board
#define SDA_PIN 21
#define SCL_PIN 22
#define OLED_RESET -1

Bsec iaqSensor;
String output;
void checkIaqSensorStatus(void);
void errLeds(void);

float temperature;
float humidity;
float pressure;
float IAQ;
float carbon;
float VOC;
const char* IAQsts;

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* serverName = "http://<rpi-ip>:3000/sensor-data";


void setup() {
  Serial.begin(115200);
  delay(100);
  Wire.begin(SDA_PIN, SCL_PIN);

  display.begin(i2c_Address, true); // Address 0x3C default
  Serial.println("OLED begun");

  display.display();
  delay(100);
  display.clearDisplay();
  display.display();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setRotation(0);
  Serial.println("Connecting to ");
  Serial.println(ssid);

  // Connect to your local Wi-Fi network
  WiFi.begin(ssid, password);

  // Check if Wi-Fi is connected to Wi-Fi network
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected..!");
  Serial.print("Got IP: ");
  Serial.println(WiFi.localIP());

  iaqSensor.begin(BME68X_I2C_ADDR_HIGH, Wire);
  output = "\nBSEC library version " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);

  Serial.println(output);
  checkIaqSensorStatus();

  bsec_virtual_sensor_t sensorList[10] = {
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

  iaqSensor.updateSubscription(sensorList, 10, BSEC_SAMPLE_RATE_LP);
  checkIaqSensorStatus();

  // Configure time using NTP
  configTime(0, 0, "pool.ntp.org");
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
}

void loop() {
  display.setCursor(0, 0);
  display.clearDisplay();

  if (iaqSensor.run()) { // If new data is available
    updateOutputString();
    Serial.println(output);
    // Offload sending data to a separate function
    sendSensorData();
  } else {
    //checkIaqSensorStatus();   //uncomment this for debugging in case of errors
  }

 
  printIAQStatus();

  updateDisplay();

  display.display();

  delay(2000);
}

void updateOutputString() {
  unsigned long time_trigger = millis();
  output = String(time_trigger);
  output += ",IAQ:" + String(iaqSensor.iaq);
  output += ",IAQAcc:" + String(iaqSensor.iaqAccuracy);
  output += ",StaticIAQ:" + String(iaqSensor.staticIaq);
  output += ",CO2:" + String(iaqSensor.co2Equivalent);
  output += ",VOC:" + String(iaqSensor.breathVocEquivalent);
  output += ",Temp:" + String(iaqSensor.rawTemperature);
  output += ",Pressure:" + String(iaqSensor.pressure);
  output += ",Humidity:" + String(iaqSensor.rawHumidity);
  output += ",GasRes:" + String(iaqSensor.gasResistance);
  output += ",StabStatus:" + String(iaqSensor.stabStatus);
  output += ",RunInStatus:" + String(iaqSensor.runInStatus);
  output += ",CompTemp:" + String(iaqSensor.temperature);
  output += ",CompHumidity:" + String(iaqSensor.humidity);
  output += ",GasPercent:" + String(iaqSensor.gasPercentage);
}

void updateDisplay() {
  displaySensorData();
  //printIAQStatus();
  time_t now;
  struct tm timeinfo;
  time(&now);
  if (now < 24 * 3600) { // Time not set (less than 1 day since epoch)
    Serial.println("Time not set, attempting to synchronize...");
    configTime(0, 0, "pool.ntp.org");
    delay(500); // Small delay to allow time to synchronize
    time(&now);
    if (now < 24 * 3600) {
      Serial.println("Failed to synchronize time.");
      display.println("Time not set");
    } else {
      localtime_r(&now, &timeinfo);
      display.print("");
      printWithLeadingZero(timeinfo.tm_mday);
      display.print('/');
      printWithLeadingZero(timeinfo.tm_mon + 1);
      display.print('/');
      printWithLeadingZero((timeinfo.tm_year + 1900) % 100);
      display.print(" ");
      printWithLeadingZero(timeinfo.tm_hour);
      display.print(':');
      printWithLeadingZero(timeinfo.tm_min);
      display.print(':');
      printWithLeadingZero(timeinfo.tm_sec);
    }
  } else {
    localtime_r(&now, &timeinfo);
    display.print("");
    printWithLeadingZero(timeinfo.tm_mday);
    display.print('/');
    printWithLeadingZero(timeinfo.tm_mon + 1);
    display.print('/');
    printWithLeadingZero((timeinfo.tm_year + 1900) % 100);
    display.print(" ");
    printWithLeadingZero(timeinfo.tm_hour);
    display.print(':');
    printWithLeadingZero(timeinfo.tm_min);
    display.print(':');
    printWithLeadingZero(timeinfo.tm_sec);
  }
  display.setCursor(0, 9);

  //displaySensorData();
}

void displaySensorData() {
  Serial.print("Temperature = ");
  Serial.print(iaqSensor.temperature);
  Serial.println(" °C");
  display.print("Temperature: ");
  display.print(iaqSensor.temperature);
  display.print(" \xF7"); // the ° for temperature
  display.print("C");

  Serial.print("Pressure = ");
  Serial.print(iaqSensor.pressure / 100.0);
  Serial.println(" hPa");
  display.print("Pressure: ");
  display.print(iaqSensor.pressure / 100);
  display.println(" hPa");

  Serial.print("Humidity = ");
  Serial.print(iaqSensor.humidity);
  Serial.println(" %");
  display.print("Humidity: ");
  display.print(iaqSensor.humidity);
  display.println(" %");

  Serial.print("IAQ = ");
  Serial.print(iaqSensor.staticIaq);
  Serial.println(" PPM");
  display.print("IAQ: ");
  display.print(iaqSensor.staticIaq);
  display.println(" PPM");

  Serial.print("CO2 equiv = ");
  Serial.print(iaqSensor.co2Equivalent);
  Serial.println(" PPM");
  display.print("CO2eq: ");
  display.print(iaqSensor.co2Equivalent);
  display.println(" PPM");

  Serial.print("Breath VOC = ");
  Serial.print(iaqSensor.breathVocEquivalent);
  Serial.println(" PPM");
  display.print("VOC: ");
  display.print(iaqSensor.breathVocEquivalent);
  display.println(" PPM");
}

void printIAQStatus() {
  // Check if the data is valid
  if (isnan(iaqSensor.staticIaq) || iaqSensor.staticIaq < 0) {
    IAQsts = "Invalid data";
  } else if ((iaqSensor.staticIaq >= 0) && (iaqSensor.staticIaq <= 50)) {
    IAQsts = "Excellent";
  } else if ((iaqSensor.staticIaq > 50) && (iaqSensor.staticIaq <= 100)) {
    IAQsts = "Good";
  } else if ((iaqSensor.staticIaq > 100) && (iaqSensor.staticIaq <= 150)) {
    IAQsts = "Lightly polluted";
  } else if ((iaqSensor.staticIaq > 150) && (iaqSensor.staticIaq <= 200)) {
    IAQsts = "Moderately polluted";
  } else if ((iaqSensor.staticIaq > 200) && (iaqSensor.staticIaq <= 250)) {
    IAQsts = "Heavily polluted";
  } else if ((iaqSensor.staticIaq > 250) && (iaqSensor.staticIaq <= 350)) {
    IAQsts = "Severely Polluted";
  } else if (iaqSensor.staticIaq > 350) {
    IAQsts = "Extremely polluted";
  } else {
    IAQsts = "Unknown";
  }
 
  Serial.print("Air Quality Index, IAQ: ");
  Serial.println(IAQsts);
  display.print("IAQsts: ");
  display.println(IAQsts);

  
}


// void extractAndSendData() {
//   // Variables to store the extracted values
//   float temperature, humidity, pressure, iaq, co2, voc;

//   // Extracting the values using sscanf
//   sscanf(output.c_str(), "*d,IAQ:%f,IAQAcc:%*d,StaticIAQ:%*f,CO2:%f,VOC:%f,Temp:%f,Pressure:%f,Humidity:%f,GasRes:%*f,StabStatus:%*f,RunInStatus:%*f,CompTemp:%*f,CompHumidity:%*f,GasPercent:%*f",
//          &iaq, &co2, &voc, &temperature, &pressure, &humidity);

//   send_data_to_pi(temperature, humidity, pressure, iaq, co2, voc, IAQsts);
// }

void sendSensorData() {
  // Send the sensor data in a non-blocking way, if possible
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");

    String jsonData = "{\"temperature\":" + String(iaqSensor.temperature) +
                      ", \"humidity\":" + String(iaqSensor.humidity) +
                      ", \"pressure\":" + String(iaqSensor.pressure / 100) +
                      ", \"IAQ\":" + String(iaqSensor.staticIaq) +
                      ", \"carbon\":" + String(iaqSensor.co2Equivalent) +
                      ", \"VOC\":" + String(iaqSensor.breathVocEquivalent) +
                      ", \"IAQsts\":\"" + String(IAQsts) + "\"}";

    int httpResponseCode = http.POST(jsonData);
    // debugging code 
    // if (httpResponseCode > 0) {
    //   String response = http.getString();
    //   Serial.println("HTTP Response code: " + String(httpResponseCode));
    //   Serial.println(response);
    // } else {
    //   Serial.print("Error on sending POST: ");
    //   Serial.println(httpResponseCode);
    // }
    http.end();
  }
}

// for debugging the sensor
void checkIaqSensorStatus(void) {
  if (iaqSensor.bsecStatus != BSEC_OK) {
    if (iaqSensor.bsecStatus < BSEC_OK) {
      output = "BSEC error code : " + String(iaqSensor.bsecStatus);
      Serial.println(output);
      for (;;)
        errLeds(); /* Halt in case of failure */
    } else {
      output = "BSEC warning code : " + String(iaqSensor.bsecStatus);
      Serial.println(output);
    }
  }

  if (iaqSensor.bme68xStatus != BME68X_OK) {
    if (iaqSensor.bme68xStatus < BME68X_OK) {
      output = "BME68X error code : " + String(iaqSensor.bme68xStatus);
      Serial.println(output);
      for (;;)
        errLeds(); /* Halt in case of failure */
    } else {
      output = "BME68X warning code : " + String(iaqSensor.bme68xStatus);
      Serial.println(output);
    }
  }
}

void errLeds(void) {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(100);
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
}

// Helper function to print the date and time components with leading zero
void printWithLeadingZero(int value) {
  if (value < 10) {
    display.print('0');
  }
  display.print(value, DEC);
}
