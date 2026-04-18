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

    *out_fingerprint_id = (uint32_t)slot + 1U;
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

    *out_fingerprint_id = (uint32_t)result.slot + 1U;
    return true;
}

static bool delete_impl(uint32_t fingerprint_id)
{
    as608_status_t st;

    if (!ensure_ctx() || (fingerprint_id == 0U)) {
        return false;
    }

    st = as608_delete(&s_ctx, (uint16_t)(fingerprint_id - 1U));
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

static bool list_fingerprints_impl(uint32_t *out_ids, size_t max_ids, size_t *out_count)
{
    uint16_t page_count;
    size_t found = 0;

    if ((out_count == NULL) || !ensure_ctx()) {
        return false;
    }

    page_count = (uint16_t)((s_ctx.max_templates + 255U) / 256U);

    for (uint16_t p = 0; p < page_count; ++p) {
        as608_index_page_t page = {0};
        if (as608_read_index_table(&s_ctx, (uint8_t)p, &page) != AS608_OK) {
            return false;
        }
        for (uint16_t byte_i = 0; byte_i < AS608_INDEX_TABLE_PAGE_SIZE; ++byte_i) {
            uint8_t b = page.bits[byte_i];
            if (b == 0U) {
                continue;
            }
            for (uint8_t bit = 0; bit < 8U; ++bit) {
                if ((b & (1U << bit)) != 0U) {
                    uint16_t slot = (uint16_t)(p * 256U + byte_i * 8U + bit);
                    if (slot >= s_ctx.max_templates) {
                        continue;
                    }
                    if ((out_ids != NULL) && (found < max_ids)) {
                        /* Public app IDs stay 1-based for backward compatibility. */
                        out_ids[found] = (uint32_t)slot + 1U;
                    }
                    found++;
                }
            }
        }
    }

    *out_count = found;
    return true;
}

static bool export_template_impl(uint32_t fingerprint_id, uint8_t *buffer, size_t *in_out_len)
{
    return ensure_ctx() &&
           (fingerprint_id > 0U) &&
           as608_export_template(&s_ctx, (uint16_t)(fingerprint_id - 1U), buffer, in_out_len) == AS608_OK;
}

static bool import_template_impl(uint32_t fingerprint_id, const uint8_t *buffer, size_t len)
{
    return ensure_ctx() &&
           (fingerprint_id > 0U) &&
           as608_import_template(&s_ctx, (uint16_t)(fingerprint_id - 1U), buffer, len) == AS608_OK;
}

fingerprint_sensor_port_t as608_driver_port(void)
{
    return (fingerprint_sensor_port_t){
        .enroll = enroll_impl,
        .identify = identify_impl,
        .delete_fingerprint = delete_impl,
        .wipe_all = wipe_all_impl,
        .list_fingerprints = list_fingerprints_impl,
        .export_template = export_template_impl,
        .import_template = import_template_impl,
    };
}
