#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise Bluedroid, configure and start BLE advertising.
 *        Call once after nvs_flash_init() and WiFi init.
 *        Runs entirely via GAP callbacks – no blocking task needed.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t ble_beacon_init(void);

#ifdef __cplusplus
}
#endif
