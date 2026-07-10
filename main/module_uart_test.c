#include "module_uart_test.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define MODULE_UART_PORT UART_NUM_1
#define MODULE_UART_TX_GPIO 17
#define MODULE_UART_RX_GPIO 18
#define MODULE_UART_BAUD_RATE 115200
#define MODULE_UART_RX_BUFFER_SIZE 256
#define MODULE_UART_RESPONSE_SIZE 96
#define MODULE_UART_DETAIL_SIZE 160
#define MODULE_UART_TIMEOUT_MS 1000

#define MODULE_UART_REQUEST "MODULINO_PING\n"
#define MODULE_UART_EXPECTED_RESPONSE "MODULINO_PONG"

static bool s_uart_initialized;
static test_result_t s_result = TEST_RESULT_SKIP;
static char s_detail[MODULE_UART_DETAIL_SIZE] = "test not run";

static esp_err_t initialize_uart(void)
{
    if (s_uart_initialized) {
        return ESP_OK;
    }

    const uart_config_t config = {
        .baud_rate = MODULE_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(MODULE_UART_PORT, &config);
    if (err != ESP_OK) {
        return err;
    }

    err = uart_set_pin(MODULE_UART_PORT,
                       MODULE_UART_TX_GPIO,
                       MODULE_UART_RX_GPIO,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }

    err = uart_driver_install(MODULE_UART_PORT,
                              MODULE_UART_RX_BUFFER_SIZE,
                              0,
                              0,
                              NULL,
                              0);
    if (err == ESP_OK) {
        s_uart_initialized = true;
    }
    return err;
}

static void store_esp_error(const char *operation, esp_err_t err)
{
    s_result = TEST_RESULT_FAIL;
    snprintf(s_detail,
             sizeof(s_detail),
             "%s: %s",
             operation,
             esp_err_to_name(err));
}

static void store_mismatch(const char *response, bool overflow)
{
    s_result = TEST_RESULT_FAIL;
    snprintf(s_detail,
             sizeof(s_detail),
             "response mismatch: %s%s",
             response,
             overflow ? "..." : "");
}

void module_uart_test_run(void)
{
    esp_err_t err = initialize_uart();
    if (err != ESP_OK) {
        store_esp_error("UART initialization failed", err);
        return;
    }

    err = uart_flush_input(MODULE_UART_PORT);
    if (err != ESP_OK) {
        store_esp_error("UART RX flush failed", err);
        return;
    }

    const char request[] = MODULE_UART_REQUEST;
    int written = uart_write_bytes(MODULE_UART_PORT, request, sizeof(request) - 1);
    if (written != (int)(sizeof(request) - 1)) {
        s_result = TEST_RESULT_FAIL;
        snprintf(s_detail, sizeof(s_detail), "UART write failed");
        return;
    }

    char response[MODULE_UART_RESPONSE_SIZE] = {0};
    size_t response_length = 0;
    bool overflow = false;
    int64_t deadline_us = esp_timer_get_time() + ((int64_t)MODULE_UART_TIMEOUT_MS * 1000);

    while (true) {
        int64_t remaining_us = deadline_us - esp_timer_get_time();
        if (remaining_us <= 0) {
            break;
        }

        TickType_t wait_ticks = pdMS_TO_TICKS((remaining_us + 999) / 1000);
        if (wait_ticks == 0) {
            wait_ticks = 1;
        }

        uint8_t byte = 0;
        int received = uart_read_bytes(MODULE_UART_PORT, &byte, 1, wait_ticks);
        if (received < 0) {
            s_result = TEST_RESULT_FAIL;
            snprintf(s_detail, sizeof(s_detail), "UART read failed");
            return;
        }
        if (received == 0) {
            break;
        }

        if ((byte == '\r') || (byte == '\n')) {
            if (response_length > 0 || overflow) {
                break;
            }
            continue;
        }

        if (response_length < (sizeof(response) - 1)) {
            response[response_length++] = isprint(byte) ? (char)byte : '?';
            response[response_length] = '\0';
        } else {
            overflow = true;
        }
    }

    if ((response_length == 0) && !overflow) {
        s_result = TEST_RESULT_FAIL;
        snprintf(s_detail,
                 sizeof(s_detail),
                 "timeout after %d ms",
                 MODULE_UART_TIMEOUT_MS);
        return;
    }

    if (overflow || (strcmp(response, MODULE_UART_EXPECTED_RESPONSE) != 0)) {
        store_mismatch(response, overflow);
        return;
    }

    s_result = TEST_RESULT_PASS;
    snprintf(s_detail, sizeof(s_detail), "response=%s", response);
}

test_result_t module_uart_test_get_result(void)
{
    return s_result;
}

const char *module_uart_test_get_detail(void)
{
    return s_detail;
}
