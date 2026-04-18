#pragma once

#include <stdbool.h>
#include <string.h>
#include "application/use_cases.h"

typedef struct {
    bool ws_connected;
    bool ws_send_ok;
    bool enroll_ok;
    bool identify_ok;
    bool delete_ok;
    int64_t now_ms;
    size_t queue_size;
    queue_item_t queue[8];
    bool sent_called;
    queue_item_t last_sent;
} fake_env_t;

static fake_env_t g_fake;

static bool fake_enqueue(const queue_item_t *item) { g_fake.queue[g_fake.queue_size++] = *item; return true; }
static bool fake_peek(queue_item_t *out_item) { if (!g_fake.queue_size) return false; *out_item = g_fake.queue[0]; return true; }
static bool fake_ack(const char *event_id) { (void)event_id; if (!g_fake.queue_size) return false; memmove(&g_fake.queue[0], &g_fake.queue[1], (g_fake.queue_size-1)*sizeof(queue_item_t)); g_fake.queue_size--; return true; }
static size_t fake_size(void) { return g_fake.queue_size; }
static bool fake_clear(void) { g_fake.queue_size = 0; return true; }
static bool fake_load(device_config_t *cfg) { (void)cfg; return false; }
static bool fake_save(const device_config_t *cfg) { (void)cfg; return true; }
static bool fake_reset(void) { return true; }
static bool fake_ws_connected(void) { return g_fake.ws_connected; }
static bool fake_emit(const queue_item_t *item) { g_fake.sent_called=true; g_fake.last_sent=*item; return g_fake.ws_send_ok; }
static bool fake_send_result(const operation_result_t *result, const char *corr) { (void)result; (void)corr; return true; }
static bool fake_enroll(uint32_t *out) { if (!g_fake.enroll_ok) return false; *out=42; return true; }
static bool fake_identify(uint32_t *out) { if (!g_fake.identify_ok) return false; *out=7; return true; }
static bool fake_delete(uint32_t id) { (void)id; return g_fake.delete_ok; }
static bool fake_wipe(void) { return true; }
static bool fake_list(uint32_t *out_ids, size_t max_ids, size_t *out_count)
{
    if (out_count == NULL) {
        return false;
    }
    if (out_ids != NULL && max_ids > 0) {
        out_ids[0] = 7;
    }
    *out_count = 1;
    return true;
}
static bool fake_export(uint32_t id, uint8_t *b, size_t *l) { (void)id;(void)b;(void)l; return false; }
static bool fake_import(uint32_t id, const uint8_t *b, size_t l) { (void)id;(void)b;(void)l; return false; }
static int64_t fake_now(void) { return g_fake.now_ms; }

static inline use_case_context_t make_ctx(void) {
    use_case_context_t ctx = {
        .config_repo = {.load=fake_load,.save=fake_save,.factory_reset_config=fake_reset},
        .queue_repo = {.enqueue=fake_enqueue,.peek=fake_peek,.ack=fake_ack,.size=fake_size,.clear=fake_clear},
        .ws = {.is_connected=fake_ws_connected,.emit_event=fake_emit,.send_operation_result=fake_send_result},
        .sensor = {.enroll=fake_enroll,.identify=fake_identify,.delete_fingerprint=fake_delete,.wipe_all=fake_wipe,.list_fingerprints=fake_list,.export_template=fake_export,.import_template=fake_import},
        .clock = {.now_epoch_ms=fake_now},
        .device_id = "dev-1",
    };
    return ctx;
}
