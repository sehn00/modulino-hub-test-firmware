#pragma once

#include <stdbool.h>

#include "mqtt_client.h"
#include "test_result.h"

void mqtt_test_run(void);
test_result_t mqtt_test_get_result(void);
const char *mqtt_test_get_detail(void);
esp_mqtt_client_handle_t mqtt_test_get_client(void);
bool mqtt_test_is_connected(void);
