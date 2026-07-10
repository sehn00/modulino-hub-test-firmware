#pragma once

#include "test_result.h"

void module_uart_test_run(void);
test_result_t module_uart_test_get_result(void);
const char *module_uart_test_get_detail(void);
