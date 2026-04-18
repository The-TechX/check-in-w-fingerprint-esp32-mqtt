#include <stdio.h>

void test_registration_offline_queues_event(void);
void test_checkin_unknown_fingerprint_no_event(void);
void test_delete_publishes_result_when_online(void);
void test_demo_mode_gating(void);
void test_queue_replay(void);

int main(void)
{
    test_registration_offline_queues_event();
    test_checkin_unknown_fingerprint_no_event();
    test_delete_publishes_result_when_online();
    test_demo_mode_gating();
    test_queue_replay();
    printf("All host tests passed.\n");
    return 0;
}
