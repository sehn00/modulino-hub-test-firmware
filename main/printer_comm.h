#pragma once

#include <stddef.h>

#include "esp_err.h"

typedef enum {
    PRINTER_COMM_NOT_TESTED = 0,
    PRINTER_COMM_OK,
    PRINTER_COMM_INVALID_ARGUMENT,
    PRINTER_COMM_UNSAFE_GCODE,
    PRINTER_COMM_INITIALIZATION_ERROR,
    PRINTER_COMM_RX_FLUSH_ERROR,
    PRINTER_COMM_WRITE_ERROR,
    PRINTER_COMM_TX_WAIT_ERROR,
    PRINTER_COMM_READ_ERROR,
    PRINTER_COMM_TIMEOUT,
    PRINTER_COMM_PRINTER_ERROR,
    PRINTER_COMM_RESPONSE_OVERFLOW,
} printer_comm_result_t;

printer_comm_result_t printer_comm_init(void);
esp_err_t printer_comm_mock_query(const char *gcode, char *response, size_t response_size);
printer_comm_result_t printer_comm_uart_query(const char *gcode, char *response, size_t response_size);

const char *printer_comm_get_printer_id(void);
printer_comm_result_t printer_comm_get_init_result(void);
printer_comm_result_t printer_comm_get_last_result(void);
const char *printer_comm_result_code(printer_comm_result_t result);
const char *printer_comm_result_message(printer_comm_result_t result);
const char *printer_comm_result_connection(printer_comm_result_t result);
const char *printer_comm_result_status_reason(printer_comm_result_t result);
