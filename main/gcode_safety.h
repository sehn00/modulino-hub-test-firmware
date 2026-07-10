#pragma once

#include <stdbool.h>

typedef enum {
    GCODE_SAFETY_NOT_SUPPORTED = 0,
    GCODE_SAFETY_SAFE_READ,
} gcode_safety_t;

gcode_safety_t gcode_safety_classify(const char *gcode);
bool gcode_safety_is_safe_read(const char *gcode);
const char *gcode_safety_to_string(gcode_safety_t safety);
