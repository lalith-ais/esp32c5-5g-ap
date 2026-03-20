/*
 * ble_nus.c  –  BLE Nordic UART Service (NUS) GATT server  –  ESP32-C5
 */

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_gatt_common_api.h"

#include "ble_nus.h"

#define TAG         "BLE_NUS"
#define NUS_IDX_MAX 6          /* number of GATT attribute handles needed */

/* ── NUS 128-bit UUIDs (little-endian) ───────────────────────────────── */

static esp_bt_uuid_t nus_rx_uuid = {
    .len = ESP_UUID_LEN_128,
    .uuid.uuid128 = {
        0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,
        0x93,0xF3,0xA3,0xB5,0x02,0x00,0x40,0x6E
    }
};

static esp_bt_uuid_t nus_tx_uuid = {
    .len = ESP_UUID_LEN_128,
    .uuid.uuid128 = {
        0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,
        0x93,0xF3,0xA3,0xB5,0x03,0x00,0x40,0x6E
    }
};

static esp_bt_uuid_t cccd_uuid = {
    .len         = ESP_UUID_LEN_16,
    .uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG
};

/* ── Service ID ──────────────────────────────────────────────────────── */
static esp_gatt_srvc_id_t nus_srvc_id = {
    .is_primary = true,
    .id = {
        .inst_id = 0,
        .uuid = {
            .len = ESP_UUID_LEN_128,
            .uuid.uuid128 = {
                0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,
                0x93,0xF3,0xA3,0xB5,0x01,0x00,0x40,0x6E
            }
        }
    }
};

/* ── Attribute handles ───────────────────────────────────────────────── */
static uint16_t s_svc_handle    = 0;
static uint16_t s_rx_handle     = 0;   /* RX char value handle            */
static uint16_t s_tx_handle     = 0;   /* TX char value handle            */
static uint16_t s_cccd_handle   = 0;   /* TX CCCD descriptor handle       */

/* ── Build state — use a simple enum instead of UUID byte inspection ─── */
typedef enum {
    NUS_BUILD_IDLE = 0,
    NUS_BUILD_RX_ADDED,     /* RX char added, now adding TX                */
    NUS_BUILD_TX_ADDED,     /* TX char added, now adding CCCD              */
    NUS_BUILD_DONE          /* all attributes built                         */
} nus_build_state_t;

static nus_build_state_t s_build = NUS_BUILD_IDLE;

/* ── Connection state ────────────────────────────────────────────────── */
static uint16_t s_gatts_if  = ESP_GATT_IF_NONE;
static uint16_t s_conn_id   = 0xFFFF;
static bool     s_notify_en = false;

/* ── Line accumulation ───────────────────────────────────────────────── */
#define LINE_BUF_SIZE 256
static char s_line[LINE_BUF_SIZE];
static int  s_line_pos = 0;

/* ── Shared CLI handler (main.c) ─────────────────────────────────────── */
extern void handle_command(char *line);

/* ── GAP advertising ─────────────────────────────────────────────────── */
#define DEVICE_NAME "ESP32C5_NUS"

static esp_ble_adv_params_t adv_params = {
    .adv_int_min       = 0x20,
    .adv_int_max       = 0x40,
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t adv_raw[] = {
    0x02, ESP_BLE_AD_TYPE_FLAG, 0x06,
    0x0C, ESP_BLE_AD_TYPE_NAME_CMPL,
          'E','S','P','3','2','C','5','_','N','U','S','\0',
};

/* ── Public: send string to iMX6 via TX notification ────────────────── */

void ble_nus_send(const char *msg)
{
    if (!s_notify_en || s_conn_id == 0xFFFF ||
        s_gatts_if == ESP_GATT_IF_NONE ||
        s_tx_handle == 0 || msg == NULL) return;

    size_t len = strlen(msg);
    while (len > 0) {
        size_t chunk = (len > 244) ? 244 : len;
        esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
                                    s_tx_handle,
                                    (uint16_t)chunk,
                                    (uint8_t *)msg, false);
        msg += chunk;
        len -= chunk;
    }
}

