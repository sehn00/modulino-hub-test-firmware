#include "printer_comm.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include "gcode_safety.h"

#define PRINTER_UART_PORT UART_NUM_2
#define PRINTER_UART_TX_GPIO 7
#define PRINTER_UART_RX_GPIO 8
#define PRINTER_UART_RX_BUFFER_SIZE 4096
#define PRINTER_UART_RESPONSE_TIMEOUT_MS 3000
#define PRINTER_UART_TX_TIMEOUT_MS 3000
#define PRINTER_ID "prt_test001"

static bool s_init_attempted;
static bool s_uart_initialized;
static SemaphoreHandle_t s_transaction_mutex;
static printer_comm_result_t s_init_result = PRINTER_COMM_NOT_TESTED;
static printer_comm_result_t s_last_result = PRINTER_COMM_NOT_TESTED;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;

static const char *skip_spaces(const char *value)
{
    while ((value != NULL) && isspace((unsigned char)*value)) {
        value++;
    }

    return value;
}

static bool command_equals(const char *gcode, const char *command)
{
    gcode = skip_spaces(gcode);
    if ((gcode == NULL) || (command == NULL)) {
        return false;
    }

    while (*command != '\0') {
        if (toupper((unsigned char)*gcode) != toupper((unsigned char)*command)) {
            return false;
        }
        gcode++;
        command++;
    }

    return (*gcode == '\0') || isspace((unsigned char)*gcode);
}

static const char *canonical_safe_command(const char *gcode)
{
    if (command_equals(gcode, "M105")) {
        return "M105";
    }
    if (command_equals(gcode, "M114")) {
        return "M114";
    }
    if (command_equals(gcode, "M115")) {
        return "M115";
    }

    return NULL;
}

static void store_last_result(printer_comm_result_t result)
{
    portENTER_CRITICAL(&s_status_lock);
    s_last_result = result;
    portEXIT_CRITICAL(&s_status_lock);
}

