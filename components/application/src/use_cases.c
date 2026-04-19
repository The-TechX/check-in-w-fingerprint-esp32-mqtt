#include "application/use_cases.h"
#include <stdio.h>
#include <string.h>

static void compose_event_id(char *out, size_t out_len, const char *prefix, int64_t ts)
{
    snprintf(out, out_len, "%s-%lld", prefix, (long long)ts);
}

bool use_case_register_fingerprint(use_case_context_t *ctx, const char *correlation_id, bool initiated_from_web_ui, operation_result_t *out_result)
{
    uint32_t fingerprint_id = 0;
    if (!ctx->sensor.enroll(&fingerprint_id)) {
        if (out_result) {
            *out_result = (operation_result_t){ .success = false, .fingerprint_id = 0 };
            strncpy(out_result->code, "ENROLL_FAILED", sizeof(out_result->code) - 1);
        }
        return false;
    }

    queue_item_t event = {0};
    event.type = EVENT_TYPE_REGISTER_RESULT;
    event.fingerprint_id = fingerprint_id;
    event.timestamp_epoch_ms = ctx->clock.now_epoch_ms();
    strncpy(event.correlation_id, correlation_id ? correlation_id : "", sizeof(event.correlation_id) - 1);
    compose_event_id(event.event_id, sizeof(event.event_id), initiated_from_web_ui ? "webreg" : "remreg", event.timestamp_epoch_ms);

    bool sent_ok = ctx->ws.is_connected() ? ctx->ws.emit_event(&event) : false;
    if (!sent_ok) {
        ctx->queue_repo.enqueue(&event);
    }

    if (out_result) {
        *out_result = (operation_result_t){ .success = true, .fingerprint_id = fingerprint_id };
        strncpy(out_result->code, "OK", sizeof(out_result->code) - 1);
    }
    return true;
}

bool use_case_check_in_once(use_case_context_t *ctx)
{
    return use_case_check_in_once_with_id(ctx, NULL);
}

bool use_case_check_in_once_with_id(use_case_context_t *ctx, uint32_t *out_fingerprint_id)
{
    uint32_t fingerprint_id = 0;
    if (!ctx->sensor.identify(&fingerprint_id)) {
        return false;
    }

    queue_item_t event = {0};
    event.type = EVENT_TYPE_CHECKIN;
    event.fingerprint_id = fingerprint_id;
    event.timestamp_epoch_ms = ctx->clock.now_epoch_ms();
    compose_event_id(event.event_id, sizeof(event.event_id), "checkin", event.timestamp_epoch_ms);

    if (ctx->ws.is_connected() && ctx->ws.emit_event(&event)) {
        if (out_fingerprint_id != NULL) {
            *out_fingerprint_id = fingerprint_id;
        }
        return true;
    }

    if (out_fingerprint_id != NULL) {
        *out_fingerprint_id = fingerprint_id;
    }
    return ctx->queue_repo.enqueue(&event);
}

bool use_case_delete_fingerprint(use_case_context_t *ctx, uint32_t fingerprint_id, const char *correlation_id, operation_result_t *out_result)
{
    bool ok = ctx->sensor.delete_fingerprint(fingerprint_id);
    operation_result_t result = {0};
    result.success = ok;
    result.fingerprint_id = fingerprint_id;
    strncpy(result.code, ok ? "OK" : "DELETE_FAILED", sizeof(result.code) - 1);

    if (ctx->ws.is_connected()) {
        ctx->ws.send_operation_result(&result, correlation_id);
    }
    if (out_result) {
        *out_result = result;
    }
    return ok;
}

bool use_case_wipe_all_fingerprints(use_case_context_t *ctx, const char *correlation_id, operation_result_t *out_result)
{
    bool ok = ctx->sensor.wipe_all();
    operation_result_t result = {0};

    result.success = ok;
    result.fingerprint_id = 0;
    strncpy(result.code, ok ? "OK" : "WIPE_FAILED", sizeof(result.code) - 1);

    if (ctx->ws.is_connected()) {
        ctx->ws.send_operation_result(&result, correlation_id);
    }
    if (out_result) {
        *out_result = result;
    }
    return ok;
}

bool use_case_list_registered_fingerprints(use_case_context_t *ctx, uint32_t *out_ids, size_t max_ids, size_t *out_count)
{
    if ((ctx == NULL) || (ctx->sensor.list_fingerprints == NULL) || (out_count == NULL)) {
        return false;
    }

    return ctx->sensor.list_fingerprints(out_ids, max_ids, out_count);
}

bool use_case_process_pending_queue(use_case_context_t *ctx, size_t max_items_to_flush)
{
    if (!ctx->ws.is_connected()) {
        return false;
    }

    size_t processed = 0;
    queue_item_t item = {0};
    while (processed < max_items_to_flush && ctx->queue_repo.peek(&item)) {
        if (!ctx->ws.emit_event(&item)) {
            return false;
        }
        ctx->queue_repo.ack(item.event_id);
        processed++;
    }

    return true;
}
