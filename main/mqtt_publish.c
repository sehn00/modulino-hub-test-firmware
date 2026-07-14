#include "mqtt_publish.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "mqtt_test.h"
#include "printer_comm.h"

#define MQTT_PUBLISH_FW_VERSION "0.1.0"
#define MQTT_PUBLISH_PROTO_VERSION "1.0"
#define MQTT_PUBLISH_SCAN_ID "scan_mock001"

#define MQTT_PUBLISH_TOPIC_SIZE 128
#define MQTT_PUBLISH_PAYLOAD_SIZE 768
#define MQTT_PUBLISH_DETAIL_SIZE 160
#define MQTT_PUBLISH_BOOT_ID_SIZE 38
#define MQTT_PUBLISH_TASK_STACK_SIZE 6144
#define MQTT_PUBLISH_TASK_PRIORITY 4
#define MQTT_HEARTBEAT_PERIOD_MS 5000
#define MQTT_PRINTER_STATUS_PERIOD_MS 3000
#define MQTT_PUBLISH_TASK_POLL_MS 100

typedef struct {
    char status[MQTT_PUBLISH_TOPIC_SIZE];
    char birth[MQTT_PUBLISH_TOPIC_SIZE];
    char discovery[MQTT_PUBLISH_TOPIC_SIZE];
    char logs[MQTT_PUBLISH_TOPIC_SIZE];
    char heartbeat[MQTT_PUBLISH_TOPIC_SIZE];
    char printer_status[MQTT_PUBLISH_TOPIC_SIZE];
} mqtt_topics_t;

static bool s_start_attempted;
static test_result_t s_initial_result = TEST_RESULT_SKIP;
static char s_initial_detail[MQTT_PUBLISH_DETAIL_SIZE] = "not run";
static char s_boot_id[MQTT_PUBLISH_BOOT_ID_SIZE];
static uint32_t s_sequence;
static portMUX_TYPE s_sequence_lock = portMUX_INITIALIZER_UNLOCKED;
static mqtt_topics_t s_topics;

static void generate_boot_id(void)
{
    uint32_t random_words[4];
    esp_fill_random(random_words, sizeof(random_words));

    snprintf(s_boot_id,
             sizeof(s_boot_id),
             "boot_%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32,
             random_words[0],
             random_words[1],
             random_words[2],
             random_words[3]);
}

uint32_t mqtt_publish_next_sequence(void)
{
    uint32_t sequence;

    portENTER_CRITICAL(&s_sequence_lock);
    if (s_sequence == UINT32_MAX) {
        s_sequence = 0;
    }
    sequence = ++s_sequence;
    portEXIT_CRITICAL(&s_sequence_lock);

    return sequence;
}

static bool format_topic(char *buffer, size_t buffer_size, const char *suffix)
{
    int written = snprintf(buffer,
                           buffer_size,
                           "modulino/local/v1/%s/%s",
                           CONFIG_MODULINO_HUB_ID,
                           suffix);
    return (written >= 0) && ((size_t)written < buffer_size);
}

static bool prepare_topics(void)
{
    return format_topic(s_topics.status, sizeof(s_topics.status), "status") &&
           format_topic(s_topics.birth, sizeof(s_topics.birth), "birth") &&
           format_topic(s_topics.discovery, sizeof(s_topics.discovery), "modules/discovery") &&
           format_topic(s_topics.logs, sizeof(s_topics.logs), "logs") &&
           format_topic(s_topics.heartbeat, sizeof(s_topics.heartbeat), "heartbeat") &&
           format_topic(s_topics.printer_status, sizeof(s_topics.printer_status), "printer/status");
}

static cJSON *create_payload(const char *schema)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    if ((cJSON_AddStringToObject(root, "schema", schema) == NULL) ||
        (cJSON_AddStringToObject(root, "hub_id", CONFIG_MODULINO_HUB_ID) == NULL) ||
        (cJSON_AddStringToObject(root, "boot_id", s_boot_id) == NULL) ||
        (cJSON_AddNumberToObject(root, "seq", mqtt_publish_next_sequence()) == NULL)) {
        cJSON_Delete(root);
        return NULL;
    }

    return root;
}

static bool add_unsynced_time(cJSON *root)
{
    return (cJSON_AddNullToObject(root, "device_ts") != NULL) &&
           (cJSON_AddStringToObject(root, "ts_quality", "unsynced") != NULL);
}

