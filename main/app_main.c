#include "esp_log.h"
#include "sdkconfig.h"

#include "mqtt_publish.h"
#include "mqtt_rpc.h"
#include "mqtt_test.h"
#include "nvs_test.h"
#include "printer_comm.h"
#include "serial_cli.h"
#include "wifi_test.h"

static const char *TAG = "modulino_main";

void app_main(void)
{
    ESP_LOGI(TAG, "modulino-dev test firmware boot");
    ESP_LOGI(TAG, "cycle scope: Printer UART READY_FOR_HW_TEST");
    ESP_LOGI(TAG,
             "printer UART2: TX GPIO7, RX GPIO8, %d baud, 8N1, LF, 3000 ms",
             CONFIG_MODULINO_PRINTER_UART_BAUD_RATE);
    ESP_LOGI(TAG, "parts module UART: reserved for future 115200 8N1 loopback/mock MCU tests");
    ESP_LOGI(TAG, "MQTT initial and periodic status publish enabled after connection");

    printer_comm_result_t printer_init_result = printer_comm_init();
    if (printer_init_result == PRINTER_COMM_OK) {
        ESP_LOGI(TAG, "Printer UART initialized; hardware communication not yet verified");
    } else {
        ESP_LOGE(TAG,
                 "Printer UART initialization failed: %s",
                 printer_comm_result_message(printer_init_result));
    }

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
