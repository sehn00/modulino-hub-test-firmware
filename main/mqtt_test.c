#include "mqtt_test.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "http_parser.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "wifi_test.h"

#define MQTT_TEST_CONNECTED_BIT BIT0
#define MQTT_TEST_ERROR_BIT BIT1
#define MQTT_TEST_DISCONNECTED_BIT BIT2
#define MQTT_TEST_DETAIL_SIZE 128
#define MQTT_TEST_BROKER_SIZE 96
#define MQTT_TEST_LWT_TOPIC_SIZE 128
#define MQTT_TEST_LWT_PAYLOAD_SIZE 256

static bool s_run_attempted;
static test_result_t s_result = TEST_RESULT_FAIL;
static char s_detail[MQTT_TEST_DETAIL_SIZE] = "not run";
static char s_broker[MQTT_TEST_BROKER_SIZE];
static char s_lwt_topic[MQTT_TEST_LWT_TOPIC_SIZE];
static char s_lwt_payload[MQTT_TEST_LWT_PAYLOAD_SIZE];

static esp_mqtt_client_handle_t s_client;
static EventGroupHandle_t s_event_group;
static bool s_event_registered;
static volatile bool s_connected;
static char s_event_error[MQTT_TEST_DETAIL_SIZE];

static void set_esp_error(const char *operation, esp_err_t err)
{
    s_result = TEST_RESULT_FAIL;
    snprintf(s_detail, sizeof(s_detail), "%s: %s", operation, esp_err_to_name(err));
}

static bool hub_id_is_valid(const char *hub_id)
{
    if ((hub_id == NULL) || (hub_id[0] == '\0')) {
        return false;
    }

    for (const unsigned char *cursor = (const unsigned char *)hub_id; *cursor != '\0'; cursor++) {
        if (!isalnum(*cursor) && (*cursor != '_') && (*cursor != '-')) {
            return false;
        }
    }

    return true;
}

static bool parse_broker_uri(const char *uri)
{
    struct http_parser_url parsed_uri;
    http_parser_url_init(&parsed_uri);

    size_t uri_length = strlen(uri);
    if ((http_parser_parse_url(uri, uri_length, 0, &parsed_uri) != 0) ||
        ((parsed_uri.field_set & (1U << UF_SCHEMA)) == 0) ||
        ((parsed_uri.field_set & (1U << UF_HOST)) == 0) ||
        ((parsed_uri.field_set & (1U << UF_USERINFO)) != 0)) {
        snprintf(s_detail, sizeof(s_detail), "invalid broker URI");
        return false;
    }

    uint16_t scheme_offset = parsed_uri.field_data[UF_SCHEMA].off;
    uint16_t scheme_length = parsed_uri.field_data[UF_SCHEMA].len;
    if ((scheme_length != strlen("mqtt")) || (strncmp(uri + scheme_offset, "mqtt", scheme_length) != 0)) {
        snprintf(s_detail, sizeof(s_detail), "only plain mqtt:// URI is supported");
        return false;
    }

    uint16_t host_offset = parsed_uri.field_data[UF_HOST].off;
    uint16_t host_length = parsed_uri.field_data[UF_HOST].len;
    uint16_t port = ((parsed_uri.field_set & (1U << UF_PORT)) != 0) ? parsed_uri.port : 1883;
    int written = snprintf(s_broker,
                           sizeof(s_broker),
                           "%.*s:%u",
                           host_length,
                           uri + host_offset,
                           (unsigned int)port);
    if ((written < 0) || ((size_t)written >= sizeof(s_broker))) {
        snprintf(s_detail, sizeof(s_detail), "broker address is too long");
        return false;
    }

    return true;
}

static bool prepare_lwt(void)
{
    if (!hub_id_is_valid(CONFIG_MODULINO_HUB_ID)) {
        snprintf(s_detail, sizeof(s_detail), "invalid hub_id");
        return false;
    }

    int written = snprintf(s_lwt_topic,
                           sizeof(s_lwt_topic),
                           "modulino/local/v1/%s/status",
                           CONFIG_MODULINO_HUB_ID);
    if ((written < 0) || ((size_t)written >= sizeof(s_lwt_topic))) {
        snprintf(s_detail, sizeof(s_detail), "LWT topic is too long");
        return false;
    }

    written = snprintf(s_lwt_payload,
                       sizeof(s_lwt_payload),
                       "{\n"
                       "  \"schema\": \"modulino.hub_status.v1\",\n"
                       "  \"hub_id\": \"%s\",\n"
                       "  \"status\": \"offline\",\n"
                       "  \"reason\": \"mqtt_lwt\"\n"
                       "}",
                       CONFIG_MODULINO_HUB_ID);
    if ((written < 0) || ((size_t)written >= sizeof(s_lwt_payload))) {
        snprintf(s_detail, sizeof(s_detail), "LWT payload is too long");
        return false;
    }

    return true;
}

static const char *connection_refused_reason(esp_mqtt_connect_return_code_t code)
{
    switch (code) {
    case MQTT_CONNECTION_REFUSE_PROTOCOL:
        return "broker refused protocol";
    case MQTT_CONNECTION_REFUSE_ID_REJECTED:
        return "broker rejected client ID";
    case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE:
        return "broker unavailable";
    case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
        return "broker rejected username";
    case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
        return "broker rejected connection";
    default:
        return "broker refused connection";
    }
}