/* ── RX data → line buffer → handle_command() ───────────────────────── */

static void process_rx(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n' || c == '\r') {
            if (s_line_pos > 0) {
                s_line[s_line_pos] = '\0';
                handle_command(s_line);
                s_line_pos = 0;
            }
        } else if (c == 127 || c == '\b') {
            if (s_line_pos > 0) s_line_pos--;
        } else {
            if (s_line_pos < LINE_BUF_SIZE - 1)
                s_line[s_line_pos++] = c;
        }
    }
}

/* ── GATTS event handler ─────────────────────────────────────────────── */

static void gatts_cb(esp_gatts_cb_event_t event,
                     esp_gatt_if_t gatts_if,
                     esp_ble_gatts_cb_param_t *param)
{
    switch (event) {

    /* ── Step 1: app registered → create NUS service ── */
    case ESP_GATTS_REG_EVT:
        if (param->reg.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "GATTS reg failed status=%d", param->reg.status);
            break;
        }
        s_gatts_if = gatts_if;
        s_build    = NUS_BUILD_IDLE;
        ESP_LOGI(TAG, "GATTS registered, creating NUS service");
        esp_ble_gap_set_device_name(DEVICE_NAME);
        esp_ble_gap_config_adv_data_raw(adv_raw, sizeof(adv_raw));
        esp_ble_gatts_create_service(gatts_if, &nus_srvc_id, NUS_IDX_MAX);
        break;

    /* ── Step 2: service created → start it, add RX char ── */
    case ESP_GATTS_CREATE_EVT:
        if (param->create.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Create service failed status=%d",
                     param->create.status);
            break;
        }
        s_svc_handle = param->create.service_handle;
        ESP_LOGI(TAG, "NUS service created handle=0x%04x", s_svc_handle);
        esp_ble_gatts_start_service(s_svc_handle);
        /* Add RX characteristic (write) */
        {
            esp_attr_value_t val = {
                .attr_max_len = 244, .attr_len = 0, .attr_value = NULL
            };
            esp_ble_gatts_add_char(s_svc_handle,
                                   &nus_rx_uuid,
                                   ESP_GATT_PERM_WRITE,
                                   ESP_GATT_CHAR_PROP_BIT_WRITE |
                                   ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                                   &val, NULL);
        }
        break;

    /* ── Steps 3 & 4: char added — use build state, not UUID bytes ── */
    case ESP_GATTS_ADD_CHAR_EVT:
        if (param->add_char.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Add char failed status=%d",
                     param->add_char.status);
            break;
        }

        if (s_build == NUS_BUILD_IDLE) {
            /* RX was just added → now add TX */
            s_rx_handle = param->add_char.attr_handle;
            s_build     = NUS_BUILD_RX_ADDED;
            ESP_LOGI(TAG, "RX char added handle=0x%04x → adding TX char",
                     s_rx_handle);
            esp_attr_value_t val = {
                .attr_max_len = 244, .attr_len = 0, .attr_value = NULL
            };
            esp_ble_gatts_add_char(s_svc_handle,
                                   &nus_tx_uuid,
                                   ESP_GATT_PERM_READ,
                                   ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                                   &val, NULL);

        } else if (s_build == NUS_BUILD_RX_ADDED) {
            /* TX was just added → now add CCCD descriptor */
            s_tx_handle = param->add_char.attr_handle;
            s_build     = NUS_BUILD_TX_ADDED;
            ESP_LOGI(TAG, "TX char added handle=0x%04x → adding CCCD",
                     s_tx_handle);
            esp_ble_gatts_add_char_descr(s_svc_handle,
                                         &cccd_uuid,
                                         ESP_GATT_PERM_READ |
                                         ESP_GATT_PERM_WRITE,
                                         NULL, NULL);
        }
        break;

    /* ── Step 5: CCCD added — NUS profile complete ── */
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        if (param->add_char_descr.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Add CCCD failed status=%d",
                     param->add_char_descr.status);
            break;
        }
        s_cccd_handle = param->add_char_descr.attr_handle;
        s_build       = NUS_BUILD_DONE;
        ESP_LOGI(TAG, "CCCD added handle=0x%04x – NUS fully ready",
                 s_cccd_handle);
        break;

    /* ── Client connected ── */
    case ESP_GATTS_CONNECT_EVT:
        s_conn_id   = param->connect.conn_id;
        s_notify_en = false;
        s_line_pos  = 0;
        ESP_LOGI(TAG, "iMX6 connected  conn_id=%d", s_conn_id);
        esp_ble_gatt_set_local_mtu(247);
        break;

    /* ── Client disconnected → restart advertising ── */
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "iMX6 disconnected, restarting advertising");
        s_conn_id   = 0xFFFF;
        s_notify_en = false;
        s_line_pos  = 0;
        esp_ble_gap_start_advertising(&adv_params);
        break;

    /* ── Client wrote to RX char or CCCD ── */
    case ESP_GATTS_WRITE_EVT:
        if (!param->write.is_prep) {
            if (param->write.handle == s_cccd_handle) {
                /* CCCD write — enable/disable notifications */
                if (param->write.len == 2) {
                    uint16_t val = param->write.value[0] |
                                  (param->write.value[1] << 8);
                    s_notify_en = (val == 0x0001);
                    ESP_LOGI(TAG, "Notifications %s",
                             s_notify_en ? "enabled" : "disabled");
                    if (s_notify_en)
                        ble_nus_send("\r\nESP32-C5 BLE UART ready. "
                                     "Type 'help'\r\n");
                }
            } else if (param->write.handle == s_rx_handle ||
                       param->write.handle == s_rx_handle + 1) {
                /* RX data from iMX6 */
                ESP_LOGI(TAG, "RX %d bytes", param->write.len);
                process_rx(param->write.value, param->write.len);
            }
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if,
                                            param->write.conn_id,
                                            param->write.trans_id,
                                            ESP_GATT_OK, NULL);
            }
        }
        break;

    /* ── MTU negotiated ── */
    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(TAG, "MTU set to %d", param->mtu.mtu);
        break;

    default:
        break;
    }
}

