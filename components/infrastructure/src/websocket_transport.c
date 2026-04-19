#include "infrastructure/websocket_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"

#define WS_MSG_MAX 512
#define WS_LIST_IDS_MAX 32
static const char *TAG = "ws_transport";

static esp_websocket_client_handle_t s_client = NULL;
static bool s_connected = false;
static bool s_busy = false;
static device_config_t s_cfg = {0};
static use_case_context_t *s_ctx = NULL;
static char s_active_enroll_request_id[64] = {0};

static int64_t now_epoch_ms_impl(void) { return esp_timer_get_time() / 1000; }
static bool is_connected_impl(void) { return s_connected; }

static bool send_json(const char *json)
{
    if (!s_connected || !s_client || !json) return false;
    return esp_websocket_client_send_text(s_client, json, strlen(json), pdMS_TO_TICKS(500)) >= 0;
}

static bool emit_event_impl(const queue_item_t *item)
{
    if (!item) return false;
    char json[WS_MSG_MAX];
    const char *event = item->type == EVENT_TYPE_CHECKIN ? "fingerprint_match" : "fingerprint_enrolled";
    snprintf(json, sizeof(json),
             "{\"type\":\"event\",\"event\":\"%s\",\"eventId\":\"%s\",\"requestId\":\"%s\",\"deviceId\":\"%s\",\"timestamp\":%lld,\"payload\":{\"fingerprintId\":%lu}}",
             event, item->event_id, item->correlation_id, s_cfg.device_id, (long long)item->timestamp_epoch_ms,
             (unsigned long)item->fingerprint_id);
    return send_json(json);
}

static bool send_operation_result_impl(const operation_result_t *result, const char *correlation_id)
{
    if (!result) return false;
    char json[WS_MSG_MAX];
    snprintf(json, sizeof(json),
             "{\"type\":\"response\",\"event\":\"operation_result\",\"requestId\":\"%s\",\"deviceId\":\"%s\",\"timestamp\":%lld,\"payload\":{\"success\":%s,\"fingerprintId\":%lu,\"code\":\"%s\",\"message\":\"%s\"}}",
             correlation_id ? correlation_id : "", s_cfg.device_id, (long long)now_epoch_ms_impl(),
             result->success ? "true" : "false", (unsigned long)result->fingerprint_id, result->code, result->message);
    return send_json(json);
}

static void send_busy(const char *request_id)
{
    char json[WS_MSG_MAX];
    snprintf(json, sizeof(json),
             "{\"type\":\"error\",\"event\":\"busy\",\"requestId\":\"%s\",\"deviceId\":\"%s\",\"timestamp\":%lld,\"error\":{\"code\":\"BUSY\",\"message\":\"Device is processing another operation\"}}",
             request_id ? request_id : "", s_cfg.device_id, (long long)now_epoch_ms_impl());
    send_json(json);
}

static void send_validation_error(const char *request_id, const char *message)
{
    char json[WS_MSG_MAX];
    snprintf(json, sizeof(json),
             "{\"type\":\"error\",\"event\":\"validation_error\",\"requestId\":\"%s\",\"deviceId\":\"%s\",\"timestamp\":%lld,\"error\":{\"code\":\"INVALID_COMMAND\",\"message\":\"%s\"}}",
             request_id ? request_id : "", s_cfg.device_id, (long long)now_epoch_ms_impl(), message);
    send_json(json);
}

static void send_enroll_step_event(const char *request_id, const char *step, const char *hint)
{
    char json[WS_MSG_MAX];
    snprintf(json, sizeof(json),
             "{\"type\":\"event\",\"event\":\"enroll_progress\",\"requestId\":\"%s\",\"deviceId\":\"%s\",\"timestamp\":%lld,\"payload\":{\"step\":\"%s\",\"hint\":\"%s\"}}",
             request_id ? request_id : "", s_cfg.device_id, (long long)now_epoch_ms_impl(),
             step ? step : "unknown", hint ? hint : "");
    send_json(json);
}

