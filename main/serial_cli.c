#include "serial_cli.h"

#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gcode_safety.h"
#include "module_uart_test.h"
#include "mqtt_publish.h"
#include "mqtt_rpc.h"
#include "mqtt_test.h"
#include "nvs_test.h"
#include "printer_comm.h"
#include "test_result.h"
#include "wifi_test.h"

#define SERIAL_CLI_LINE_SIZE 96
#define SERIAL_CLI_RESPONSE_SIZE 192
#define SERIAL_CLI_TASK_STACK_SIZE 4096
#define SERIAL_CLI_TASK_PRIORITY 5

static bool streq(const char *left, const char *right)
{
    return (left != NULL) && (right != NULL) && (strcmp(left, right) == 0);
}

static void print_help(void)
{
    printf("\n");
    printf("modulino-dev test firmware CLI\n");
    printf("commands:\n");
    printf("  help\n");
    printf("  test all\n");
    printf("  test module uart\n");
    printf("  printer m105\n");
    printf("  printer m114\n");
    printf("  printer m115\n");
    printf("\n");
}

static test_result_t expect_safe_read(const char *gcode)
{
    return (gcode_safety_classify(gcode) == GCODE_SAFETY_SAFE_READ) ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

static void run_all_tests(void)
{
    char response[SERIAL_CLI_RESPONSE_SIZE];
    esp_err_t err = nvs_test_get_init_result();

    test_result_print("nvs init", err == ESP_OK ? TEST_RESULT_PASS : TEST_RESULT_FAIL, esp_err_to_name(err));
    test_result_print("wifi connect", wifi_test_get_result(), wifi_test_get_detail());
    test_result_print("mqtt connect", mqtt_test_get_result(), mqtt_test_get_detail());
    test_result_print("mqtt initial publish", mqtt_publish_get_initial_result(), mqtt_publish_get_initial_detail());
    test_result_print("mqtt rpc subscribe", mqtt_rpc_get_subscribe_result(), mqtt_rpc_get_subscribe_detail());

    test_result_print("gcode_safety M105", expect_safe_read("M105"), gcode_safety_to_string(gcode_safety_classify("M105")));
    test_result_print("gcode_safety M114", expect_safe_read("M114"), gcode_safety_to_string(gcode_safety_classify("M114")));
    test_result_print("gcode_safety M115", expect_safe_read("M115"), gcode_safety_to_string(gcode_safety_classify("M115")));

    test_result_print("gcode_safety G28", gcode_safety_classify("G28") == GCODE_SAFETY_NOT_SUPPORTED ? TEST_RESULT_PASS : TEST_RESULT_FAIL,
                      gcode_safety_to_string(gcode_safety_classify("G28")));

    err = printer_comm_mock_query("M105", response, sizeof(response));
    test_result_print("printer_mock M105", err == ESP_OK ? TEST_RESULT_PASS : TEST_RESULT_FAIL, esp_err_to_name(err));

    err = printer_comm_mock_query("M114", response, sizeof(response));
    test_result_print("printer_mock M114", err == ESP_OK ? TEST_RESULT_PASS : TEST_RESULT_FAIL, esp_err_to_name(err));

    err = printer_comm_mock_query("M115", response, sizeof(response));
    test_result_print("printer_mock M115", err == ESP_OK ? TEST_RESULT_PASS : TEST_RESULT_FAIL, esp_err_to_name(err));

    err = printer_comm_uart_query("M105", response, sizeof(response));
    test_result_print("printer_uart real", err == ESP_ERR_NOT_SUPPORTED ? TEST_RESULT_NOT_SUPPORTED : TEST_RESULT_FAIL, esp_err_to_name(err));

    test_result_print("parts_module_uart handshake",
                      module_uart_test_get_result(),
                      module_uart_test_get_detail());
}

static const char *printer_command_to_gcode(const char *command)
{
    if (streq(command, "m105")) {
        return "M105";
    }
    if (streq(command, "m114")) {
        return "M114";
    }
    if (streq(command, "m115")) {
        return "M115";
    }

    return NULL;
}

static void handle_printer_command(const char *command)
{
    const char *gcode = printer_command_to_gcode(command);
    if (gcode == NULL) {
        printf("Unsupported printer command. Try: printer m105, printer m114, printer m115\n");
        return;
    }

    char response[SERIAL_CLI_RESPONSE_SIZE];
    esp_err_t err = printer_comm_mock_query(gcode, response, sizeof(response));
    if (err != ESP_OK) {
        test_result_print(gcode, TEST_RESULT_FAIL, esp_err_to_name(err));
        return;
    }

    printf("[PRINTER MOCK] %s -> %s\n", gcode, response);
}

static void handle_line(char *line)
{
    char *saveptr = NULL;
    char *first = strtok_r(line, " \t", &saveptr);
    char *second = strtok_r(NULL, " \t", &saveptr);
    char *third = strtok_r(NULL, " \t", &saveptr);

    if (first == NULL) {
        return;
    }

    if (streq(first, "help")) {
        print_help();
        return;
    }

    if (streq(first, "test") && streq(second, "all")) {
        run_all_tests();
        return;
    }

    if (streq(first, "test") && streq(second, "module") && streq(third, "uart")) {
        module_uart_test_run();
        test_result_print("parts_module_uart handshake",
                          module_uart_test_get_result(),
                          module_uart_test_get_detail());
        return;
    }

    if (streq(first, "printer")) {
        handle_printer_command(second);
        return;
    }

    printf("Unknown command. Type 'help'.\n");
}

static void serial_cli_task(void *arg)
{
    (void)arg;

    char line[SERIAL_CLI_LINE_SIZE];
    size_t line_length = 0;
    size_t overflow_length = 0;
    bool ignore_lf_after_cr = false;

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    print_help();
    printf("modulino> ");

    while (true) {
        int input = getchar();
        if (input == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (ignore_lf_after_cr) {
            ignore_lf_after_cr = false;
            if (input == '\n') {
                continue;
            }
        }

        if ((input == '\r') || (input == '\n')) {
            ignore_lf_after_cr = (input == '\r');
            putchar('\n');

            if (overflow_length > 0) {
                printf("Input too long. Maximum command length is %u characters.\n",
                       (unsigned int)(sizeof(line) - 1));
            } else {
                line[line_length] = '\0';
                handle_line(line);
            }

            line_length = 0;
            overflow_length = 0;
            printf("modulino> ");
            continue;
        }

        if ((input == '\b') || (input == 0x7f)) {
            if (overflow_length > 0) {
                overflow_length--;
                printf("\b \b");
            } else if (line_length > 0) {
                line_length--;
                printf("\b \b");
            }
            continue;
        }

        if (!isprint((unsigned char)input)) {
            continue;
        }

        putchar(input);

        if ((overflow_length == 0) && (line_length < (sizeof(line) - 1))) {
            line[line_length++] = (char)input;
        } else {
            overflow_length++;
        }
    }
}

void serial_cli_start(void)
{
    BaseType_t result = xTaskCreate(serial_cli_task,
                                    "serial_cli",
                                    SERIAL_CLI_TASK_STACK_SIZE,
                                    NULL,
                                    SERIAL_CLI_TASK_PRIORITY,
                                    NULL);
    if (result != pdPASS) {
        test_result_print("serial_cli task", TEST_RESULT_FAIL, "xTaskCreate failed");
    }
}
