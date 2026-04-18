#include "drivers/as608/as608_driver.h"

#include <esp_log.h>

static const char *TAG = "as608_driver";
static uint32_t s_next_id = 1;

/*
 * AS608 ASSUMPTION NOTE:
 * - This stub assumes sensor enrollment produces a numeric template slot ID.
 * - Real hardware flow must implement packet protocol over UART and multi-image enrollment steps.
 * - Template export/import support is vendor-firmware dependent and must be validated against module datasheet.
 */

static bool enroll_impl(uint32_t *out_fingerprint_id)
{
    if (out_fingerprint_id == NULL) {
        return false;
    }
    *out_fingerprint_id = s_next_id++;
    ESP_LOGI(TAG, "Stub enroll created fingerprintId=%lu", (unsigned long)*out_fingerprint_id);
    return true;
}

static bool identify_impl(uint32_t *out_fingerprint_id)
{
    if (out_fingerprint_id == NULL) {
        return false;
    }
    if (s_next_id <= 1) {
        return false;
    }
    *out_fingerprint_id = 1;
    return true;
}

static bool delete_impl(uint32_t fingerprint_id)
{
    ESP_LOGI(TAG, "Stub delete fingerprintId=%lu", (unsigned long)fingerprint_id);
    return true;
}

static bool wipe_all_impl(void)
{
    s_next_id = 1;
    return true;
}

static bool export_template_impl(uint32_t fingerprint_id, uint8_t *buffer, size_t *in_out_len)
{
    (void)fingerprint_id;
    (void)buffer;
    (void)in_out_len;
    return false;
}

static bool import_template_impl(uint32_t fingerprint_id, const uint8_t *buffer, size_t len)
{
    (void)fingerprint_id;
    (void)buffer;
    (void)len;
    return false;
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
