#include "nvs_test.h"

#include <stdbool.h>

#include "nvs_flash.h"

static bool s_init_attempted;
static esp_err_t s_init_result = ESP_ERR_INVALID_STATE;

esp_err_t nvs_test_init(void)
{
    if (!s_init_attempted) {
        s_init_result = nvs_flash_init();
        s_init_attempted = true;
    }

    return s_init_result;
}

esp_err_t nvs_test_get_init_result(void)
{
    return s_init_result;
}
