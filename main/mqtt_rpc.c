#include "mqtt_rpc.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "gcode_safety.h"
#include "mqtt_publish.h"
#include "mqtt_test.h"
#include "printer_comm.h"

#define MQTT_RPC_REQUEST_PAYLOAD_MAX 8192
#define MQTT_RPC_SCRIPT_MAX 2048
#define MQTT_RPC_LINE_MAX 128
#define MQTT_RPC_MAX_LINES 10
#define MQTT_RPC_TOPIC_SIZE 128
#define MQTT_RPC_RESPONSE_PAYLOAD_SIZE 9216
#define MQTT_RPC_RAW_RESPONSE_SIZE 2048
#define MQTT_RPC_MOCK_RESPONSE_SIZE 256
#define MQTT_RPC_DETAIL_SIZE 160
#define MQTT_RPC_QUEUE_LENGTH 4
#define MQTT_RPC_WORKER_STACK_SIZE 10240
#define MQTT_RPC_WORKER_PRIORITY 4
#define MQTT_RPC_SUBSCRIBE_TIMEOUT_MS 5000

#define MQTT_RPC_SUBSCRIBED_BIT BIT0
#define MQTT_RPC_DISCONNECTED_BIT BIT1

#define MQTT_RPC_METHOD "printer.gcode.run"
#define MQTT_RPC_PRINTER_ID "prt_mock001"

typedef enum {
    RPC_REASON_NONE = 0,
    RPC_REASON_INVALID_JSON,
    RPC_REASON_INVALID_PARAMS,
    RPC_REASON_PAYLOAD_TOO_LARGE,
    RPC_REASON_SCRIPT_TOO_LARGE,
    RPC_REASON_UNSUPPORTED_METHOD,
    RPC_REASON_PRINTER_OFFLINE,
    RPC_REASON_UNCLASSIFIED_GCODE,
} rpc_reason_t;

typedef struct {
    rpc_reason_t preparse_reason;
    char *payload;
    size_t payload_length;
} rpc_queue_item_t;

typedef struct {
    size_t line_count;
    char lines[MQTT_RPC_MAX_LINES][MQTT_RPC_LINE_MAX + 1];
} rpc_script_t;

typedef struct {
    char *buffer;
    size_t total_length;
    size_t received_length;
    bool ignore;
} rpc_fragment_state_t;

static bool s_start_attempted;
static test_result_t s_subscribe_result = TEST_RESULT_SKIP;
static char s_subscribe_detail[MQTT_RPC_DETAIL_SIZE] = "not run";
static char s_request_topic[MQTT_RPC_TOPIC_SIZE];
static char s_progress_topic[MQTT_RPC_TOPIC_SIZE];
static char s_response_topic[MQTT_RPC_TOPIC_SIZE];
static char s_response_payload[MQTT_RPC_RESPONSE_PAYLOAD_SIZE];

static esp_mqtt_client_handle_t s_client;
static QueueHandle_t s_request_queue;
static EventGroupHandle_t s_subscribe_events;
static int s_subscribe_message_id = -1;
static bool s_event_registered;
static rpc_fragment_state_t s_fragments;

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
    return format_topic(s_request_topic, sizeof(s_request_topic), "rpc/request") &&
           format_topic(s_progress_topic, sizeof(s_progress_topic), "rpc/progress") &&
           format_topic(s_response_topic, sizeof(s_response_topic), "rpc/response");
}

static void reset_fragments(void)
{
    free(s_fragments.buffer);
    memset(&s_fragments, 0, sizeof(s_fragments));
}

static void queue_item(rpc_queue_item_t *item)
{
    if ((s_request_queue == NULL) || (xQueueSend(s_request_queue, item, 0) != pdPASS)) {
        free(item->payload);
    }
}

static void queue_preparse_rejection(rpc_reason_t reason)
{
    rpc_queue_item_t item = {
        .preparse_reason = reason,
        .payload = NULL,
        .payload_length = 0,
    };
    queue_item(&item);
}

