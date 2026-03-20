#pragma once

/*
 * ble_nus.h  –  BLE Nordic UART Service (NUS) server for ESP32-C5
 *
 * NUS is the standard BLE replacement for Classic BT SPP.
 * It exposes two GATT characteristics:
 *
 *   RX  (write)   – iMX6 writes commands TO the ESP32-C5
 *   TX  (notify)  – ESP32-C5 sends responses BACK to the iMX6
 *
 * Standard NUS UUIDs (same ones used by Nordic, Zephyr, BTstack, bleak):
 *   Service  : 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 *   RX char  : 6E400002-B5A3-F393-E0A9-E50E24DCCA9E  (write)
 *   TX char  : 6E400003-B5A3-F393-E0A9-E50E24DCCA9E  (notify)
 *
 * On the iMX6 side, connect with:
 *   pip install bleak
 *   python3 ble_nus_client.py   (see project README)
 * or use gatttool / bluetoothctl directly.
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialise Bluedroid BLE stack and start the NUS GATT server.
 *         Call once from app_main() after nvs_flash_init().
 * @return ESP_OK on success.
 */
esp_err_t ble_nus_init(void);

/**
 * @brief  Send a NUL-terminated string to the connected iMX6 client
 *         via BLE NUS TX notification.
 *         No-op if no client is connected or notifications not enabled.
 */
void ble_nus_send(const char *msg);

#ifdef __cplusplus
}
#endif
