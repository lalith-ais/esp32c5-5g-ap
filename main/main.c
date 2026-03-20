/*
 * main.c  –  ESP32-C5  :  5 GHz SoftAP  +  BLE NUS UART controller
 *
 * The WiFi AP channel is controlled via two interfaces:
 *   1. UART0      (USB / debug port, 115200 baud)
 *   2. BLE NUS    (Nordic UART Service – connect from iMX6 using bleak)
 *
 * Commands:
 *   ch <n>   switch AP channel  (2.4GHz: 1-13 / 5GHz: 36 40 44 48 149 153 157 161)
 *   status   print current SSID, channel, band
 *   help     list commands
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/uart.h"

#include "ble_nus.h"

#define TAG             "AP_CLI"

/* ── AP config ───────────────────────────────────────────────────────── */
#define WIFI_SSID       "ESP32C5_AP"
#define WIFI_PASS       "yourpassword"
#define WIFI_CHANNEL    40
#define MAX_STA_CONN    4

/* ── UART0 CLI ───────────────────────────────────────────────────────── */
#define CLI_UART        UART_NUM_0
#define CLI_BAUD        115200
#define CLI_BUF_SIZE    256
#define CLI_TASK_STACK  4096

/* ── Valid channels ──────────────────────────────────────────────────── */
static const uint8_t CHANNELS_2G[] = {1,2,3,4,5,6,7,8,9,10,11,12,13};
static const uint8_t CHANNELS_5G[] = {36,40,44,48,149,153,157,161};
static uint8_t s_current_channel   = WIFI_CHANNEL;

/* ─────────────────────────────────────────────────────────────────────── */
/*  cli_send() – writes to UART0 AND back over BLE NUS to the iMX6.      */
/*  ble_nus_send() is a no-op when no BLE client is connected.            */
/* ─────────────────────────────────────────────────────────────────────── */
static void cli_send(const char *msg)
{
    uart_write_bytes(CLI_UART, msg, strlen(msg));
    ble_nus_send(msg);
}

/* ── Helpers ─────────────────────────────────────────────────────────── */
static bool is_valid_channel(uint8_t ch)
{
    for (size_t i = 0; i < sizeof(CHANNELS_2G); i++)
        if (CHANNELS_2G[i] == ch) return true;
    for (size_t i = 0; i < sizeof(CHANNELS_5G); i++)
        if (CHANNELS_5G[i] == ch) return true;
    return false;
}

static const char *band_of(uint8_t ch)
{
    return (ch >= 36) ? "5GHz" : "2.4GHz";
}

/* ── AP channel switch ───────────────────────────────────────────────── */
static esp_err_t ap_set_channel(uint8_t ch)
{
    esp_err_t err;

    err = esp_wifi_stop();
    if (err != ESP_OK) return err;

    wifi_config_t cfg;
    err = esp_wifi_get_config(WIFI_IF_AP, &cfg);
    if (err != ESP_OK) { esp_wifi_start(); return err; }

    cfg.ap.channel = ch;
    if (ch >= 36) {
        cfg.ap.authmode         = strlen(WIFI_PASS) == 0
                                  ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_WPA3_PSK;
        cfg.ap.pmf_cfg.required = true;
    } else {
        cfg.ap.authmode         = strlen(WIFI_PASS) == 0
                                  ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
        cfg.ap.pmf_cfg.required = false;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    if (err != ESP_OK) { esp_wifi_start(); return err; }

    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    s_current_channel = ch;
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────────────────── */
/*  handle_command()                                                        */
/*  Non-static – called from cli_task() here AND from ble_nus.c           */
/* ─────────────────────────────────────────────────────────────────────── */
void handle_command(char *line)
{
    int len = strlen(line);
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n'
                        || line[len-1] == ' '))
        line[--len] = '\0';
    if (len == 0) return;

    if (strncmp(line, "ch ", 3) == 0) {
        int n = atoi(line + 3);
        if (n <= 0 || n > 255) {
            cli_send("ERR: channel out of range\r\n");
            return;
        }
        uint8_t ch = (uint8_t)n;
        if (!is_valid_channel(ch)) {
            cli_send("ERR: invalid channel. "
                     "2.4GHz=1-13  5GHz=36,40,44,48,149,153,157,161\r\n");
            return;
        }
        ESP_LOGI(TAG, "Switching to channel %d (%s)", ch, band_of(ch));
        esp_err_t err = ap_set_channel(ch);
        if (err == ESP_OK) {
            char buf[64];
            snprintf(buf, sizeof(buf), "OK ch %d %s\r\n", ch, band_of(ch));
            cli_send(buf);
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "ERR: wifi restart 0x%x\r\n", err);
            cli_send(buf);
        }

    } else if (strcmp(line, "status") == 0) {
        char buf[80];
        snprintf(buf, sizeof(buf), "SSID=%s  ch=%d  band=%s\r\n",
                 WIFI_SSID, s_current_channel, band_of(s_current_channel));
        cli_send(buf);

    } else if (strcmp(line, "help") == 0) {
        cli_send("Commands:\r\n"
                 "  ch <n>   Switch AP channel\r\n"
                 "           2.4GHz: 1-13\r\n"
                 "           5GHz:   36 40 44 48 149 153 157 161\r\n"
                 "  status   Current channel and band\r\n"
                 "  help     This message\r\n");
    } else {
        cli_send("ERR: unknown command. Type 'help'\r\n");
    }
}

