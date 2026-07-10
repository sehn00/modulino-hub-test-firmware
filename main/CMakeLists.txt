idf_component_register(SRCS
                    "app_main.c"
                    "test_result.c"
                    "serial_cli.c"
                    "gcode_safety.c"
                    "printer_comm.c"
                    "nvs_test.c"
                    "wifi_test.c"
                    "mqtt_test.c"
                    "mqtt_publish.c"
                    "mqtt_rpc.c"
                    "module_uart_test.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_uart esp_event esp_netif esp_timer esp_wifi http_parser json mqtt nvs_flash)