static void enroll_progress_callback(const char *step, void *user_ctx)
{
    (void)user_ctx;
    if (step == NULL || s_active_enroll_request_id[0] == '\0') return;

    if (strcmp(step, "place_finger_first") == 0) {
        send_enroll_step_event(s_active_enroll_request_id, step, "Coloca la huella para primera lectura");
    } else if (strcmp(step, "remove_finger") == 0) {
        send_enroll_step_event(s_active_enroll_request_id, step, "Retira la huella del sensor");
    } else if (strcmp(step, "place_finger_second") == 0) {
        send_enroll_step_event(s_active_enroll_request_id, step, "Coloca la huella para segunda lectura");
    } else if (strcmp(step, "operation_success") == 0) {
        send_enroll_step_event(s_active_enroll_request_id, step, "Registro completado");
    } else if (strcmp(step, "operation_failed") == 0) {
        send_enroll_step_event(s_active_enroll_request_id, step, "Registro fallido");
    } else {
        send_enroll_step_event(s_active_enroll_request_id, step, "");
    }
}

static void send_list_response(const char *request_id, const uint32_t *ids, size_t count, bool truncated)
{
    char ids_buf[WS_MSG_MAX / 2] = {0};
    size_t off = 0;
    for (size_t i = 0; i < count; ++i) {
        int written = snprintf(ids_buf + off, sizeof(ids_buf) - off, "%s%lu", i ? "," : "", (unsigned long)ids[i]);
        if (written < 0 || (size_t)written >= (sizeof(ids_buf) - off)) break;
        off += (size_t)written;
    }

    char json[WS_MSG_MAX];
    snprintf(json, sizeof(json),
             "{\"type\":\"response\",\"event\":\"fingerprints_list\",\"requestId\":\"%s\",\"deviceId\":\"%s\",\"timestamp\":%lld,\"payload\":{\"count\":%u,\"ids\":[%s],\"truncated\":%s}}",
             request_id ? request_id : "", s_cfg.device_id, (long long)now_epoch_ms_impl(), (unsigned)count, ids_buf,
             truncated ? "true" : "false");
    send_json(json);
}

static void handle_command(const char *command, const char *request_id, const char *payload)
{
    if (!s_ctx || !command) return;
    if (strcmp(command, "ping") == 0 || strcmp(command, "healthcheck") == 0) {
        char out[WS_MSG_MAX];
        snprintf(out, sizeof(out),
                 "{\"type\":\"event\",\"event\":\"pong\",\"requestId\":\"%s\",\"deviceId\":\"%s\",\"timestamp\":%lld,\"payload\":{\"online\":true}}",
                 request_id ? request_id : "", s_cfg.device_id, (long long)now_epoch_ms_impl());
        send_json(out);
        return;
    }

    if (s_busy) {
        send_busy(request_id);
        return;
    }

    s_busy = true;
    if (strcmp(command, "enroll_fingerprint") == 0) {
        operation_result_t result = {0};
        if (s_ctx->sensor.set_enroll_progress_callback != NULL) {
            strlcpy(s_active_enroll_request_id, request_id ? request_id : "", sizeof(s_active_enroll_request_id));
            s_ctx->sensor.set_enroll_progress_callback(enroll_progress_callback, NULL);
        }
        bool ok = use_case_register_fingerprint(s_ctx, request_id, false, &result);
        if (s_ctx->sensor.set_enroll_progress_callback != NULL) {
            s_ctx->sensor.set_enroll_progress_callback(NULL, NULL);
            s_active_enroll_request_id[0] = '\0';
        }
        if (!ok) strncpy(result.code, "ENROLL_FAILED", sizeof(result.code) - 1);
        send_operation_result_impl(&result, request_id);
    } else if (strcmp(command, "identify_fingerprint") == 0) {
        uint32_t id = 0;
        bool ok = use_case_check_in_once_with_id(s_ctx, &id);
        queue_item_t item = {.type = ok ? EVENT_TYPE_CHECKIN : EVENT_TYPE_OPERATION_RESULT, .fingerprint_id = id, .timestamp_epoch_ms = now_epoch_ms_impl()};
        strlcpy(item.event_id, ok ? "identify-ok" : "identify-miss", sizeof(item.event_id));
        strlcpy(item.correlation_id, request_id ? request_id : "", sizeof(item.correlation_id));
        emit_event_impl(&item);
    } else if (strcmp(command, "delete_fingerprint") == 0) {
        uint32_t id = (uint32_t)strtoul(payload ? payload : "0", NULL, 10);
        operation_result_t result = {0};
        use_case_delete_fingerprint(s_ctx, id, request_id, &result);
        send_operation_result_impl(&result, request_id);
    } else if (strcmp(command, "wipe_all_fingerprints") == 0) {
        operation_result_t result = {0};
        use_case_wipe_all_fingerprints(s_ctx, request_id, &result);
        send_operation_result_impl(&result, request_id);
    } else if (strcmp(command, "list") == 0 || strcmp(command, "list_fingerprints") == 0) {
        uint32_t ids[WS_LIST_IDS_MAX] = {0};
        size_t found = 0;
        bool ok = use_case_list_registered_fingerprints(s_ctx, ids, WS_LIST_IDS_MAX, &found);
        if (!ok) {
            send_validation_error(request_id, "List fingerprints failed");
        } else {
            send_list_response(request_id, ids, found, false);
        }
    } else {
        send_validation_error(request_id, "Unsupported command");
    }
    s_busy = false;
}

