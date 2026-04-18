#pragma once

#include "drivers/as608/as608_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void as608_default_config(as608_config_t *cfg);

as608_status_t as608_init(as608_t *ctx, const as608_config_t *cfg);
as608_status_t as608_deinit(as608_t *ctx);

as608_status_t as608_capture_image(as608_t *ctx);
as608_status_t as608_image_to_char(as608_t *ctx, uint8_t buffer_id);
as608_status_t as608_create_model(as608_t *ctx);
as608_status_t as608_store_model(as608_t *ctx, uint8_t buffer_id, uint16_t slot);
as608_status_t as608_search(as608_t *ctx, uint8_t buffer_id, uint16_t start_slot, uint16_t count,
                            as608_match_result_t *out_result);
as608_status_t as608_match(as608_t *ctx, uint16_t *out_score);

as608_status_t as608_delete(as608_t *ctx, uint16_t slot);
as608_status_t as608_delete_many(as608_t *ctx, uint16_t start_slot, uint16_t count);
as608_status_t as608_empty_database(as608_t *ctx);

as608_status_t as608_get_template_count(as608_t *ctx, uint16_t *out_count);
as608_status_t as608_read_index_table(as608_t *ctx, uint8_t page, as608_index_page_t *out_page);
as608_status_t as608_find_free_slot(as608_t *ctx, uint16_t *out_slot);

as608_status_t as608_wait_finger_present(as608_t *ctx, uint32_t timeout_ms, uint32_t poll_interval_ms);
as608_status_t as608_wait_finger_removed(as608_t *ctx, uint32_t timeout_ms, uint32_t poll_interval_ms);
as608_status_t as608_enroll(as608_t *ctx, uint16_t preferred_slot, uint32_t timeout_ms, uint16_t *out_slot);
as608_status_t as608_identify(as608_t *ctx, uint32_t timeout_ms, as608_match_result_t *out_result);
as608_status_t as608_verify_slot(as608_t *ctx, uint16_t slot, uint32_t timeout_ms, uint16_t *out_score);

as608_status_t as608_export_template(as608_t *ctx, uint16_t slot, uint8_t *buffer, size_t *in_out_len);
as608_status_t as608_import_template(as608_t *ctx, uint16_t slot, const uint8_t *buffer, size_t len);

const char *as608_status_str(as608_status_t status);

#ifdef __cplusplus
}
#endif
