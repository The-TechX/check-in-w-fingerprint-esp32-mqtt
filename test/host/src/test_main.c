#include <stdio.h>

void test_registration_offline_queues_event(void);
void test_checkin_unknown_fingerprint_no_event(void);
void test_delete_publishes_result_when_online(void);
void test_demo_mode_gating(void);
void test_queue_replay(void);
void test_list_registered_ids_from_sensor(void);

int main(void)
{
    test_registration_offline_queues_event();
    test_checkin_unknown_fingerprint_no_event();
    test_delete_publishes_result_when_online();
    test_demo_mode_gating();
    test_queue_replay();
    test_list_registered_ids_from_sensor();
    printf("All host tests passed.\n");
    return 0;
}
