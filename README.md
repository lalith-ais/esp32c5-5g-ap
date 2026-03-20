# ESP32-C5  –  5 GHz SoftAP + BLE NUS Channel Controller

## Overview

Controls the ESP32-C5 WiFi AP channel over BLE using the Nordic UART
Service (NUS) — the standard BLE replacement for Classic BT SPP.

The ESP32-C5 is **BLE-only** (Bluetooth LE Core 6.0, no BR/EDR Classic BT).

## Architecture

```
iMX6 (Debian)                    ESP32-C5
─────────────────                ──────────────────────────────
python3 ble_nus_client.py  <──>  BLE NUS GATT server
                                   │
                                   └─► handle_command()
                                         │
                                         └─► ap_set_channel()
                                               │
                                               └─► WiFi AP (5 GHz)

UART0 (USB/debug)          <──>  cli_task() ──► handle_command()
```

## NUS UUIDs

| Role    | UUID                                   |
|---------|----------------------------------------|
| Service | 6E400001-B5A3-F393-E0A9-E50E24DCCA9E  |
| RX      | 6E400002-B5A3-F393-E0A9-E50E24DCCA9E  |
| TX      | 6E400003-B5A3-F393-E0A9-E50E24DCCA9E  |

## Build & Flash (ESP32-C5)

```bash
cd esp32c5-ble-uart
rm -f sdkconfig
idf.py fullclean
idf.py set-target esp32c5
idf.py build
idf.py flash monitor
```

Expected boot output:
```
AP_CLI:  WiFi AP started  SSID=ESP32C5_AP  ch=40 (5GHz)
BLE_NUS: BLE NUS init done – will advertise as 'ESP32C5_NUS'
BLE_NUS: BLE advertising started as 'ESP32C5_NUS'
AP_CLI:  All systems up: WiFi AP + BLE NUS + CLI
```

## iMX6 Client Setup (Debian)

```bash
pip install bleak

# Auto-scan and connect
python3 ble_nus_client.py

# Or connect by MAC address directly
python3 ble_nus_client.py AA:BB:CC:DD:EE:FF
```

## Commands

```
ch <n>   Switch AP channel
         2.4GHz: 1 2 3 4 5 6 7 8 9 10 11 12 13
         5GHz:   36 40 44 48 149 153 157 161
status   Show current SSID, channel and band
help     List commands
```

## Example Session

```
$ python3 ble_nus_client.py
Scanning for 'ESP32C5_NUS'...
Found: ESP32C5_NUS  [AA:BB:CC:DD:EE:FF]
Connecting to AA:BB:CC:DD:EE:FF ...
Connected. MTU=247

ESP32-C5 BLE UART ready. Type 'help'

status
SSID=ESP32C5_AP  ch=40  band=5GHz

ch 36
OK ch 36 5GHz

ch 6
OK ch 6 2.4GHz
```
