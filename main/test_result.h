#pragma once

typedef enum {
    TEST_RESULT_PASS = 0,
    TEST_RESULT_FAIL,
    TEST_RESULT_SKIP,
    TEST_RESULT_NOT_SUPPORTED,
} test_result_t;

const char *test_result_to_string(test_result_t result);
void test_result_print(const char *name, test_result_t result, const char *detail);
