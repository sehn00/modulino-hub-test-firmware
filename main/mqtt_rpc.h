#pragma once

#include "test_result.h"

void mqtt_rpc_start(void);
test_result_t mqtt_rpc_get_subscribe_result(void);
const char *mqtt_rpc_get_subscribe_detail(void);