static bool event_topic_is_request(const esp_mqtt_event_handle_t event)
{
    size_t expected_length = strlen(s_request_topic);
    return (event->topic != NULL) &&
           (event->topic_len >= 0) &&
           ((size_t)event->topic_len == expected_length) &&
           (memcmp(event->topic, s_request_topic, expected_length) == 0);
}

static void handle_request_fragment(const esp_mqtt_event_handle_t event)
{
    if ((event->current_data_offset < 0) || (event->data_len < 0) || (event->total_data_len < 0)) {
        reset_fragments();
        queue_preparse_rejection(RPC_REASON_INVALID_JSON);
        return;
    }

    size_t offset = (size_t)event->current_data_offset;
    size_t fragment_length = (size_t)event->data_len;
    size_t total_length = (event->total_data_len > 0) ? (size_t)event->total_data_len : fragment_length;

    if (offset == 0) {
        reset_fragments();

        if (!event_topic_is_request(event) || event->retain) {
            s_fragments.ignore = true;
            return;
        }

        if (total_length > MQTT_RPC_REQUEST_PAYLOAD_MAX) {
            s_fragments.ignore = true;
            queue_preparse_rejection(RPC_REASON_PAYLOAD_TOO_LARGE);
            return;
        }

        s_fragments.buffer = malloc(total_length + 1);
        if (s_fragments.buffer == NULL) {
            s_fragments.ignore = true;
            return;
        }
        s_fragments.total_length = total_length;
    }

    if (s_fragments.ignore) {
        return;
    }

    if ((s_fragments.buffer == NULL) ||
        (offset != s_fragments.received_length) ||
        (offset > s_fragments.total_length) ||
        (fragment_length > (s_fragments.total_length - offset))) {
        reset_fragments();
        s_fragments.ignore = true;
        queue_preparse_rejection(RPC_REASON_INVALID_JSON);
        return;
    }

    if (fragment_length > 0) {
        if (event->data == NULL) {
            reset_fragments();
            s_fragments.ignore = true;
            queue_preparse_rejection(RPC_REASON_INVALID_JSON);
            return;
        }
        memcpy(s_fragments.buffer + offset, event->data, fragment_length);
    }
    s_fragments.received_length += fragment_length;

    if (s_fragments.received_length == s_fragments.total_length) {
        s_fragments.buffer[s_fragments.total_length] = '\0';

        rpc_queue_item_t item = {
            .preparse_reason = RPC_REASON_NONE,
            .payload = s_fragments.buffer,
            .payload_length = s_fragments.total_length,
        };
        s_fragments.buffer = NULL;
        s_fragments.ignore = true;
        queue_item(&item);
    }
}

static void mqtt_rpc_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    esp_mqtt_event_handle_t event = event_data;
    if (event == NULL) {
        return;
    }

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_SUBSCRIBED:
        if ((event->msg_id == s_subscribe_message_id) && (s_subscribe_events != NULL)) {
            xEventGroupSetBits(s_subscribe_events, MQTT_RPC_SUBSCRIBED_BIT);
        }
        break;
    case MQTT_EVENT_DATA:
        handle_request_fragment(event);
        break;
    case MQTT_EVENT_DISCONNECTED:
        if (s_subscribe_events != NULL) {
            xEventGroupSetBits(s_subscribe_events, MQTT_RPC_DISCONNECTED_BIT);
        }
        break;
    default:
        break;
    }
}

static const char *reason_code(rpc_reason_t reason)
{
    switch (reason) {
    case RPC_REASON_INVALID_JSON:
        return "invalid_json";
    case RPC_REASON_INVALID_PARAMS:
        return "invalid_params";
    case RPC_REASON_PAYLOAD_TOO_LARGE:
        return "payload_too_large";
    case RPC_REASON_SCRIPT_TOO_LARGE:
        return "script_too_large";
    case RPC_REASON_UNSUPPORTED_METHOD:
        return "unsupported_method";
    case RPC_REASON_PRINTER_OFFLINE:
        return "printer_offline";
    case RPC_REASON_UNCLASSIFIED_GCODE:
        return "unclassified_gcode";
    case RPC_REASON_NONE:
    default:
        return "invalid_params";
    }
}

