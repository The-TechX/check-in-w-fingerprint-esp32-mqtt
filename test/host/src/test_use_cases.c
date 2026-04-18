#include <assert.h>
#include <string.h>
#include "test_fakes.h"
#include "domain/domain_models.h"

void test_registration_offline_queues_event(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.enroll_ok = true;
    g_fake.now_ms = 1000;

    use_case_context_t ctx = make_ctx();
    operation_result_t result = {0};
    bool ok = use_case_register_fingerprint(&ctx, "corr-1", true, &result);

    assert(ok);
    assert(result.success);
    assert(g_fake.queue_size == 1);
    assert(g_fake.queue[0].type == EVENT_TYPE_REGISTER_RESULT);
}

void test_checkin_unknown_fingerprint_no_event(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.identify_ok = false;
    use_case_context_t ctx = make_ctx();

    bool ok = use_case_check_in_once(&ctx);

    assert(!ok);
    assert(g_fake.queue_size == 0);
}

void test_delete_publishes_result_when_online(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.delete_ok = true;
    g_fake.mqtt_connected = true;

    use_case_context_t ctx = make_ctx();
    operation_result_t result;
    bool ok = use_case_delete_fingerprint(&ctx, 12, "corr-del", &result);

    assert(ok);
    assert(result.success);
}

void test_demo_mode_gating(void)
{
    device_config_t cfg = {0};
    cfg.initialized = false;
    cfg.demo_consumed = false;
    assert(domain_is_demo_mode_allowed(&cfg));
    cfg.demo_consumed = true;
    assert(!domain_is_demo_mode_allowed(&cfg));
}

void test_queue_replay(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.mqtt_connected = true;
    g_fake.mqtt_publish_ok = true;
    g_fake.queue_size = 1;
    g_fake.queue[0].type = EVENT_TYPE_CHECKIN;
    strcpy(g_fake.queue[0].event_id, "evt-1");

    use_case_context_t ctx = make_ctx();
    bool ok = use_case_process_pending_queue(&ctx, 4);

    assert(ok);
    assert(g_fake.queue_size == 0);
}

void test_list_registered_ids_from_sensor(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    use_case_context_t ctx = make_ctx();
    uint32_t ids[4] = {0};
    size_t count = 0;

    bool ok = use_case_list_registered_fingerprints(&ctx, ids, 4, &count);
    assert(ok);
    assert(count == 1);
    assert(ids[0] == 7);
}
