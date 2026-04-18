#include "infrastructure/mqtt_adapter.h"

#include <inttypes.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define MQTT_TOPIC_MAX 192
#define MQTT_PAYLOAD_MAX 512
#define MQTT_COMMAND_QUEUE_LEN 8
#define MQTT_COMMAND_TASK_STACK 6144
#define MQTT_COMMAND_TASK_PRIO 5

static const char *TAG = "mqtt_adapter";

typedef struct {
    char topic[MQTT_TOPIC_MAX];
    char payload[MQTT_PAYLOAD_MAX];
} mqtt_command_msg_t;

typedef struct {
    char events_checkin[MQTT_TOPIC_MAX];
    char events_register_result[MQTT_TOPIC_MAX];
    char events_operation_result[MQTT_TOPIC_MAX];
    char events_progress[MQTT_TOPIC_MAX];
    char status_heartbeat[MQTT_TOPIC_MAX];
    char commands_prefix[MQTT_TOPIC_MAX];
    char commands_wildcard[MQTT_TOPIC_MAX];
} mqtt_topics_t;

static bool s_connected = false;
static esp_mqtt_client_handle_t s_client = NULL;
static TaskHandle_t s_command_task = NULL;
static QueueHandle_t s_command_queue = NULL;
static use_case_context_t *s_ctx = NULL;
static device_config_t s_cfg = {0};
static mqtt_topics_t s_topics = {0};
static char s_broker_uri[MQTT_TOPIC_MAX] = {0};

static bool starts_with(const char *value, const char *prefix)
{
    if (value == NULL || prefix == NULL) {
        return false;
    }

    while (*prefix != '\0') {
        if (*value == '\0' || *value != *prefix) {
            return false;
        }
        value++;
        prefix++;
    }

    return true;
}

static void compose_topics(const device_config_t *cfg)
{
    snprintf(s_topics.events_checkin,
             sizeof(s_topics.events_checkin),
             "%s/devices/%s/events/checkin",
             cfg->mqtt_topic_prefix,
             cfg->device_id);
    snprintf(s_topics.events_register_result,
             sizeof(s_topics.events_register_result),
             "%s/devices/%s/events/register-result",
             cfg->mqtt_topic_prefix,
             cfg->device_id);
    snprintf(s_topics.events_operation_result,
             sizeof(s_topics.events_operation_result),
             "%s/devices/%s/events/operation-result",
             cfg->mqtt_topic_prefix,
             cfg->device_id);
    snprintf(s_topics.events_progress,
             sizeof(s_topics.events_progress),
             "%s/devices/%s/events/progress",
             cfg->mqtt_topic_prefix,
             cfg->device_id);
    snprintf(s_topics.status_heartbeat,
             sizeof(s_topics.status_heartbeat),
             "%s/devices/%s/status/heartbeat",
             cfg->mqtt_topic_prefix,
             cfg->device_id);
    snprintf(s_topics.commands_prefix,
             sizeof(s_topics.commands_prefix),
             "%s/devices/%s/commands/",
             cfg->mqtt_topic_prefix,
             cfg->device_id);
    snprintf(s_topics.commands_wildcard,
             sizeof(s_topics.commands_wildcard),
             "%s/devices/%s/commands/#",
             cfg->mqtt_topic_prefix,
             cfg->device_id);
}

static void mqtt_publish_json(const char *topic, const char *payload)
{
    if (!s_connected || s_client == NULL || topic == NULL || payload == NULL) {
        return;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Publish failed topic=%s", topic);
    }
}

static void publish_progress(const char *command,
                             const char *stage,
                             const char *status,
                             const char *correlation_id,
                             const char *message)
{
    char payload[MQTT_PAYLOAD_MAX];

    snprintf(payload,
             sizeof(payload),
             "{\"deviceId\":\"%s\",\"command\":\"%s\",\"stage\":\"%s\",\"status\":\"%s\",\"correlationId\":\"%s\",\"message\":\"%s\",\"timestampMs\":%" PRId64 "}",
             s_cfg.device_id,
             command ? command : "",
             stage ? stage : "",
             status ? status : "",
             correlation_id ? correlation_id : "",
             message ? message : "",
             (int64_t)(esp_timer_get_time() / 1000));

    mqtt_publish_json(s_topics.events_progress, payload);
}

