#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

// Test-only handshake mock. This is not an implementation of the Parts Module protocol.
#define MOCK_UART_PORT UART_NUM_1
#define MOCK_UART_RX_GPIO 20
#define MOCK_UART_TX_GPIO 21
#define MOCK_UART_BAUD_RATE 115200
#define MOCK_UART_RX_BUFFER_SIZE 256
#define MOCK_LINE_SIZE 64

#define MOCK_REQUEST "MODULINO_PING"
#define MOCK_RESPONSE "MODULINO_PONG\n"

static const char *TAG = "mock_module";

static void initialize_protocol_uart(void)
{
    const uart_config_t config = {
        .baud_rate = MOCK_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(MOCK_UART_PORT, &config));
    ESP_ERROR_CHECK(uart_set_pin(MOCK_UART_PORT,
                                 MOCK_UART_TX_GPIO,
                                 MOCK_UART_RX_GPIO,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(MOCK_UART_PORT,
                                        MOCK_UART_RX_BUFFER_SIZE,
                                        0,
                                        0,
                                        NULL,
                                        0));
}

static void process_line(const char *line, bool overflow)
{
    if (!overflow && (strcmp(line, MOCK_REQUEST) == 0)) {
        uart_write_bytes(MOCK_UART_PORT, MOCK_RESPONSE, strlen(MOCK_RESPONSE));
    }
}

void app_main(void)
{
    initialize_protocol_uart();
    ESP_LOGI(TAG, "test-only Parts Module UART mock ready");

    char line[MOCK_LINE_SIZE];
    size_t line_length = 0;
    bool overflow = false;
    bool ignore_lf_after_cr = false;

    while (true) {
        uint8_t byte = 0;
        int received = uart_read_bytes(MOCK_UART_PORT, &byte, 1, portMAX_DELAY);
        if (received <= 0) {
            continue;
        }

        if (ignore_lf_after_cr) {
            ignore_lf_after_cr = false;
            if (byte == '\n') {
                continue;
            }
        }

        if ((byte == '\r') || (byte == '\n')) {
            ignore_lf_after_cr = (byte == '\r');
            if ((line_length > 0) || overflow) {
                line[line_length] = '\0';
                process_line(line, overflow);
            }
            line_length = 0;
            overflow = false;
            continue;
        }

        if (line_length < (sizeof(line) - 1)) {
            line[line_length++] = (char)byte;
        } else {
            overflow = true;
        }
    }
}
