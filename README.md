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
ESP-ROM:esp32c5-eco2-20250121
Build:Jan 21 2025
rst:0x1 (POWERON),boot:0x59 (SPI_FAST_FLASH_BOOT)
SPI mode:DIO, clock div:1
load:0x408556b0,len:0x1720
load:0x4084bba0,len:0xde0
load:0x4084e5a0,len:0x3198
entry 0x4084bbaa
I (23) boot: ESP-IDF v5.5.2-dirty 2nd stage bootloader
I (23) boot: compile time Mar 18 2026 21:13:03
I (24) boot: chip revision: v1.0
I (25) boot: efuse block revision: v0.3
I (27) boot.esp32c5: SPI Speed      : 80MHz
I (31) boot.esp32c5: SPI Mode       : DIO
I (35) boot.esp32c5: SPI Flash Size : 16MB
I (39) boot: Enabling RNG early entropy source...
I (43) boot: Partition Table:
I (46) boot: ## Label            Usage          Type ST Offset   Length
I (52) boot:  0 nvs              WiFi data        01 02 00009000 00006000
I (59) boot:  1 phy_init         RF data          01 01 0000f000 00001000
I (65) boot:  2 factory          factory app      00 00 00010000 00100000
I (72) boot: End of partition table
I (75) esp_image: segment 0: paddr=00010020 vaddr=420a0020 size=1d19ch (119196) map
I (104) esp_image: segment 1: paddr=0002d1c4 vaddr=40800000 size=02e54h ( 11860) load
I (107) esp_image: segment 2: paddr=00030020 vaddr=42000020 size=9ecb4h (650420) map
I (219) esp_image: segment 3: paddr=000cecdc vaddr=40802e54 size=137b4h ( 79796) load
I (236) esp_image: segment 4: paddr=000e2498 vaddr=40816680 size=0405ch ( 16476) load
I (245) boot: Loaded app from partition at offset 0x10000
I (245) boot: Disabling RNG early entropy source...
I (255) MSPI Timing: Enter flash timing tuning
I (256) cpu_start: Unicore app
I (276) cpu_start: GPIO 12 and 11 are used as console UART I/O pins
I (276) cpu_start: Pro cpu start user code
I (276) cpu_start: cpu freq: 240000000 Hz
I (278) app_init: Application information:
I (282) app_init: Project name:     esp32c5-5g-ap
I (287) app_init: App version:      be153a9-dirty
I (291) app_init: Compile time:     Mar 18 2026 21:13:19
I (296) app_init: ELF file SHA256:  1887d5389...
I (300) app_init: ESP-IDF:          v5.5.2-dirty
I (305) efuse_init: Min chip rev:     v1.0
I (309) efuse_init: Max chip rev:     v1.99 
I (313) efuse_init: Chip rev:         v1.0
I (316) heap_init: Initializing. RAM available for dynamic allocation:
I (323) heap_init: At 40820280 len 0003C320 (240 KiB): RAM
I (328) heap_init: At 4085C5A0 len 00002F58 (11 KiB): RAM
I (333) heap_init: At 50000000 len 00003FE8 (15 KiB): RTCRAM
I (339) spi_flash: detected chip: generic
I (342) spi_flash: flash io: dio
W (345) spi_flash: CPU frequency is set to 240MHz. esp_flash_write_encrypted() will automatically limit CPU frequency to 80MHz during execution.
I (358) sleep_gpio: Configure to isolate all GPIO pins in sleep state
I (364) sleep_gpio: Enable automatic switching of GPIO sleep configuration
I (371) coexist: coex firmware version: 7260f71
I (375) coexist: coexist rom version 78e5c6e42
I (379) main_task: Started on CPU0
I (379) main_task: Calling app_main()
I (399) WiFi_AP: AP IP Address: 192.168.4.1
I (399) pp: pp rom version: 78a72e9d5
I (399) net80211: net80211 rom version: 78a72e9d5
I (409) wifi:wifi driver task: 408264c0, prio:23, stack:6656, core=0
I (419) wifi:wifi firmware version: ee91c8c
I (419) wifi:wifi certification version: v7.0
I (419) wifi:config NVS flash: enabled
I (419) wifi:config nano formatting: disabled
I (419) wifi:mac_version:HAL_MAC_ESP32AX_752MP_ECO2,ut_version:N, band mode:0x3
I (429) wifi:Init data frame dynamic rx buffer num: 32
I (439) wifi:Init static rx mgmt buffer num: 5
I (439) wifi:Init management short buffer num: 32
I (439) wifi:Init dynamic tx buffer num: 32
I (449) wifi:Init static tx FG buffer num: 2
I (449) wifi:Init static rx buffer size: 1700 (rxctrl:64, csi:512)
I (459) wifi:Init static rx buffer num: 10
I (459) wifi:Init dynamic rx buffer num: 32
I (469) wifi_init: rx ba win: 6
I (469) wifi_init: accept mbox: 6
I (469) wifi_init: tcpip mbox: 32
I (469) wifi_init: udp mbox: 6
I (479) wifi_init: tcp mbox: 6
I (479) wifi_init: tcp tx win: 5760
I (479) wifi_init: tcp rx win: 5760
I (489) wifi_init: tcp mss: 1440
I (489) wifi_init: WiFi IRAM OP enabled
I (489) wifi_init: WiFi RX IRAM OP enabled
I (499) wifi_init: WiFi SLP IRAM OP enabled
I (499) phy_init: phy_version 108,cdfe9643,Dec  2 2025,11:38:11
W (1119) wifi:WDEV_RXCCK_DELAY:960
W (1119) wifi:WDEV_RXOFDM_DELAY:260
W (1119) wifi:WDEV_RX_11G_OFDM_DELAY:261
W (1119) wifi:WDEV_TXCCK_DELAY:630
W (1129) wifi:WDEV_TXOFDM_DELAY:94
W (1129) wifi:ACK_TAB0   :0x   90a0b, QAM16:0x9 (24Mbps), QPSK:0xa (12Mbps), BPSK:0xb (6Mbps)
W (1139) wifi:CTS_TAB0   :0x   90a0b, QAM16:0x9 (24Mbps), QPSK:0xa (12Mbps), BPSK:0xb (6Mbps)
W (1149) wifi:WDEVBEAMFORMCONF:0x61d7120, HE_BF_RPT_RA_SET_OPT:1
W (1149) wifi:WDEVVHTBEAMFORMCONF: 0x61d7120, WDEV_VHT_BEAMFORMEE_ENA: 1, WDEV_VHT_NG_SEL: 0
W (1159) wifi:(agc)0x600a7128:0xd21f0c20, min.avgNF:0xce->0xd2(dB), RCalCount:0x1f0, min.RRssi:0xc20(-62.00)
W (1169) wifi:MODEM_SYSCON_WIFI_BB_CFG_REG(0x600a9c18):0x10003802
W (1179) wifi:(phy)rate:0x0(  LP-1Mbps), pwr:20, txing:20
W (1179) wifi:(phy)rate:0x1(  LP-2Mbps), pwr:20, txing:20
W (1189) wifi:(phy)rate:0x2(LP-5.5Mbps), pwr:20, txing:20
W (1189) wifi:(phy)rate:0x3( LP-11Mbps), pwr:20, txing:20
W (1199) wifi:(phy)rate:0x5(  SP-2Mbps), pwr:20, txing:20
W (1199) wifi:(phy)rate:0x6(SP-5.5Mbps), pwr:20, txing:20
W (1209) wifi:(phy)rate:0x7( SP-11Mbps), pwr:20, txing:20
W (1209) wifi:(phy)rate:0x8(    48Mbps), pwr:17, txing:17
W (1219) wifi:(phy)rate:0x9(    24Mbps), pwr:19, txing:19
W (1219) wifi:(phy)rate:0xa(    12Mbps), pwr:19, txing:19
W (1229) wifi:(phy)rate:0xb(     6Mbps), pwr:19, txing:19
W (1229) wifi:(phy)rate:0xc(    54Mbps), pwr:17, txing:17
W (1239) wifi:(phy)rate:0xd(    36Mbps), pwr:19, txing:19
W (1239) wifi:(phy)rate:0xe(    18Mbps), pwr:19, txing:19
W (1249) wifi:(phy)rate:0xf(     9Mbps), pwr:19, txing:19
W (1249) wifi:(phy)rate:0x10, mcs:0x0, pwr(bw20:19, bw40:18), txing:19, HE pwr(bw20:19), txing:19
W (1259) wifi:(phy)rate:0x11, mcs:0x1, pwr(bw20:19, bw40:18), txing:19, HE pwr(bw20:19), txing:19
W (1269) wifi:(phy)rate:0x12, mcs:0x2, pwr(bw20:18, bw40:17), txing:18, HE pwr(bw20:18), txing:18
W (1279) wifi:(phy)rate:0x13, mcs:0x3, pwr(bw20:18, bw40:17), txing:18, HE pwr(bw20:18), txing:18
W (1289) wifi:(phy)rate:0x14, mcs:0x4, pwr(bw20:17, bw40:16), txing:17, HE pwr(bw20:17), txing:17
W (1299) wifi:(phy)rate:0x15, mcs:0x5, pwr(bw20:17, bw40:16), txing:17, HE pwr(bw20:17), txing:17
W (1299) wifi:(phy)rate:0x16, mcs:0x6, pwr(bw20:17, bw40:16), txing:17, HE pwr(bw20:17), txing:17
W (1309) wifi:(phy)rate:0x17, mcs:0x7, pwr(bw20:17, bw40:16), txing:17, HE pwr(bw20:17), txing:17
W (1319) wifi:(phy)rate:0x18, mcs:0x8, pwr(bw20:19, bw40:16), txing:19, HE pwr(bw20:16), txing:16
W (1329) wifi:(phy)rate:0x19, mcs:0x9, pwr(bw20:18, bw40:16), txing:18, HE pwr(bw20:15), txing:15
W (1339) wifi:(hal)co_hosted_bss:0, max_indicator:0, bitmask:0xff, mBSSIDsEnable:0
I (1349) wifi:11ax coex: WDEVAX_PTI0(0x55777555), WDEVAX_PTI1(0x00003377).

I (1349) wifi:mode : sta (3c:dc:75:9a:f3:78) + softAP (3c:dc:75:9a:f3:79)
I (1359) wifi:enable tsf
W (1359) wifi:(BB)enable busy check(0x18), disable idle check(0xaa)
W (1369) wifi:11ax/11ac mode can not work under phy bw 40M, the sta 2G phymode changed to 11N
W (1379) wifi:11ax/11ac mode can not work under phy bw 40M, the sta 5G phymode changed to 11N
I (1439) wifi:Total power save buffer number: 16
I (1439) wifi:Init max length of beacon: 752/752
I (1439) wifi:Init max length of beacon: 752/752
I (1449) esp_netif_lwip: DHCP server started on interface WIFI_AP_DEF with IP: 192.168.4.1
I (1449) WiFi_AP: Wi-Fi AP+STA Started: SSID=ESP32C5_AP
I (1459) main_task: Returned from app_main()
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
