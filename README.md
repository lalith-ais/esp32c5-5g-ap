# ESP32-C5 5GHz SoftAP — ESP-IDF Example

A minimal, working example of a **5GHz Wi-Fi Access Point** on the **ESP32-C5** using ESP-IDF.

This repo exists because getting 5GHz SoftAP working on the ESP32-C5 is not obvious — the official docs and most examples online don't cover it clearly. Hopefully this saves you the debugging time.

---

## The Key Discovery

If you just want to know the trick and move on:

```c
// ❌ This does NOT enable the 5GHz radio on ESP32-C5
esp_wifi_set_mode(WIFI_MODE_AP);

// ✅ This is required for 5GHz SoftAP to work
esp_wifi_set_mode(WIFI_MODE_APSTA);
```

**Pure `WIFI_MODE_AP` does not bring up the 5GHz radio on the ESP32-C5. You must use `WIFI_MODE_APSTA`.**

Everything else is standard ESP-IDF Wi-Fi setup. No special band config, no `esp_wifi_set_band()`, no custom country code needed.

---

## What Doesn't Work (and Why)

If you've been searching for this, you've probably tried some of these:

| Attempt | Result |
|---|---|
| `WIFI_MODE_AP` with a 5GHz channel | Silently falls back to 2.4GHz or fails |
| `esp_wifi_set_band(WIFI_BAND_5G)` | `band_mode` field doesn't exist in current IDF |
| `wifi_country_t.schan = 36` | `ESP_ERR_INVALID_ARG` — schan is for 2.4GHz only |
| `.band_mode = WIFI_BAND_MODE_5G_ONLY` | Compile error — field doesn't exist |

---

## Requirements

| | |
|---|---|
| **Chip** | ESP32-C5 |
| **ESP-IDF** | v5.5.2 (tested) |
| **Flash** | 16MB |

---

## Configuration

Edit the defines at the top of `main/main.c`:

```c
#define WIFI_SSID      "ESP32C5_AP"     // Your AP name
#define WIFI_PASS      "yourpassword"   // Min 8 chars, or "" for open network
#define WIFI_CHANNEL   40               // Any 5GHz channel (see table below)
#define MAX_STA_CONN   4                // Max simultaneous clients
```

### 5GHz Channel Reference

| Channel | Frequency |
|---|---|
| 36 | 5180 MHz |
| 40 | 5200 MHz |
| 44 | 5220 MHz |
| 48 | 5240 MHz |
| 149 | 5745 MHz |
| 153 | 5765 MHz |
| 157 | 5785 MHz |
| 161 | 5805 MHz |

> Channel availability varies by country. Check your local regulations.

---

## Build & Flash

```bash
# Source ESP-IDF
. $IDF_PATH/export.sh

# Set target
idf.py set-target esp32c5

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## Expected Output

```
I (342) WiFi_AP: AP IP Address: 192.168.4.1
I (891) WiFi_AP: 5GHz SoftAP started  SSID: ESP32C5_AP  Channel: 40
```

When a client connects:
```
I (4123) WiFi_AP: Station joined  AID=1
```

---

## Project Structure

```
esp32c5-5g-ap/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    └── main.c
```

---

## Tested On

- ESP32-C5 devkit
- ESP-IDF v5.5.2
- Verified visible and connectable on 5GHz from Android and Windows clients

---

## License

Public domain / CC0. Do whatever you want with it.
