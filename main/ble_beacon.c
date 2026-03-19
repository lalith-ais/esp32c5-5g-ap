/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * BLE beacon – extracted from standalone example and adapted to run
 * alongside the 5GHz SoftAP + UART CLI on the same ESP32-C5.
 *
 * No changes to the core BLE logic from the original tested code.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include "esp_log.h"
#include "esp_bt_defs.h"
#include "ble_beacon.h"

#define ADV_CONFIG_FLAG      (1 << 0)
#define SCAN_RSP_CONFIG_FLAG (1 << 1)
#define URI_PREFIX_HTTPS     (0x17)

static const char *TAG = "BLE_BEACON";
static const char device_name[] = "Bluedroid_Beacon";

static uint8_t adv_config_done = 0;
static esp_bd_addr_t local_addr;
static uint8_t local_addr_type;

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,   /* 20 ms */
    .adv_int_max        = 0x20,   /* 20 ms */
    .adv_type           = ADV_TYPE_SCAN_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t adv_raw_data[] = {
    0x02, ESP_BLE_AD_TYPE_FLAG,       0x06,
    0x11, ESP_BLE_AD_TYPE_NAME_CMPL,  'B','l','u','e','d','r','o','i','d','_','B','e','a','c','o','n',
    0x02, ESP_BLE_AD_TYPE_TX_PWR,     0x09,
    0x03, ESP_BLE_AD_TYPE_APPEARANCE, 0x00, 0x02,
    0x02, ESP_BLE_AD_TYPE_LE_ROLE,    0x00,
};

/* Bytes [2..7] are filled with the local BT address before advertising starts */
static uint8_t scan_rsp_raw_data[] = {
    0x08, ESP_BLE_AD_TYPE_LE_DEV_ADDR, 0x00,0x00,0x00,0x00,0x00,0x00, 0x00,
    0x11, ESP_BLE_AD_TYPE_URI, URI_PREFIX_HTTPS,
          '/','/', 'e','s','p','r','e','s','s','i','f','.','c','o','m',
};

/* ── GAP callback (unchanged from original) ─────────────────────────── */

static void esp_gap_cb(esp_gap_ble_cb_event_t event,
                       esp_ble_gap_cb_param_t *param)
{
    switch (event) {

    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Adv data set, status %d", param->adv_data_cmpl.status);
        adv_config_done &= (~ADV_CONFIG_FLAG);
        if (adv_config_done == 0)
            esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Adv raw data set, status %d", param->adv_data_raw_cmpl.status);
        adv_config_done &= (~ADV_CONFIG_FLAG);
        if (adv_config_done == 0)
            esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Scan rsp data set, status %d", param->scan_rsp_data_cmpl.status);
        adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
        if (adv_config_done == 0)
            esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Scan rsp raw data set, status %d",
                 param->scan_rsp_data_raw_cmpl.status);
        adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
        if (adv_config_done == 0)
            esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Advertising start failed, status %d",
                     param->adv_start_cmpl.status);
            break;
        }
        ESP_LOGI(TAG, "BLE advertising started successfully");
        break;

    default:
        break;
    }
}

/* ── Public init function ────────────────────────────────────────────── */

esp_err_t ble_beacon_init(void)
{
    esp_err_t ret;

    /* Release Classic BT memory – we only need BLE */
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mem_release classic BT: %s (may already be released)",
                 esp_err_to_name(ret));
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bt controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bt controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gap register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gap_set_device_name(device_name);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set device name failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Embed local BT address into the scan response */
    ret = esp_ble_gap_get_local_used_addr(local_addr, &local_addr_type);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "get local addr failed: %s", esp_err_to_name(ret));
        return ret;
    }
    scan_rsp_raw_data[2] = local_addr[5];
    scan_rsp_raw_data[3] = local_addr[4];
    scan_rsp_raw_data[4] = local_addr[3];
    scan_rsp_raw_data[5] = local_addr[2];
    scan_rsp_raw_data[6] = local_addr[1];
    scan_rsp_raw_data[7] = local_addr[0];

    /* Queue both adv data and scan response – advertising starts once
       both ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT and
       ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT have fired. */
    adv_config_done |= ADV_CONFIG_FLAG;
    adv_config_done |= SCAN_RSP_CONFIG_FLAG;

    ret = esp_ble_gap_config_adv_data_raw(adv_raw_data, sizeof(adv_raw_data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "config adv data failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gap_config_scan_rsp_data_raw(scan_rsp_raw_data,
                                               sizeof(scan_rsp_raw_data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "config scan rsp failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "BLE beacon init done – advertising as '%s'", device_name);
    return ESP_OK;
}