static const char *reason_message(rpc_reason_t reason)
{
    switch (reason) {
    case RPC_REASON_INVALID_JSON:
        return "Request payload is not valid JSON";
    case RPC_REASON_INVALID_PARAMS:
        return "Required JSON-RPC fields are missing or invalid";
    case RPC_REASON_PAYLOAD_TOO_LARGE:
        return "Request payload exceeds 8192 bytes";
    case RPC_REASON_SCRIPT_TOO_LARGE:
        return "G-code script exceeds the size or line limits";
    case RPC_REASON_UNSUPPORTED_METHOD:
        return "Only printer.gcode.run is supported";
    case RPC_REASON_PRINTER_OFFLINE:
        return "Requested printer is not the disconnected mock printer";
    case RPC_REASON_UNCLASSIFIED_GCODE:
        return "G-code is not classified SAFE_READ";
    case RPC_REASON_NONE:
    default:
        return "Invalid request";
    }
}

static cJSON *create_rpc_envelope(const char *request_id)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    if ((cJSON_AddStringToObject(root, "jsonrpc", "2.0") == NULL) ||
        (cJSON_AddStringToObject(root, "hub_id", CONFIG_MODULINO_HUB_ID) == NULL) ||
        (cJSON_AddStringToObject(root, "boot_id", mqtt_publish_get_boot_id()) == NULL) ||
        (cJSON_AddNumberToObject(root, "seq", mqtt_publish_next_sequence()) == NULL) ||
        (cJSON_AddNullToObject(root, "device_ts") == NULL) ||
        (cJSON_AddStringToObject(root, "ts_quality", "unsynced") == NULL)) {
        cJSON_Delete(root);
        return NULL;
    }

    bool id_added = (request_id != NULL)
                        ? (cJSON_AddStringToObject(root, "id", request_id) != NULL)
                        : (cJSON_AddNullToObject(root, "id") != NULL);
    if (!id_added) {
        cJSON_Delete(root);
        return NULL;
    }

    return root;
}

static bool enqueue_rpc_json(const char *topic, cJSON *root)
{
    if (root == NULL) {
        return false;
    }

    bool serialized = cJSON_PrintPreallocated(root,
                                              s_response_payload,
                                              sizeof(s_response_payload),
                                              false);
    cJSON_Delete(root);
    if (!serialized || !mqtt_test_is_connected() || (s_client == NULL)) {
        return false;
    }

    int message_id = esp_mqtt_client_enqueue(s_client,
                                             topic,
                                             s_response_payload,
                                             (int)strlen(s_response_payload),
                                             1,
                                             false,
                                             true);
    return message_id >= 0;
}

static bool publish_rejected_response(const char *request_id, rpc_reason_t reason)
{
    cJSON *root = create_rpc_envelope(request_id);
    if (root == NULL) {
        return false;
    }

    cJSON *error = cJSON_AddObjectToObject(root, "error");
    cJSON *data = (error != NULL) ? cJSON_AddObjectToObject(error, "data") : NULL;
    if ((error == NULL) ||
        (cJSON_AddStringToObject(error, "code", reason_code(reason)) == NULL) ||
        (cJSON_AddStringToObject(error, "message", reason_message(reason)) == NULL) ||
        (data == NULL) ||
        (cJSON_AddStringToObject(data, "status", "rejected") == NULL)) {
        cJSON_Delete(root);
        return false;
    }

    return enqueue_rpc_json(s_response_topic, root);
}

static bool publish_accepted_progress(const char *request_id)
{
    cJSON *root = create_rpc_envelope(request_id);
    if (root == NULL) {
        return false;
    }

    cJSON *progress = cJSON_AddObjectToObject(root, "progress");
    if ((progress == NULL) ||
        (cJSON_AddStringToObject(progress, "status", "accepted") == NULL) ||
        (cJSON_AddNumberToObject(progress, "elapsed_ms", 0) == NULL)) {
        cJSON_Delete(root);
        return false;
    }

    return enqueue_rpc_json(s_progress_topic, root);
}