static void parse_and_dispatch(const char *msg)
{
    if (!msg) return;
    char command[64] = {0}, request_id[64] = {0}, payload[32] = {0};
    const char *command_tag = strstr(msg, "\"command\":\"");
    const char *request_tag = strstr(msg, "\"requestId\":\"");
    const char *fingerprint_tag = strstr(msg, "\"fingerprintId\":");
    if (!command_tag) {
        send_validation_error("", "Missing command");
        return;
    }
    sscanf(command_tag, "\"command\":\"%63[^\"]\"", command);
    if (request_tag) sscanf(request_tag, "\"requestId\":\"%63[^\"]\"", request_id);
    if (fingerprint_tag) {
        unsigned long parsed = 0;
        if (sscanf(fingerprint_tag, "\"fingerprintId\":%lu", &parsed) == 1) {
            snprintf(payload, sizeof(payload), "%lu", parsed);
        }
    }
    handle_command(command, request_id, payload[0] ? payload : NULL);
}

static void websocket_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)args; (void)base;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        s_connected = true;
        ESP_LOGI(TAG, "WebSocket connected");
    } else if (event_id == WEBSOCKET_EVENT_DISCONNECTED) {
        s_connected = false;
        ESP_LOGW(TAG, "WebSocket disconnected");
    } else if (event_id == WEBSOCKET_EVENT_DATA && data && data->data_len > 0) {
        char msg[WS_MSG_MAX] = {0};
        int len = data->data_len < (int)sizeof(msg) - 1 ? data->data_len : (int)sizeof(msg) - 1;
        memcpy(msg, data->data_ptr, len);
        msg[len] = '\0';
        parse_and_dispatch(msg);
    }
}

websocket_transport_port_t websocket_transport_port(void)
{
    return (websocket_transport_port_t){
        .is_connected = is_connected_impl,
        .emit_event = emit_event_impl,
        .send_operation_result = send_operation_result_impl,
    };
}

clock_port_t websocket_transport_clock_port(void) { return (clock_port_t){.now_epoch_ms = now_epoch_ms_impl}; }

bool websocket_transport_start(const device_config_t *cfg, use_case_context_t *ctx)
{
    if (!cfg || !ctx || !cfg->websocket_host[0] || !cfg->device_id[0]) return false;
    s_cfg = *cfg;
    s_ctx = ctx;

    char uri[256] = {0};
    uint16_t port = s_cfg.websocket_port ? s_cfg.websocket_port : (s_cfg.tls_enabled ? 443 : 8080);
    const char *path = s_cfg.websocket_path[0] ? s_cfg.websocket_path : "/device";
    snprintf(uri, sizeof(uri), "%s://%s:%u%s", s_cfg.tls_enabled ? "wss" : "ws", s_cfg.websocket_host, port, path);

    esp_websocket_client_config_t ws_cfg = {.uri = uri, .reconnect_timeout_ms = 3000, .network_timeout_ms = 5000};
    if (s_client) {
        esp_websocket_client_stop(s_client);
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }
    s_client = esp_websocket_client_init(&ws_cfg);
    if (!s_client) return false;
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
    return esp_websocket_client_start(s_client) == ESP_OK;
}

void websocket_transport_stop(void)
{
    if (!s_client) return;
    esp_websocket_client_stop(s_client);
    esp_websocket_client_destroy(s_client);
    s_client = NULL;
    s_connected = false;
}