static void publish_heartbeat(void)
{
    char payload[MQTT_PAYLOAD_MAX];
    size_t queue_depth = 0;

    if (s_ctx != NULL && s_ctx->queue_repo.size != NULL) {
        queue_depth = s_ctx->queue_repo.size();
    }

    snprintf(payload,
             sizeof(payload),
             "{\"deviceId\":\"%s\",\"online\":true,\"queueDepth\":%u,\"timestampMs\":%" PRId64 "}",
             s_cfg.device_id,
             (unsigned int)queue_depth,
             (int64_t)(esp_timer_get_time() / 1000));

    mqtt_publish_json(s_topics.status_heartbeat, payload);
}

static bool json_extract_string_field(const char *json,
                                      const char *field,
                                      char *out,
                                      size_t out_len)
{
    char pattern[64];
    const char *field_pos;
    const char *start;
    const char *end;
    size_t copy_len;

    if (json == NULL || field == NULL || out == NULL || out_len == 0) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", field);
    field_pos = strstr(json, pattern);
    if (field_pos == NULL) {
        return false;
    }

    start = strchr(field_pos + strlen(pattern), ':');
    if (start == NULL) {
        return false;
    }

    start++;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (*start != '"') {
        return false;
    }

    start++;
    end = strchr(start, '"');
    if (end == NULL) {
        return false;
    }

    copy_len = (size_t)(end - start);
    if (copy_len >= out_len) {
        copy_len = out_len - 1;
    }

    memcpy(out, start, copy_len);
    out[copy_len] = '\0';
    return true;
}

static bool json_extract_uint32_field(const char *json,
                                      const char *field,
                                      uint32_t *out_value)
{
    char pattern[64];
    const char *field_pos;
    const char *start;
    char *endptr = NULL;
    unsigned long parsed;

    if (json == NULL || field == NULL || out_value == NULL) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", field);
    field_pos = strstr(json, pattern);
    if (field_pos == NULL) {
        return false;
    }

    start = strchr(field_pos + strlen(pattern), ':');
    if (start == NULL) {
        return false;
    }

    start++;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    parsed = strtoul(start, &endptr, 10);
    if (endptr == start) {
        return false;
    }

    *out_value = (uint32_t)parsed;
    return true;
}

static bool parse_payload_common(const char *payload,
                                 char *out_correlation_id,
                                 size_t correlation_id_len,
                                 uint32_t *out_fingerprint_id)
{
    if (payload == NULL) {
        return false;
    }

    if (out_correlation_id != NULL) {
        if (!json_extract_string_field(payload, "correlationId", out_correlation_id, correlation_id_len)) {
            out_correlation_id[0] = '\0';
        }
    }

    if (out_fingerprint_id != NULL) {
        if (!json_extract_uint32_field(payload, "fingerprintId", out_fingerprint_id)) {
            return false;
        }
    }

    return true;
}