static bool publish_completed_response(const char *request_id, const char *raw_response)
{
    cJSON *root = create_rpc_envelope(request_id);
    if (root == NULL) {
        return false;
    }

    cJSON *result = cJSON_AddObjectToObject(root, "result");
    if ((result == NULL) ||
        (cJSON_AddStringToObject(result, "status", "completed") == NULL) ||
        (cJSON_AddStringToObject(result, "raw_response", raw_response) == NULL) ||
        (cJSON_AddStringToObject(result, "source", "mock") == NULL)) {
        cJSON_Delete(root);
        return false;
    }

    return enqueue_rpc_json(s_response_topic, root);
}

static bool payload_contains_null_escape(const char *payload, size_t payload_length)
{
    for (size_t index = 0; index + 5 < payload_length; index++) {
        if ((payload[index] == '\\') &&
            (tolower((unsigned char)payload[index + 1]) == 'u') &&
            (payload[index + 2] == '0') &&
            (payload[index + 3] == '0') &&
            (payload[index + 4] == '0') &&
            (payload[index + 5] == '0')) {
            return true;
        }
    }
    return false;
}

static bool is_supported_safe_gcode(const char *line)
{
    if (gcode_safety_classify(line) != GCODE_SAFETY_SAFE_READ) {
        return false;
    }

    return (strcasecmp(line, "M105") == 0) ||
           (strcasecmp(line, "M114") == 0) ||
           (strcasecmp(line, "M115") == 0);
}

static rpc_reason_t validate_script(const char *script, rpc_script_t *parsed_script)
{
    size_t script_length = strlen(script);
    if (script_length == 0) {
        return RPC_REASON_INVALID_PARAMS;
    }
    if (script_length > MQTT_RPC_SCRIPT_MAX) {
        return RPC_REASON_SCRIPT_TOO_LARGE;
    }

    memset(parsed_script, 0, sizeof(*parsed_script));
    size_t position = 0;

    while (position < script_length) {
        size_t line_start = position;
        while ((position < script_length) && (script[position] != '\r') && (script[position] != '\n')) {
            position++;
        }

        size_t line_end = position;
        size_t raw_line_length = line_end - line_start;
        if ((raw_line_length > MQTT_RPC_LINE_MAX) || (parsed_script->line_count >= MQTT_RPC_MAX_LINES)) {
            return RPC_REASON_SCRIPT_TOO_LARGE;
        }

        while ((line_start < line_end) && isspace((unsigned char)script[line_start])) {
            line_start++;
        }
        while ((line_end > line_start) && isspace((unsigned char)script[line_end - 1])) {
            line_end--;
        }

        size_t normalized_length = line_end - line_start;
        if (normalized_length == 0) {
            return RPC_REASON_UNCLASSIFIED_GCODE;
        }

        char *line = parsed_script->lines[parsed_script->line_count];
        memcpy(line, script + line_start, normalized_length);
        line[normalized_length] = '\0';
        parsed_script->line_count++;

        if (!is_supported_safe_gcode(line)) {
            return RPC_REASON_UNCLASSIFIED_GCODE;
        }

        if (position < script_length) {
            char separator = script[position++];
            if ((separator == '\r') && (position < script_length) && (script[position] == '\n')) {
                position++;
            }
        }
    }

    return RPC_REASON_NONE;
}

static bool append_mock_response(char *output, size_t output_size, size_t *used, const char *response)
{
    int written = snprintf(output + *used,
                           output_size - *used,
                           "%s%s",
                           (*used > 0) ? "\n" : "",
                           response);
    if ((written < 0) || ((size_t)written >= (output_size - *used))) {
        return false;
    }
    *used += (size_t)written;
    return true;
}

