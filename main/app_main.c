#include "esp_log.h"

#include "mqtt_publish.h"
#include "mqtt_rpc.h"
#include "mqtt_test.h"
#include "nvs_test.h"
#include "serial_cli.h"
#include "wifi_test.h"

static const char *TAG = "modulino_main";

void app_main(void)
{
    ESP_LOGI(TAG, "modulino-dev test firmware boot");
    ESP_LOGI(TAG, "cycle scope: printer mock/disconnected mode");
    ESP_LOGI(TAG, "printer UART: NOT_SUPPORTED");
    ESP_LOGI(TAG, "parts module UART: reserved for future 115200 8N1 loopback/mock MCU tests");
    ESP_LOGI(TAG, "MQTT initial and periodic status publish enabled after connection");

    esp_err_t nvs_result = nvs_test_init();
    if (nvs_result == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized");
    } else {
        ESP_LOGW(TAG, "NVS initialization failed: %s; continuing", esp_err_to_name(nvs_result));
    }

    wifi_test_run();
    mqtt_test_run();
    mqtt_publish_start();
    mqtt_rpc_start();

    serial_cli_start();
}
