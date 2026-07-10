#pragma once

#include "test_result.h"

void wifi_test_run(void);
test_result_t wifi_test_get_result(void);
const char *wifi_test_get_detail(void);