/* ── GAP event handler ───────────────────────────────────────────────── */

static void gap_cb(esp_gap_ble_cb_event_t event,
                   esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Adv data ready, starting advertising");
        esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
            ESP_LOGE(TAG, "Advertising start failed");
        else
            ESP_LOGI(TAG, "BLE advertising started as '%s'", DEVICE_NAME);
        break;

    default:
        break;
    }
}

/* ── Public init ─────────────────────────────────────────────────────── */

esp_err_t ble_nus_init(void)
{
    esp_err_t ret;

    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK)
        ESP_LOGW(TAG, "mem_release classic BT: %s (expected on C5)",
                 esp_err_to_name(ret));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init: %s", esp_err_to_name(ret));
        return ret;
    }

    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_BLE)) != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_bluedroid_config_t bd_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bluedroid_init_with_cfg(&bd_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init: %s", esp_err_to_name(ret));
        return ret;
    }

    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable: %s", esp_err_to_name(ret));
        return ret;
    }

    if ((ret = esp_ble_gap_register_callback(gap_cb)) != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback: %s", esp_err_to_name(ret));
        return ret;
    }

    if ((ret = esp_ble_gatts_register_callback(gatts_cb)) != ESP_OK) {
        ESP_LOGE(TAG, "GATTS callback: %s", esp_err_to_name(ret));
        return ret;
    }

    if ((ret = esp_ble_gatts_app_register(0)) != ESP_OK) {
        ESP_LOGE(TAG, "GATTS app register: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "BLE NUS init done – will advertise as '%s'", DEVICE_NAME);
    return ESP_OK;
}