static bool publish_event_impl(const queue_item_t *item)
{
    char payload[MQTT_PAYLOAD_MAX];
    const char *topic = s_topics.events_progress;

    if (item == NULL) {
        return false;
    }

    if (item->type == EVENT_TYPE_CHECKIN) {
        topic = s_topics.events_checkin;
        snprintf(payload,
                 sizeof(payload),
                 "{\"eventId\":\"%s\",\"deviceId\":\"%s\",\"fingerprintId\":%lu,\"timestampMs\":%" PRId64 ",\"source\":\"sensor\"}",
                 item->event_id,
                 s_cfg.device_id,
                 (unsigned long)item->fingerprint_id,
                 item->timestamp_epoch_ms);
    } else if (item->type == EVENT_TYPE_REGISTER_RESULT) {
        topic = s_topics.events_register_result;
        snprintf(payload,
                 sizeof(payload),
                 "{\"eventId\":\"%s\",\"correlationId\":\"%s\",\"deviceId\":\"%s\",\"fingerprintId\":%lu,\"status\":\"success\",\"timestampMs\":%" PRId64 "}",
                 item->event_id,
                 item->correlation_id,
                 s_cfg.device_id,
                 (unsigned long)item->fingerprint_id,
                 item->timestamp_epoch_ms);
    } else {
        snprintf(payload,
                 sizeof(payload),
                 "{\"eventId\":\"%s\",\"deviceId\":\"%s\",\"fingerprintId\":%lu,\"timestampMs\":%" PRId64 "}",
                 item->event_id,
                 s_cfg.device_id,
                 (unsigned long)item->fingerprint_id,
                 item->timestamp_epoch_ms);
    }

    mqtt_publish_json(topic, payload);
    return s_connected;
}

static bool publish_operation_result_impl(const operation_result_t *result, const char *correlation_id)
{
    char payload[MQTT_PAYLOAD_MAX];

    if (result == NULL) {
        return false;
    }

    snprintf(payload,
             sizeof(payload),
             "{\"deviceId\":\"%s\",\"correlationId\":\"%s\",\"fingerprintId\":%lu,\"status\":\"%s\",\"code\":\"%s\",\"message\":\"%s\",\"timestampMs\":%" PRId64 "}",
             s_cfg.device_id,
             correlation_id ? correlation_id : "",
             (unsigned long)result->fingerprint_id,
             result->success ? "success" : "error",
             result->code,
             result->message,
             (int64_t)(esp_timer_get_time() / 1000));

    mqtt_publish_json(s_topics.events_operation_result, payload);
    return s_connected;
}

static bool is_connected_impl(void)
{
    return s_connected;
}

static int64_t now_epoch_ms_impl(void)
{
    return esp_timer_get_time() / 1000;
}