static bool enqueue_payload(const char *topic, int qos, bool retain, cJSON *root)
{
    if (root == NULL) {
        return false;
    }

    char payload[MQTT_PUBLISH_PAYLOAD_SIZE];
    bool serialized = cJSON_PrintPreallocated(root, payload, sizeof(payload), false);
    cJSON_Delete(root);
    if (!serialized) {
        return false;
    }

    esp_mqtt_client_handle_t client = mqtt_test_get_client();
    if ((client == NULL) || !mqtt_test_is_connected()) {
        return false;
    }

    int message_id = esp_mqtt_client_enqueue(client,
                                             topic,
                                             payload,
                                             (int)strlen(payload),
                                             qos,
                                             retain,
                                             true);
    return message_id >= 0;
}

static bool publish_online_status(void)
{
    cJSON *root = create_payload("modulino.hub_status.v1");
    if (root == NULL) {
        return false;
    }

    if ((cJSON_AddStringToObject(root, "status", "online") == NULL) ||
        (cJSON_AddStringToObject(root, "reason", "connected") == NULL) ||
        !add_unsynced_time(root)) {
        cJSON_Delete(root);
        return false;
    }

    return enqueue_payload(s_topics.status, 1, true, root);
}

static const char *reset_reason_string(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "power_on";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:
        return "task_watchdog";
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep_sleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    case ESP_RST_USB:
        return "usb";
    case ESP_RST_JTAG:
        return "jtag";
    case ESP_RST_EFUSE:
        return "efuse";
    case ESP_RST_PWR_GLITCH:
        return "power_glitch";
    case ESP_RST_CPU_LOCKUP:
        return "cpu_lockup";
    case ESP_RST_UNKNOWN:
    default:
        return "unknown";
    }
}

static bool publish_birth(void)
{
    cJSON *root = create_payload("modulino.hub_birth.v1");
    if (root == NULL) {
        return false;
    }

    if ((cJSON_AddStringToObject(root, "fw_version", MQTT_PUBLISH_FW_VERSION) == NULL) ||
        (cJSON_AddStringToObject(root, "proto_version", MQTT_PUBLISH_PROTO_VERSION) == NULL) ||
        (cJSON_AddStringToObject(root, "reset_reason", reset_reason_string(esp_reset_reason())) == NULL) ||
        !add_unsynced_time(root)) {
        cJSON_Delete(root);
        return false;
    }

    return enqueue_payload(s_topics.birth, 1, true, root);
}

static bool publish_mock_discovery(void)
{
    cJSON *root = create_payload("modulino.module_discovery.v1");
    if (root == NULL) {
        return false;
    }

    if ((cJSON_AddStringToObject(root, "scan_id", MQTT_PUBLISH_SCAN_ID) == NULL) ||
        (cJSON_AddStringToObject(root, "source", "mock") == NULL) ||
        (cJSON_AddArrayToObject(root, "modules") == NULL) ||
        !add_unsynced_time(root)) {
        cJSON_Delete(root);
        return false;
    }

    return enqueue_payload(s_topics.discovery, 1, true, root);
}

static bool publish_connected_log(void)
{
    cJSON *root = create_payload("modulino.log.v1");
    if (root == NULL) {
        return false;
    }

    if ((cJSON_AddStringToObject(root, "level", "info") == NULL) ||
        (cJSON_AddStringToObject(root, "event", "mqtt_connected") == NULL) ||
        !add_unsynced_time(root)) {
        cJSON_Delete(root);
        return false;
    }

    return enqueue_payload(s_topics.logs, 0, false, root);
}

static bool publish_heartbeat(void)
{
    cJSON *root = create_payload("modulino.heartbeat.v1");
    if (root == NULL) {
        return false;
    }

    wifi_ap_record_t access_point;
    bool rssi_added;
    if (esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) {
        rssi_added = cJSON_AddNumberToObject(root, "wifi_rssi_dbm", access_point.rssi) != NULL;
    } else {
        rssi_added = cJSON_AddNullToObject(root, "wifi_rssi_dbm") != NULL;
    }

    if ((cJSON_AddNumberToObject(root, "uptime_ms", (double)(esp_timer_get_time() / 1000)) == NULL) ||
        (cJSON_AddNumberToObject(root, "free_heap_bytes", esp_get_free_heap_size()) == NULL) ||
        !rssi_added ||
        !add_unsynced_time(root)) {
        cJSON_Delete(root);
        return false;
    }

    return enqueue_payload(s_topics.heartbeat, 0, false, root);
}

