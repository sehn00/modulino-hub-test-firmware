#pragma once

#include <stdint.h>

#include "test_result.h"

void mqtt_publish_start(void);
test_result_t mqtt_publish_get_initial_result(void);
const char *mqtt_publish_get_initial_detail(void);
const char *mqtt_publish_get_boot_id(void);
uint32_t mqtt_publish_next_sequence(void);