static void execute_command_message(const mqtt_command_msg_t *msg)
{
    const char *command = NULL;
    char correlation_id[DOMAIN_EVENT_ID_MAX] = {0};
    operation_result_t result = {0};
    uint32_t fingerprint_id = 0;

    if (msg == NULL || s_ctx == NULL) {
        return;
    }

    if (!starts_with(msg->topic, s_topics.commands_prefix)) {
        return;
    }

    command = msg->topic + strlen(s_topics.commands_prefix);

    if (strcmp(command, "register/start") == 0) {
        parse_payload_common(msg->payload, correlation_id, sizeof(correlation_id), NULL);
        publish_progress("register/start", "start", "progress", correlation_id, "Enrollment started");
        if (use_case_register_fingerprint(s_ctx, correlation_id, false, &result)) {
            publish_progress("register/start", "finish", "success", correlation_id, "Enrollment finished");
        } else {
            publish_progress("register/start", "finish", "error", correlation_id, "Enrollment failed");
            s_ctx->mqtt.publish_operation_result(&result, correlation_id);
        }
        return;
    }

    if (strcmp(command, "checkin/once") == 0) {
        parse_payload_common(msg->payload, correlation_id, sizeof(correlation_id), NULL);
        publish_progress("checkin/once", "start", "progress", correlation_id, "Check-in attempt started");

        if (use_case_check_in_once_with_id(s_ctx, &fingerprint_id)) {
            result = (operation_result_t){.success = true, .fingerprint_id = fingerprint_id};
            strlcpy(result.code, "OK", sizeof(result.code));
            strlcpy(result.message, "Check-in matched", sizeof(result.message));
            s_ctx->mqtt.publish_operation_result(&result, correlation_id);
            publish_progress("checkin/once", "finish", "success", correlation_id, "Check-in matched");
        } else {
            result = (operation_result_t){.success = false, .fingerprint_id = 0};
            strlcpy(result.code, "NO_MATCH", sizeof(result.code));
            strlcpy(result.message, "Check-in without match", sizeof(result.message));
            s_ctx->mqtt.publish_operation_result(&result, correlation_id);
            publish_progress("checkin/once", "finish", "error", correlation_id, "Check-in without match");
        }
        return;
    }

    if (strcmp(command, "fingerprint/delete") == 0) {
        if (!parse_payload_common(msg->payload, correlation_id, sizeof(correlation_id), &fingerprint_id)) {
            publish_progress("fingerprint/delete", "validate", "error", "", "Invalid payload, fingerprintId required");
            return;
        }
        publish_progress("fingerprint/delete", "start", "progress", correlation_id, "Delete started");
        if (use_case_delete_fingerprint(s_ctx, fingerprint_id, correlation_id, &result)) {
            publish_progress("fingerprint/delete", "finish", "success", correlation_id, "Delete completed");
        } else {
            publish_progress("fingerprint/delete", "finish", "error", correlation_id, "Delete failed");
        }
        return;
    }

    if (strcmp(command, "fingerprint/wipe-all") == 0) {
        parse_payload_common(msg->payload, correlation_id, sizeof(correlation_id), NULL);
        publish_progress("fingerprint/wipe-all", "start", "progress", correlation_id, "Wipe-all started");
        if (use_case_wipe_all_fingerprints(s_ctx, correlation_id, &result)) {
            publish_progress("fingerprint/wipe-all", "finish", "success", correlation_id, "Wipe-all completed");
        } else {
            publish_progress("fingerprint/wipe-all", "finish", "error", correlation_id, "Wipe-all failed");
        }
        return;
    }

    if (strcmp(command, "fingerprint/list") == 0) {
        uint32_t ids[64] = {0};
        size_t count = 0;

        parse_payload_common(msg->payload, correlation_id, sizeof(correlation_id), NULL);
        publish_progress("fingerprint/list", "start", "progress", correlation_id, "List started");

        if (use_case_list_registered_fingerprints(s_ctx, ids, 64, &count)) {
            snprintf(result.message, sizeof(result.message), "Listed %u fingerprints", (unsigned int)count);
            result.success = true;
            result.fingerprint_id = (count > 0) ? ids[0] : 0;
            strlcpy(result.code, "OK", sizeof(result.code));
            s_ctx->mqtt.publish_operation_result(&result, correlation_id);
            publish_progress("fingerprint/list", "finish", "success", correlation_id, result.message);
        } else {
            result = (operation_result_t){.success = false};
            strlcpy(result.code, "LIST_FAILED", sizeof(result.code));
            strlcpy(result.message, "Could not list fingerprints", sizeof(result.message));
            s_ctx->mqtt.publish_operation_result(&result, correlation_id);
            publish_progress("fingerprint/list", "finish", "error", correlation_id, result.message);
        }
        return;
    }

    publish_progress(command, "validate", "error", "", "Unsupported command topic");
}

