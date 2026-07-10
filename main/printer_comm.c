#include "printer_comm.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>

#include "gcode_safety.h"

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

esp_err_t printer_comm_uart_query(const char *gcode, char *response, size_t response_size)
{
    (void)gcode;
    (void)response;
    (void)response_size;

    return ESP_ERR_NOT_SUPPORTED;
}