static void capture_mqtt_error(const esp_mqtt_event_handle_t event)
{
    const esp_mqtt_error_codes_t *error = event->error_handle;
    if (error == NULL) {
        snprintf(s_event_error, sizeof(s_event_error), "MQTT error");
        return;
    }

    if (error->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        snprintf(s_event_error,
                 sizeof(s_event_error),
                 "%s (code=%d)",
                 connection_refused_reason(error->connect_return_code),
                 (int)error->connect_return_code);
        return;
    }

    if (error->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        if (error->esp_tls_last_esp_err != ESP_OK) {
            snprintf(s_event_error,
                     sizeof(s_event_error),
                     "transport: %s (errno=%d)",
                     esp_err_to_name(error->esp_tls_last_esp_err),
                     error->esp_transport_sock_errno);
        } else {
            snprintf(s_event_error,
                     sizeof(s_event_error),
                     "transport error (errno=%d)",
                     error->esp_transport_sock_errno);
        }
        return;
    }

    snprintf(s_event_error, sizeof(s_event_error), "MQTT error type=%d", (int)error->error_type);
}

static void mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    esp_mqtt_event_handle_t event = event_data;
    if (event == NULL) {
        return;
    }

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        if (s_event_group != NULL) {
            xEventGroupSetBits(s_event_group, MQTT_TEST_CONNECTED_BIT);
        }
        break;
    case MQTT_EVENT_ERROR:
        s_connected = false;
        if (s_event_group != NULL) {
            capture_mqtt_error(event);
            xEventGroupSetBits(s_event_group, MQTT_TEST_ERROR_BIT);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        if (s_event_group != NULL) {
            xEventGroupSetBits(s_event_group, MQTT_TEST_DISCONNECTED_BIT);
        }
        break;
    default:
        break;
    }
}

static void delete_event_group(void)
{
    if (s_event_group != NULL) {
        vEventGroupDelete(s_event_group);
        s_event_group = NULL;
    }
}

static void cleanup_event_wait(void)
{
    if (s_event_registered && (s_client != NULL)) {
        esp_mqtt_client_unregister_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler);
        s_event_registered = false;
    }

    delete_event_group();
}

static void destroy_failed_client(bool started)
{
    cleanup_event_wait();

    if (s_client != NULL) {
        if (started) {
            esp_mqtt_client_stop(s_client);
        }
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected = false;
}

void mqtt_test_run(void)
{
    if (s_run_attempted) {
        return;
    }
    s_run_attempted = true;

    if (CONFIG_MODULINO_MQTT_BROKER_URI[0] == '\0') {
        s_result = TEST_RESULT_SKIP;
        snprintf(s_detail, sizeof(s_detail), "broker URI not configured");
        return;
    }

    if (wifi_test_get_result() != TEST_RESULT_PASS) {
        s_result = TEST_RESULT_SKIP;
        snprintf(s_detail, sizeof(s_detail), "Wi-Fi not connected");
        return;
    }

    if (!parse_broker_uri(CONFIG_MODULINO_MQTT_BROKER_URI) || !prepare_lwt()) {
        s_result = TEST_RESULT_FAIL;
        return;
    }

    s_event_group = xEventGroupCreate();
    if (s_event_group == NULL) {
        s_result = TEST_RESULT_FAIL;
        snprintf(s_detail, sizeof(s_detail), "failed to create event group");
        return;
    }

    const esp_mqtt_client_config_t config = {
        .broker.address.uri = CONFIG_MODULINO_MQTT_BROKER_URI,
        .session.last_will.topic = s_lwt_topic,
        .session.last_will.msg = s_lwt_payload,
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
        .session.keepalive = CONFIG_MODULINO_MQTT_KEEPALIVE_SECONDS,
        .network.timeout_ms = CONFIG_MODULINO_MQTT_CONNECT_TIMEOUT_MS,
        .network.disable_auto_reconnect = true,
    };

    s_client = esp_mqtt_client_init(&config);
    if (s_client == NULL) {
        s_result = TEST_RESULT_FAIL;
        snprintf(s_detail, sizeof(s_detail), "esp_mqtt_client_init failed");
        destroy_failed_client(false);
        return;
    }

    esp_err_t err = esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        set_esp_error("MQTT event handler", err);
        destroy_failed_client(false);
        return;
    }
    s_event_registered = true;

    err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        set_esp_error("esp_mqtt_client_start", err);
        destroy_failed_client(false);
        return;
    }

    EventBits_t bits = xEventGroupWaitBits(s_event_group,
                                           MQTT_TEST_CONNECTED_BIT | MQTT_TEST_ERROR_BIT | MQTT_TEST_DISCONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(CONFIG_MODULINO_MQTT_CONNECT_TIMEOUT_MS));

    if ((bits & MQTT_TEST_CONNECTED_BIT) != 0) {
        s_result = TEST_RESULT_PASS;
        snprintf(s_detail, sizeof(s_detail), "broker=%s", s_broker);
        return;
    }

    if ((bits & MQTT_TEST_ERROR_BIT) != 0) {
        s_result = TEST_RESULT_FAIL;
        snprintf(s_detail, sizeof(s_detail), "%s", s_event_error);
    } else if ((bits & MQTT_TEST_DISCONNECTED_BIT) != 0) {
        s_result = TEST_RESULT_FAIL;
        snprintf(s_detail, sizeof(s_detail), "disconnected before MQTT connection");
    } else {
        s_result = TEST_RESULT_FAIL;
        snprintf(s_detail,
                 sizeof(s_detail),
                 "timeout after %d ms",
                 CONFIG_MODULINO_MQTT_CONNECT_TIMEOUT_MS);
    }

    destroy_failed_client(true);
}

test_result_t mqtt_test_get_result(void)
{
    return s_result;
}

const char *mqtt_test_get_detail(void)
{
    return s_detail;
}

esp_mqtt_client_handle_t mqtt_test_get_client(void)
{
    return s_client;
}

bool mqtt_test_is_connected(void)
{
    return s_connected;
}
