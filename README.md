# ESP32 BME680 IAQ + OLED + RPi server

ESP32 reads a **BME680** (BSEC library), shows data on a small I²C OLED, and POSTs sensor data to a Raspberry Pi server. The RPi serves a webpage with the live values.

## What’s in this repo
- `ESP32_BME680_1.ino` – ESP32 sketch
- `pi_server` – notes/commands for the old RPi server + service

## Wiring (I²C)
Both the OLED and BME680 share the I²C bus.

![Wiring diagram](wiring.svg)

| Signal | ESP32 pin | Notes |
|---|---|---|
| SDA | GPIO 21 | I²C data |
| SCL | GPIO 22 | I²C clock |
| 3V3 | 3.3V | Power for BME680 + OLED (check your OLED voltage) |
| GND | GND | Common ground |

**OLED:** SH1106 @ `0x3C` (default)

**BME680:** uses `BME68X_I2C_ADDR_HIGH` in the sketch

## ESP32 sketch config (placeholders)
Update these in `ESP32_BME680_1.ino`:
```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* serverName = "http://<rpi-ip>:3000/sensor-data";
```

## RPi server notes
The ESP32 posts JSON to the RPi at `/sensor-data` on port 3000. The old RPi serves the page that shows the values.

The `pi_server` file includes:
- stopping Apache/nginx
- starting/restarting the `ESP32_IAQ.service`
- iptables rule to redirect port 80 → 3000
- verification commands

### ChatGPT setup link
Used to set up the service on the RPi:
https://chatgpt.com/share/6988bd14-832c-8006-978f-0f99e196c184

## JSON payload sent
```json
{
  "temperature": <float>,
  "humidity": <float>,
  "pressure": <float>,
  "IAQ": <float>,
  "carbon": <float>,
  "VOC": <float>,
  "IAQsts": "<string>"
}
```

## Notes
- Display is SH1106 128x64 via `Adafruit_SH110X`.
- Time sync uses `pool.ntp.org` with CET/CEST TZ config.