printer_comm_result_t printer_comm_init(void)
{
    if (s_init_attempted) {
        return s_init_result;
    }
    s_init_attempted = true;

    s_transaction_mutex = xSemaphoreCreateMutex();
    if (s_transaction_mutex == NULL) {
        s_init_result = PRINTER_COMM_INITIALIZATION_ERROR;
        store_last_result(PRINTER_COMM_INITIALIZATION_ERROR);
        return s_init_result;
    }

    const uart_config_t config = {
        .baud_rate = CONFIG_MODULINO_PRINTER_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(PRINTER_UART_PORT, &config);
    if (err == ESP_OK) {
        err = uart_set_pin(PRINTER_UART_PORT,
                           PRINTER_UART_TX_GPIO,
                           PRINTER_UART_RX_GPIO,
                           UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        err = uart_driver_install(PRINTER_UART_PORT,
                                  PRINTER_UART_RX_BUFFER_SIZE,
                                  0,
                                  0,
                                  NULL,
                                  0);
    }

    if (err != ESP_OK) {
        s_init_result = PRINTER_COMM_INITIALIZATION_ERROR;
        store_last_result(PRINTER_COMM_INITIALIZATION_ERROR);
        return s_init_result;
    }

    s_uart_initialized = true;
    s_init_result = PRINTER_COMM_OK;
    return s_init_result;
}

static const char *mock_response_for(const char *gcode)
{
    if (gcode_safety_classify(gcode) != GCODE_SAFETY_SAFE_READ) {
        return NULL;
    }

    if (command_equals(gcode, "M105")) {
        return "ok T:25.0 /0.0 B:24.0 /0.0";
    }

    if (command_equals(gcode, "M114")) {
        return "X:0.00 Y:0.00 Z:0.00 E:0.00 Count X:0 Y:0 Z:0\nok";
    }

    if (command_equals(gcode, "M115")) {
        return "FIRMWARE_NAME:modulino-dev-mock MACHINE_TYPE:disconnected EXTRUDER_COUNT:0\nok";
    }

    return NULL;
}

esp_err_t printer_comm_mock_query(const char *gcode, char *response, size_t response_size)
{
    if ((gcode == NULL) || (response == NULL) || (response_size == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *mock_response = mock_response_for(gcode);
    if (mock_response == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    int written = snprintf(response, response_size, "%s", mock_response);
    if ((written < 0) || ((size_t)written >= response_size)) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static printer_comm_result_t finish_transaction(printer_comm_result_t result)
{
    store_last_result(result);
    xSemaphoreGive(s_transaction_mutex);
    return result;
}

static bool line_starts_with(const char *line, size_t line_length, const char *prefix)
{
    size_t prefix_length = strlen(prefix);
    return (line_length >= prefix_length) && (memcmp(line, prefix, prefix_length) == 0);
}

static bool is_terminal_ok_line(const char *line, size_t line_length)
{
    if ((line_length < 2) || (memcmp(line, "ok", 2) != 0)) {
        return false;
    }

    return (line_length == 2) || (line[2] == ' ') || (line[2] == '\t');
}

printer_comm_result_t printer_comm_uart_query(const char *gcode, char *response, size_t response_size)
{
    if ((gcode == NULL) || (response == NULL) || (response_size == 0)) {
        return PRINTER_COMM_INVALID_ARGUMENT;
    }
    response[0] = '\0';

    if ((gcode_safety_classify(gcode) != GCODE_SAFETY_SAFE_READ) ||
        (canonical_safe_command(gcode) == NULL)) {
        return PRINTER_COMM_UNSAFE_GCODE;
    }

    if (!s_uart_initialized || (s_transaction_mutex == NULL)) {
        store_last_result(PRINTER_COMM_INITIALIZATION_ERROR);
        return PRINTER_COMM_INITIALIZATION_ERROR;
    }

    if (xSemaphoreTake(s_transaction_mutex, portMAX_DELAY) != pdTRUE) {
        store_last_result(PRINTER_COMM_INITIALIZATION_ERROR);
        return PRINTER_COMM_INITIALIZATION_ERROR;
    }

    /* Defense in depth: no UART operation may precede this in-lock safety check. */
    const char *command = canonical_safe_command(gcode);
    if ((gcode_safety_classify(gcode) != GCODE_SAFETY_SAFE_READ) || (command == NULL)) {
        xSemaphoreGive(s_transaction_mutex);
        return PRINTER_COMM_UNSAFE_GCODE;
    }

    esp_err_t err = uart_flush_input(PRINTER_UART_PORT);
    if (err != ESP_OK) {
        return finish_transaction(PRINTER_COMM_RX_FLUSH_ERROR);
    }

    char request[6];
    int request_length = snprintf(request, sizeof(request), "%s\n", command);
    if ((request_length <= 0) || ((size_t)request_length >= sizeof(request))) {
        return finish_transaction(PRINTER_COMM_WRITE_ERROR);
    }

    int written = uart_write_bytes(PRINTER_UART_PORT, request, (size_t)request_length);
    if (written != request_length) {
        return finish_transaction(PRINTER_COMM_WRITE_ERROR);
    }

    err = uart_wait_tx_done(PRINTER_UART_PORT, pdMS_TO_TICKS(PRINTER_UART_TX_TIMEOUT_MS));
    if (err != ESP_OK) {
        return finish_transaction(PRINTER_COMM_TX_WAIT_ERROR);
    }

    size_t response_used = 0;
    size_t line_start = 0;
    size_t line_length = 0;
    int64_t deadline_us = esp_timer_get_time() + ((int64_t)PRINTER_UART_RESPONSE_TIMEOUT_MS * 1000);

    while (true) {
        int64_t remaining_us = deadline_us - esp_timer_get_time();
        if (remaining_us <= 0) {
            return finish_transaction(PRINTER_COMM_TIMEOUT);
        }

        TickType_t wait_ticks = pdMS_TO_TICKS((remaining_us + 999) / 1000);
        if (wait_ticks == 0) {
            wait_ticks = 1;
        }

        uint8_t byte = 0;
        int received = uart_read_bytes(PRINTER_UART_PORT, &byte, 1, wait_ticks);
        if (received < 0) {
            return finish_transaction(PRINTER_COMM_READ_ERROR);
        }
        if (received == 0) {
            continue;
        }

        if ((byte == '\r') || (byte == '\n')) {
            if (line_length == 0) {
                continue;
            }

            const char *line = response + line_start;
            if (line_starts_with(line, line_length, "Error:")) {
                return finish_transaction(PRINTER_COMM_PRINTER_ERROR);
            }
            if (is_terminal_ok_line(line, line_length)) {
                return finish_transaction(PRINTER_COMM_OK);
            }

            line_length = 0;
            continue;
        }

        size_t required = (line_length == 0 && response_used > 0) ? 2 : 1;
        if (required >= (response_size - response_used)) {
            return finish_transaction(PRINTER_COMM_RESPONSE_OVERFLOW);
        }

        if (line_length == 0) {
            if (response_used > 0) {
                response[response_used++] = '\n';
            }
            line_start = response_used;
        }

        response[response_used++] = (byte == '\0') ? '?' : (char)byte;
        response[response_used] = '\0';
        line_length++;
    }
}

const char *printer_comm_get_printer_id(void)
{
    return PRINTER_ID;
}

printer_comm_result_t printer_comm_get_init_result(void)
{
    return s_init_result;
}

printer_comm_result_t printer_comm_get_last_result(void)
{
    printer_comm_result_t result;

    portENTER_CRITICAL(&s_status_lock);
    result = s_last_result;
    portEXIT_CRITICAL(&s_status_lock);

    return result;
}

const char *printer_comm_result_code(printer_comm_result_t result)
{
    switch (result) {
    case PRINTER_COMM_OK:
        return "ok";
    case PRINTER_COMM_TIMEOUT:
        return "printer_timeout";
    case PRINTER_COMM_PRINTER_ERROR:
        return "printer_error";
    case PRINTER_COMM_RESPONSE_OVERFLOW:
        return "response_overflow";
    case PRINTER_COMM_UNSAFE_GCODE:
        return "unsafe_gcode";
    case PRINTER_COMM_INVALID_ARGUMENT:
        return "invalid_argument";
    case PRINTER_COMM_INITIALIZATION_ERROR:
    case PRINTER_COMM_RX_FLUSH_ERROR:
    case PRINTER_COMM_WRITE_ERROR:
    case PRINTER_COMM_TX_WAIT_ERROR:
    case PRINTER_COMM_READ_ERROR:
        return "uart_error";
    case PRINTER_COMM_NOT_TESTED:
    default:
        return "not_tested";
    }
}

const char *printer_comm_result_message(printer_comm_result_t result)
{
    switch (result) {
    case PRINTER_COMM_OK:
        return "Printer returned a terminal ok response";
    case PRINTER_COMM_INVALID_ARGUMENT:
        return "Printer query arguments are invalid";
    case PRINTER_COMM_UNSAFE_GCODE:
        return "G-code is not an allowed SAFE_READ command";
    case PRINTER_COMM_INITIALIZATION_ERROR:
        return "Printer UART initialization failed";
    case PRINTER_COMM_RX_FLUSH_ERROR:
        return "Printer UART RX flush failed";
    case PRINTER_COMM_WRITE_ERROR:
        return "Printer UART write failed or was incomplete";
    case PRINTER_COMM_TX_WAIT_ERROR:
        return "Printer UART transmit completion wait failed";
    case PRINTER_COMM_READ_ERROR:
        return "Printer UART read failed";
    case PRINTER_COMM_TIMEOUT:
        return "Printer did not return a terminal response within 3000 ms";
    case PRINTER_COMM_PRINTER_ERROR:
        return "Printer returned an Error: response";
    case PRINTER_COMM_RESPONSE_OVERFLOW:
        return "Printer response exceeded the caller buffer";
    case PRINTER_COMM_NOT_TESTED:
    default:
        return "No Printer UART transaction has been run";
    }
}

const char *printer_comm_result_connection(printer_comm_result_t result)
{
    switch (result) {
    case PRINTER_COMM_OK:
    case PRINTER_COMM_PRINTER_ERROR:
    case PRINTER_COMM_RESPONSE_OVERFLOW:
        return "connected";
    case PRINTER_COMM_INITIALIZATION_ERROR:
    case PRINTER_COMM_RX_FLUSH_ERROR:
    case PRINTER_COMM_WRITE_ERROR:
    case PRINTER_COMM_TX_WAIT_ERROR:
    case PRINTER_COMM_READ_ERROR:
    case PRINTER_COMM_TIMEOUT:
        return "disconnected";
    case PRINTER_COMM_NOT_TESTED:
    case PRINTER_COMM_INVALID_ARGUMENT:
    case PRINTER_COMM_UNSAFE_GCODE:
    default:
        return "unknown";
    }
}

const char *printer_comm_result_status_reason(printer_comm_result_t result)
{
    switch (result) {
    case PRINTER_COMM_OK:
        return "last_transaction_ok";
    case PRINTER_COMM_PRINTER_ERROR:
        return "printer_error";
    case PRINTER_COMM_RESPONSE_OVERFLOW:
        return "response_overflow";
    case PRINTER_COMM_INITIALIZATION_ERROR:
        return "uart_initialization_error";
    case PRINTER_COMM_RX_FLUSH_ERROR:
        return "uart_rx_flush_error";
    case PRINTER_COMM_WRITE_ERROR:
        return "uart_write_error";
    case PRINTER_COMM_TX_WAIT_ERROR:
        return "uart_tx_wait_error";
    case PRINTER_COMM_READ_ERROR:
        return "uart_read_error";
    case PRINTER_COMM_TIMEOUT:
        return "printer_timeout";
    case PRINTER_COMM_NOT_TESTED:
    case PRINTER_COMM_INVALID_ARGUMENT:
    case PRINTER_COMM_UNSAFE_GCODE:
    default:
        return "not_tested";
    }
}
