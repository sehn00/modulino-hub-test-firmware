#include "test_result.h"

#include <stdio.h>

const char *test_result_to_string(test_result_t result)
{
    switch (result) {
    case TEST_RESULT_PASS:
        return "PASS";
    case TEST_RESULT_FAIL:
        return "FAIL";
    case TEST_RESULT_SKIP:
        return "SKIP";
    case TEST_RESULT_NOT_SUPPORTED:
        return "NOT_SUPPORTED";
    default:
        return "UNKNOWN";
    }
}

void test_result_print(const char *name, test_result_t result, const char *detail)
{
    const char *safe_name = (name != NULL) ? name : "(unnamed)";

    if ((detail != NULL) && (detail[0] != '\0')) {
        printf("[TEST] %-32s %s - %s\n", safe_name, test_result_to_string(result), detail);
    } else {
        printf("[TEST] %-32s %s\n", safe_name, test_result_to_string(result));
    }
}
