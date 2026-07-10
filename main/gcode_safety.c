#include "gcode_safety.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>

static bool is_command_boundary(char ch)
{
    return (ch == '\0') || isspace((unsigned char)ch);
}

static bool command_equals(const char *gcode, const char *command)
{
    if ((gcode == NULL) || (command == NULL)) {
        return false;
    }

    while (isspace((unsigned char)*gcode)) {
        gcode++;
    }

    while (*command != '\0') {
        if (toupper((unsigned char)*gcode) != toupper((unsigned char)*command)) {
            return false;
        }
        gcode++;
        command++;
    }

    return is_command_boundary(*gcode);
}

gcode_safety_t gcode_safety_classify(const char *gcode)
{
    if (command_equals(gcode, "M105") || command_equals(gcode, "M114") || command_equals(gcode, "M115")) {
        return GCODE_SAFETY_SAFE_READ;
    }

    return GCODE_SAFETY_NOT_SUPPORTED;
}

bool gcode_safety_is_safe_read(const char *gcode)
{
    return gcode_safety_classify(gcode) == GCODE_SAFETY_SAFE_READ;
}

const char *gcode_safety_to_string(gcode_safety_t safety)
{
    switch (safety) {
    case GCODE_SAFETY_SAFE_READ:
        return "SAFE_READ";
    case GCODE_SAFETY_NOT_SUPPORTED:
        return "NOT_SUPPORTED";
    default:
        return "UNKNOWN";
    }
}
