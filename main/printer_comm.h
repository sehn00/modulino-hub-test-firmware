#pragma once

#include <stddef.h>

#include "esp_err.h"

esp_err_t printer_comm_mock_query(const char *gcode, char *response, size_t response_size);
esp_err_t printer_comm_uart_query(const char *gcode, char *response, size_t response_size);