static void process_request(const rpc_queue_item_t *item)
{
    if (item->preparse_reason != RPC_REASON_NONE) {
        publish_rejected_response(NULL, item->preparse_reason);
        return;
    }

    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(item->payload,
                                           item->payload_length + 1,
                                           &parse_end,
                                           true);
    if (root == NULL) {
        publish_rejected_response(NULL, RPC_REASON_INVALID_JSON);
        return;
    }

    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const char *request_id = (cJSON_IsString(id) && (id->valuestring != NULL)) ? id->valuestring : NULL;

    if (!cJSON_IsObject(root)) {
        publish_rejected_response(request_id, RPC_REASON_INVALID_PARAMS);
        cJSON_Delete(root);
        return;
    }

    const cJSON *jsonrpc = cJSON_GetObjectItemCaseSensitive(root, "jsonrpc");
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");

    if (!cJSON_IsString(jsonrpc) || (jsonrpc->valuestring == NULL) || (strcmp(jsonrpc->valuestring, "2.0") != 0) ||
        !cJSON_IsString(method) || (method->valuestring == NULL) ||
        !cJSON_IsObject(params) ||
        !cJSON_IsString(id) || (id->valuestring == NULL) ||
        payload_contains_null_escape(item->payload, item->payload_length)) {
        publish_rejected_response(request_id, RPC_REASON_INVALID_PARAMS);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(method->valuestring, MQTT_RPC_METHOD) != 0) {
        publish_rejected_response(request_id, RPC_REASON_UNSUPPORTED_METHOD);
        cJSON_Delete(root);
        return;
    }

    const cJSON *printer_id = cJSON_GetObjectItemCaseSensitive(params, "printer_id");
    const cJSON *script = cJSON_GetObjectItemCaseSensitive(params, "script");
    if (!cJSON_IsString(printer_id) || (printer_id->valuestring == NULL) ||
        !cJSON_IsString(script) || (script->valuestring == NULL)) {
        publish_rejected_response(request_id, RPC_REASON_INVALID_PARAMS);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(printer_id->valuestring, MQTT_RPC_PRINTER_ID) != 0) {
        publish_rejected_response(request_id, RPC_REASON_PRINTER_OFFLINE);
        cJSON_Delete(root);
        return;
    }

    rpc_script_t parsed_script;
    rpc_reason_t script_result = validate_script(script->valuestring, &parsed_script);
    if (script_result != RPC_REASON_NONE) {
        publish_rejected_response(request_id, script_result);
        cJSON_Delete(root);
        return;
    }

    if (!publish_accepted_progress(request_id)) {
        cJSON_Delete(root);
        return;
    }

    char raw_response[MQTT_RPC_RAW_RESPONSE_SIZE] = {0};
    size_t raw_response_used = 0;
    for (size_t index = 0; index < parsed_script.line_count; index++) {
        char mock_response[MQTT_RPC_MOCK_RESPONSE_SIZE];
        esp_err_t err = printer_comm_mock_query(parsed_script.lines[index],
                                                mock_response,
                                                sizeof(mock_response));
        if ((err != ESP_OK) ||
            !append_mock_response(raw_response,
                                  sizeof(raw_response),
                                  &raw_response_used,
                                  mock_response)) {
            publish_rejected_response(request_id, RPC_REASON_PRINTER_OFFLINE);
            cJSON_Delete(root);
            return;
        }
    }

    publish_completed_response(request_id, raw_response);
    cJSON_Delete(root);
}

static void mqtt_rpc_worker_task(void *arg)
{
    (void)arg;

    rpc_queue_item_t item;
    while (true) {
        if (xQueueReceive(s_request_queue, &item, portMAX_DELAY) == pdPASS) {
            process_request(&item);
            free(item.payload);
        }
    }
}

static void cleanup_start_failure(bool unsubscribe)
{
    if (s_event_registered && (s_client != NULL)) {
        esp_mqtt_client_unregister_event(s_client, MQTT_EVENT_ANY, mqtt_rpc_event_handler);
        s_event_registered = false;
    }

    if (unsubscribe && (s_client != NULL) && mqtt_test_is_connected()) {
        esp_mqtt_client_unsubscribe(s_client, s_request_topic);
    }

    reset_fragments();

    if (s_request_queue != NULL) {
        rpc_queue_item_t item;
        while (xQueueReceive(s_request_queue, &item, 0) == pdPASS) {
            free(item.payload);
        }
        vQueueDelete(s_request_queue);
        s_request_queue = NULL;
    }

    if (s_subscribe_events != NULL) {
        vEventGroupDelete(s_subscribe_events);
        s_subscribe_events = NULL;
    }
}

