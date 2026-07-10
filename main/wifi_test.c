#include "wifi_test.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

#define WIFI_TEST_GOT_IPV4_BIT BIT0
#define WIFI_TEST_DISCONNECTED_BIT BIT1
#define WIFI_TEST_DETAIL_SIZE 96

static bool s_run_attempted;
static test_result_t s_result = TEST_RESULT_FAIL;
static char s_detail[WIFI_TEST_DETAIL_SIZE] = "not run";

static EventGroupHandle_t s_event_group;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static bool s_wifi_event_registered;
static bool s_ip_event_registered;
static esp_ip4_addr_t s_assigned_ip;
static uint8_t s_disconnect_reason;

static void set_esp_error(const char *operation, esp_err_t err)
{
    s_result = TEST_RESULT_FAIL;
    snprintf(s_detail, sizeof(s_detail), "%s: %s", operation, esp_err_to_name(err));
}

static const char *disconnect_reason_name(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
        return "AP not found";
    case WIFI_REASON_AUTH_FAIL:
        return "authentication failed";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "handshake timeout";
    case WIFI_REASON_ASSOC_FAIL:
        return "association failed";
    case WIFI_REASON_CONNECTION_FAIL:
        return "connection failed";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "beacon timeout";
    default:
        return "disconnected";
    }
}

static void wifi_disconnected_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_id;

    const wifi_event_sta_disconnected_t *event = event_data;
    s_disconnect_reason = (event != NULL) ? event->reason : 0;
    xEventGroupSetBits(s_event_group, WIFI_TEST_DISCONNECTED_BIT);
}

static void got_ip_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_id;

    const ip_event_got_ip_t *event = event_data;
    if (event != NULL) {
        s_assigned_ip = event->ip_info.ip;
        xEventGroupSetBits(s_event_group, WIFI_TEST_GOT_IPV4_BIT);
    }
}

static void cleanup_event_wait(void)
{
    if (s_ip_event_registered) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_event_instance);
        s_ip_event_registered = false;
    }

    if (s_wifi_event_registered) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, s_wifi_event_instance);
        s_wifi_event_registered = false;
    }

    if (s_event_group != NULL) {
        vEventGroupDelete(s_event_group);
        s_event_group = NULL;
    }
}

static bool prepare_wifi_config(wifi_config_t *wifi_config)
{
    size_t ssid_length = strlen(CONFIG_MODULINO_WIFI_SSID);
    size_t password_length = strlen(CONFIG_MODULINO_WIFI_PASSWORD);

    if (ssid_length > sizeof(wifi_config->sta.ssid)) {
        s_result = TEST_RESULT_FAIL;
        snprintf(s_detail, sizeof(s_detail), "SSID exceeds %u bytes", (unsigned int)sizeof(wifi_config->sta.ssid));
        return false;
    }

    if (password_length > sizeof(wifi_config->sta.password)) {
        s_result = TEST_RESULT_FAIL;
        snprintf(s_detail, sizeof(s_detail), "password exceeds %u bytes", (unsigned int)sizeof(wifi_config->sta.password));
        return false;
    }

    memset(wifi_config, 0, sizeof(*wifi_config));
    memcpy(wifi_config->sta.ssid, CONFIG_MODULINO_WIFI_SSID, ssid_length);
    memcpy(wifi_config->sta.password, CONFIG_MODULINO_WIFI_PASSWORD, password_length);
    wifi_config->sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config->sta.pmf_cfg.capable = true;
    wifi_config->sta.pmf_cfg.required = false;
    return true;
}

void wifi_test_run(void)
{
    if (s_run_attempted) {
        return;
    }
    s_run_attempted = true;

    if (CONFIG_MODULINO_WIFI_SSID[0] == '\0') {
        s_result = TEST_RESULT_SKIP;
        snprintf(s_detail, sizeof(s_detail), "SSID not configured");
        return;
    }

    wifi_config_t wifi_config;
    if (!prepare_wifi_config(&wifi_config)) {
        return;
    }

    esp_err_t err = esp_netif_init();
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        set_esp_error("esp_netif_init", err);
        return;
    }

    err = esp_event_loop_create_default();
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        set_esp_error("event loop init", err);
        return;
    }

    if (esp_netif_create_default_wifi_sta() == NULL) {
        s_result = TEST_RESULT_FAIL;
        snprintf(s_detail, sizeof(s_detail), "failed to create STA network interface");
        return;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        set_esp_error("esp_wifi_init", err);
        return;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        set_esp_error("Wi-Fi RAM storage", err);
        return;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        set_esp_error("STA mode", err);
        return;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        set_esp_error("STA config", err);
        return;
    }

    s_event_group = xEventGroupCreate();
    if (s_event_group == NULL) {
        s_result = TEST_RESULT_FAIL;
        snprintf(s_detail, sizeof(s_detail), "failed to create event group");
        return;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT,
                                              WIFI_EVENT_STA_DISCONNECTED,
                                              wifi_disconnected_handler,
                                              NULL,
                                              &s_wifi_event_instance);
    if (err != ESP_OK) {
        set_esp_error("Wi-Fi event handler", err);
        cleanup_event_wait();
        return;
    }
    s_wifi_event_registered = true;

    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              got_ip_handler,
                                              NULL,
                                              &s_ip_event_instance);
    if (err != ESP_OK) {
        set_esp_error("IP event handler", err);
        cleanup_event_wait();
        return;
    }
    s_ip_event_registered = true;

    err = esp_wifi_start();
    if (err != ESP_OK) {
        set_esp_error("esp_wifi_start", err);
        cleanup_event_wait();
        return;
    }

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        set_esp_error("esp_wifi_connect", err);
        cleanup_event_wait();
        return;
    }

    EventBits_t bits = xEventGroupWaitBits(s_event_group,
                                           WIFI_TEST_GOT_IPV4_BIT | WIFI_TEST_DISCONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(CONFIG_MODULINO_WIFI_CONNECT_TIMEOUT_MS));

    if ((bits & WIFI_TEST_GOT_IPV4_BIT) != 0) {
        s_result = TEST_RESULT_PASS;
        snprintf(s_detail, sizeof(s_detail), "IP=" IPSTR, IP2STR(&s_assigned_ip));
    } else if ((bits & WIFI_TEST_DISCONNECTED_BIT) != 0) {
        s_result = TEST_RESULT_FAIL;
        snprintf(s_detail,
                 sizeof(s_detail),
                 "%s (reason=%u)",
                 disconnect_reason_name(s_disconnect_reason),
                 (unsigned int)s_disconnect_reason);
    } else {
        s_result = TEST_RESULT_FAIL;
        snprintf(s_detail,
                 sizeof(s_detail),
                 "timeout after %d ms",
                 CONFIG_MODULINO_WIFI_CONNECT_TIMEOUT_MS);
    }

    cleanup_event_wait();

    if (s_result == TEST_RESULT_FAIL) {
        esp_wifi_disconnect();
    }
}

test_result_t wifi_test_get_result(void)
{
    return s_result;
}

const char *wifi_test_get_detail(void)
{
    return s_detail;
}