static bool publish_printer_status(void)
{
    cJSON *root = create_payload("modulino.printer_status.v1");
    if (root == NULL) {
        return false;
    }

    printer_comm_result_t last_result = printer_comm_get_last_result();

    if ((cJSON_AddStringToObject(root, "printer_id", printer_comm_get_printer_id()) == NULL) ||
        (cJSON_AddStringToObject(root, "connection", printer_comm_result_connection(last_result)) == NULL) ||
        (cJSON_AddStringToObject(root, "source", "uart") == NULL) ||
        (cJSON_AddStringToObject(root, "reason", printer_comm_result_status_reason(last_result)) == NULL) ||
        !add_unsynced_time(root)) {
        cJSON_Delete(root);
        return false;
    }

    return enqueue_payload(s_topics.printer_status, 0, false, root);
}

static bool deadline_reached(TickType_t now, TickType_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void mqtt_periodic_publish_task(void *arg)
{
    (void)arg;

    TickType_t now = xTaskGetTickCount();
    TickType_t heartbeat_deadline = now + pdMS_TO_TICKS(MQTT_HEARTBEAT_PERIOD_MS);
    TickType_t printer_deadline = now + pdMS_TO_TICKS(MQTT_PRINTER_STATUS_PERIOD_MS);

    while (true) {
        if (mqtt_test_is_connected()) {
            now = xTaskGetTickCount();

            if (deadline_reached(now, heartbeat_deadline)) {
                publish_heartbeat();
                heartbeat_deadline = now + pdMS_TO_TICKS(MQTT_HEARTBEAT_PERIOD_MS);
            }

            if (deadline_reached(now, printer_deadline)) {
                publish_printer_status();
                printer_deadline = now + pdMS_TO_TICKS(MQTT_PRINTER_STATUS_PERIOD_MS);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MQTT_PUBLISH_TASK_POLL_MS));
    }
}

static bool publish_initial_message(const char *name, bool (*publish_function)(void))
{
    if (publish_function()) {
        return true;
    }

    s_initial_result = TEST_RESULT_FAIL;
    snprintf(s_initial_detail, sizeof(s_initial_detail), "%s publish request failed", name);
    return false;
}

void mqtt_publish_start(void)
{
    if (s_start_attempted) {
        return;
    }
    s_start_attempted = true;
    generate_boot_id();

    if ((mqtt_test_get_result() != TEST_RESULT_PASS) ||
        !mqtt_test_is_connected() ||
        (mqtt_test_get_client() == NULL)) {
        s_initial_result = TEST_RESULT_SKIP;
        snprintf(s_initial_detail, sizeof(s_initial_detail), "MQTT not connected");
        return;
    }

    if (!prepare_topics()) {
        s_initial_result = TEST_RESULT_FAIL;
        snprintf(s_initial_detail, sizeof(s_initial_detail), "MQTT topic exceeds %u bytes", MQTT_PUBLISH_TOPIC_SIZE - 1);
        return;
    }

    if (!publish_initial_message("status", publish_online_status) ||
        !publish_initial_message("birth", publish_birth) ||
        !publish_initial_message("modules/discovery", publish_mock_discovery) ||
        !publish_initial_message("mqtt_connected log", publish_connected_log)) {
        return;
    }

    BaseType_t task_result = xTaskCreate(mqtt_periodic_publish_task,
                                         "mqtt_publish",
                                         MQTT_PUBLISH_TASK_STACK_SIZE,
                                         NULL,
                                         MQTT_PUBLISH_TASK_PRIORITY,
                                         NULL);
    if (task_result != pdPASS) {
        s_initial_result = TEST_RESULT_FAIL;
        snprintf(s_initial_detail, sizeof(s_initial_detail), "periodic publish task start failed");
        return;
    }

    s_initial_result = TEST_RESULT_PASS;
    snprintf(s_initial_detail, sizeof(s_initial_detail), "status,birth,modules,logs queued");
}

test_result_t mqtt_publish_get_initial_result(void)
{
    return s_initial_result;
}

const char *mqtt_publish_get_initial_detail(void)
{
    return s_initial_detail;
}

const char *mqtt_publish_get_boot_id(void)
{
    return s_boot_id;
}
