#include "infrastructure/mqtt_adapter.h"

#include <esp_log.h>
#include <esp_timer.h>

static const char *TAG = "mqtt_adapter";
static bool s_connected = false;

static bool is_connected_impl(void)
{
    return s_connected;
}

static bool publish_event_impl(const queue_item_t *item)
{
    ESP_LOGI(TAG, "Stub publish event type=%d fingerprint=%lu eventId=%s", (int)item->type,
             (unsigned long)item->fingerprint_id, item->event_id);
    return s_connected;
}

static bool publish_operation_result_impl(const operation_result_t *result, const char *correlation_id)
{
    ESP_LOGI(TAG, "Stub publish result success=%d fingerprint=%lu corr=%s", result->success,
             (unsigned long)result->fingerprint_id, correlation_id ? correlation_id : "");
    return s_connected;
}

static int64_t now_epoch_ms_impl(void)
{
    return esp_timer_get_time() / 1000;
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
    return (clock_port_t){ .now_epoch_ms = now_epoch_ms_impl };
}