static void mqtt_command_task(void *arg)
{
    mqtt_command_msg_t msg = {0};
    (void)arg;

    for (;;) {
        if (xQueueReceive(s_command_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        execute_command_message(&msg);

        if (s_ctx != NULL) {
            use_case_process_pending_queue(s_ctx, 8);
        }
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    (void)handler_args;
    (void)base;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        esp_mqtt_client_subscribe(s_client, s_topics.commands_wildcard, 1);
        publish_heartbeat();
        if (s_ctx != NULL) {
            use_case_process_pending_queue(s_ctx, 32);
        }
        ESP_LOGI(TAG, "MQTT connected and subscribed topic=%s", s_topics.commands_wildcard);
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;

    case MQTT_EVENT_DATA:
        if (s_command_queue != NULL) {
            mqtt_command_msg_t cmd = {0};
            int topic_len = event->topic_len < (int)(sizeof(cmd.topic) - 1) ? event->topic_len : (int)(sizeof(cmd.topic) - 1);
            int data_len = event->data_len < (int)(sizeof(cmd.payload) - 1) ? event->data_len : (int)(sizeof(cmd.payload) - 1);

            memcpy(cmd.topic, event->topic, topic_len);
            cmd.topic[topic_len] = '\0';
            memcpy(cmd.payload, event->data, data_len);
            cmd.payload[data_len] = '\0';

            if (xQueueSend(s_command_queue, &cmd, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Dropped MQTT command due to full queue topic=%s", cmd.topic);
            }
        }
        break;

    default:
        break;
    }
}

mqtt_port_t mqtt_adapter_port(void)
{
    return (mqtt_port_t){
        .is_connected = is_connected_impl,
        .publish_event = publish_event_impl,
        .publish_operation_result = publish_operation_result_impl,
    };
}

clock_port_t mqtt_adapter_clock_port(void)
{
    return (clock_port_t){.now_epoch_ms = now_epoch_ms_impl};
}

bool mqtt_adapter_start(const device_config_t *cfg, use_case_context_t *ctx)
{
    esp_mqtt_client_config_t mqtt_cfg = {0};

    if (cfg == NULL || ctx == NULL) {
        ESP_LOGW(TAG, "MQTT start skipped: cfg/context missing");
        return false;
    }

    if (cfg->mqtt_broker_host[0] == '\0' || cfg->device_id[0] == '\0' || cfg->mqtt_topic_prefix[0] == '\0') {
        ESP_LOGW(TAG,
                 "MQTT start skipped: host='%s' deviceId='%s' topicPrefix='%s'",
                 cfg->mqtt_broker_host,
                 cfg->device_id,
                 cfg->mqtt_topic_prefix);
        return false;
    }

    s_cfg = *cfg;
    if (s_cfg.mqtt_port == 0) {
        s_cfg.mqtt_port = 1883;
    }
    s_ctx = ctx;
    compose_topics(&s_cfg);

    if (s_command_queue == NULL) {
        s_command_queue = xQueueCreate(MQTT_COMMAND_QUEUE_LEN, sizeof(mqtt_command_msg_t));
        if (s_command_queue == NULL) {
            ESP_LOGE(TAG, "Could not create MQTT command queue");
            return false;
        }
    }

    if (s_command_task == NULL) {
        if (xTaskCreate(mqtt_command_task,
                        "mqtt_cmd",
                        MQTT_COMMAND_TASK_STACK,
                        NULL,
                        MQTT_COMMAND_TASK_PRIO,
                        &s_command_task) != pdPASS) {
            ESP_LOGE(TAG, "Could not create MQTT command task");
            return false;
        }
    }

    snprintf(s_broker_uri, sizeof(s_broker_uri), "mqtt://%s:%u", s_cfg.mqtt_broker_host, s_cfg.mqtt_port);
    mqtt_cfg.broker.address.uri = s_broker_uri;
    mqtt_cfg.credentials.client_id = s_cfg.device_id;
    mqtt_cfg.credentials.username = s_cfg.device_id;

    if (s_cfg.mqtt_auth_token[0] != '\0') {
        mqtt_cfg.credentials.authentication.password = s_cfg.mqtt_auth_token;
    }

    if (s_client != NULL) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return false;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    if (esp_mqtt_client_start(s_client) != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start failed");
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return false;
    }

    ESP_LOGI(TAG,
             "MQTT transport started uri=%s host=%s port=%u prefix=%s device=%s",
             s_broker_uri,
             s_cfg.mqtt_broker_host,
             s_cfg.mqtt_port,
             s_cfg.mqtt_topic_prefix,
             s_cfg.device_id);
    return true;
}

void mqtt_adapter_stop(void)
{
    s_connected = false;

    if (s_client != NULL) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
}