/* ── UART0 CLI task ──────────────────────────────────────────────────── */
static void cli_task(void *arg)
{
    char buf[CLI_BUF_SIZE];
    int  pos = 0;

    cli_send("\r\nESP32-C5 AP+BLE CLI ready. Type 'help'\r\n");

    while (1) {
        uint8_t c;
        int n = uart_read_bytes(CLI_UART, &c, 1, pdMS_TO_TICKS(20));
        if (n <= 0) continue;

        if (c == '\n' || c == '\r') {
            if (pos > 0) {
                buf[pos] = '\0';
                handle_command(buf);
                pos = 0;
            }
        } else if (c == 127 || c == '\b') {
            if (pos > 0) pos--;
        } else {
            if (pos < CLI_BUF_SIZE - 1)
                buf[pos++] = (char)c;
        }
    }
}

/* ── WiFi event handler ──────────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base != WIFI_EVENT) return;
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = data;
        ESP_LOGI(TAG, "WiFi client joined  AID=%d", e->aid);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = data;
        ESP_LOGI(TAG, "WiFi client left  AID=%d", e->aid);
    }
}

/* ── WiFi SoftAP init ────────────────────────────────────────────────── */
static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    esp_netif_ip_info_t ip_info;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(ap_netif, &ip_info));
    ESP_LOGI(TAG, "AP IP: " IPSTR, IP2STR(&ip_info.ip));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid           = WIFI_SSID,
            .ssid_len       = strlen(WIFI_SSID),
            .password       = WIFI_PASS,
            .channel        = WIFI_CHANNEL,
            .authmode       = WIFI_AUTH_WPA2_WPA3_PSK,
            .max_connection = MAX_STA_CONN,
            .pmf_cfg        = { .required = true },
        },
    };
    if (strlen(WIFI_PASS) == 0)
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    /* APSTA required for 5 GHz on ESP32-C5 */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started  SSID=%s  ch=%d (%s)",
             WIFI_SSID, WIFI_CHANNEL, band_of(WIFI_CHANNEL));
}

/* ── UART0 init ──────────────────────────────────────────────────────── */
static void uart_cli_init(void)
{
    uart_config_t uc = {
        .baud_rate  = CLI_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(CLI_UART, &uc));
    ESP_ERROR_CHECK(uart_driver_install(CLI_UART, CLI_BUF_SIZE * 2,
                                        CLI_BUF_SIZE * 2, 0, NULL, 0));
}

/* ── Entry point ─────────────────────────────────────────────────────── */
void app_main(void)
{
    /* NVS – required by WiFi and BT drivers */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 1. WiFi SoftAP (5 GHz, APSTA mode) */
    wifi_init_softap();

    /* 2. BLE NUS server */
    ESP_ERROR_CHECK(ble_nus_init());

    /* 3. UART0 CLI task */
    uart_cli_init();
    xTaskCreate(cli_task, "cli", CLI_TASK_STACK, NULL, 5, NULL);

    ESP_LOGI(TAG, "All systems up: WiFi AP + BLE NUS + CLI");
}