void mqtt_rpc_start(void)
{
    if (s_start_attempted) {
        return;
    }
    s_start_attempted = true;

    if ((mqtt_test_get_result() != TEST_RESULT_PASS) ||
        !mqtt_test_is_connected() ||
        (mqtt_test_get_client() == NULL)) {
        s_subscribe_result = TEST_RESULT_SKIP;
        snprintf(s_subscribe_detail, sizeof(s_subscribe_detail), "MQTT not connected");
        return;
    }

    if (!prepare_topics()) {
        s_subscribe_result = TEST_RESULT_FAIL;
        snprintf(s_subscribe_detail, sizeof(s_subscribe_detail), "RPC topic is too long");
        return;
    }

    s_request_queue = xQueueCreate(MQTT_RPC_QUEUE_LENGTH, sizeof(rpc_queue_item_t));
    s_subscribe_events = xEventGroupCreate();
    if ((s_request_queue == NULL) || (s_subscribe_events == NULL)) {
        s_subscribe_result = TEST_RESULT_FAIL;
        snprintf(s_subscribe_detail, sizeof(s_subscribe_detail), "RPC queue or event group allocation failed");
        cleanup_start_failure(false);
        return;
    }

    s_client = mqtt_test_get_client();
    esp_err_t err = esp_mqtt_client_register_event(s_client,
                                                   MQTT_EVENT_ANY,
                                                   mqtt_rpc_event_handler,
                                                   NULL);
    if (err != ESP_OK) {
        s_subscribe_result = TEST_RESULT_FAIL;
        snprintf(s_subscribe_detail,
                 sizeof(s_subscribe_detail),
                 "event handler: %s",
                 esp_err_to_name(err));
        cleanup_start_failure(false);
        return;
    }
    s_event_registered = true;

    s_subscribe_message_id = esp_mqtt_client_subscribe(s_client, s_request_topic, 1);
    if (s_subscribe_message_id < 0) {
        s_subscribe_result = TEST_RESULT_FAIL;
        snprintf(s_subscribe_detail, sizeof(s_subscribe_detail), "subscribe request failed");
        cleanup_start_failure(false);
        return;
    }

    EventBits_t bits = xEventGroupWaitBits(s_subscribe_events,
                                           MQTT_RPC_SUBSCRIBED_BIT | MQTT_RPC_DISCONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(MQTT_RPC_SUBSCRIBE_TIMEOUT_MS));
    if ((bits & MQTT_RPC_SUBSCRIBED_BIT) == 0) {
        s_subscribe_result = TEST_RESULT_FAIL;
        if ((bits & MQTT_RPC_DISCONNECTED_BIT) != 0) {
            snprintf(s_subscribe_detail, sizeof(s_subscribe_detail), "MQTT disconnected during subscribe");
        } else {
            snprintf(s_subscribe_detail, sizeof(s_subscribe_detail), "subscribe timeout after %d ms", MQTT_RPC_SUBSCRIBE_TIMEOUT_MS);
        }
        cleanup_start_failure(false);
        return;
    }

    BaseType_t worker_result = xTaskCreate(mqtt_rpc_worker_task,
                                           "mqtt_rpc",
                                           MQTT_RPC_WORKER_STACK_SIZE,
                                           NULL,
                                           MQTT_RPC_WORKER_PRIORITY,
                                           NULL);
    if (worker_result != pdPASS) {
        s_subscribe_result = TEST_RESULT_FAIL;
        snprintf(s_subscribe_detail, sizeof(s_subscribe_detail), "RPC worker task start failed");
        cleanup_start_failure(true);
        return;
    }

    s_subscribe_result = TEST_RESULT_PASS;
    snprintf(s_subscribe_detail, sizeof(s_subscribe_detail), "topic=%s", s_request_topic);
}

test_result_t mqtt_rpc_get_subscribe_result(void)
{
    return s_subscribe_result;
}

const char *mqtt_rpc_get_subscribe_detail(void)
{
    return s_subscribe_detail;
}
