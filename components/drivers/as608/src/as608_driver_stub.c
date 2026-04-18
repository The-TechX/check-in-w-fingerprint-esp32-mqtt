#include "drivers/as608/as608_driver.h"

#include "drivers/as608/as608.h"
#include "esp_log.h"

static const char *TAG = "as608_driver";

static as608_t s_ctx;
static bool s_ctx_ready;

static bool ensure_ctx(void)
{
    as608_status_t st;
    as608_config_t cfg;

    if (s_ctx_ready) {
        return true;
    }

    as608_default_config(&cfg);
    st = as608_init(&s_ctx, &cfg);
    if (st != AS608_OK) {
        ESP_LOGE(TAG, "as608_init failed: %s", as608_status_str(st));
        return false;
    }

    s_ctx_ready = true;
    return true;
}

static bool enroll_impl(uint32_t *out_fingerprint_id)
{
    uint16_t slot = 0;
    as608_status_t st;

    if ((out_fingerprint_id == NULL) || !ensure_ctx()) {
        return false;
    }

    st = as608_enroll(&s_ctx, UINT16_MAX, 10000U, &slot);
    if (st != AS608_OK) {
        ESP_LOGW(TAG, "enroll failed: %s", as608_status_str(st));
        return false;
    }

    *out_fingerprint_id = slot;
    return true;
}

static bool identify_impl(uint32_t *out_fingerprint_id)
{
    as608_match_result_t result;
    as608_status_t st;

    if ((out_fingerprint_id == NULL) || !ensure_ctx()) {
        return false;
    }

    st = as608_identify(&s_ctx, 900U, &result);
    if (st != AS608_OK) {
        ESP_LOGW(TAG, "identify failed: %s", as608_status_str(st));
        return false;
    }

    *out_fingerprint_id = result.slot;
    return true;
}

static bool delete_impl(uint32_t fingerprint_id)
{
    as608_status_t st;

    if (!ensure_ctx()) {
        return false;
    }

    st = as608_delete(&s_ctx, (uint16_t)fingerprint_id);
    if (st != AS608_OK) {
        ESP_LOGW(TAG, "delete failed: %s", as608_status_str(st));
        return false;
    }

    return true;
}

static bool wipe_all_impl(void)
{
    as608_status_t st;

    if (!ensure_ctx()) {
        return false;
    }

    st = as608_empty_database(&s_ctx);
    if (st != AS608_OK) {
        ESP_LOGW(TAG, "wipe_all failed: %s", as608_status_str(st));
        return false;
    }

    return true;
}

static bool export_template_impl(uint32_t fingerprint_id, uint8_t *buffer, size_t *in_out_len)
{
    return ensure_ctx() &&
           as608_export_template(&s_ctx, (uint16_t)fingerprint_id, buffer, in_out_len) == AS608_OK;
}

static bool import_template_impl(uint32_t fingerprint_id, const uint8_t *buffer, size_t len)
{
    return ensure_ctx() &&
           as608_import_template(&s_ctx, (uint16_t)fingerprint_id, buffer, len) == AS608_OK;
}

fingerprint_sensor_port_t as608_driver_port(void)
{
    return (fingerprint_sensor_port_t){
        .enroll = enroll_impl,
        .identify = identify_impl,
        .delete_fingerprint = delete_impl,
        .wipe_all = wipe_all_impl,
        .export_template = export_template_impl,
        .import_template = import_template_impl,
    };
}
