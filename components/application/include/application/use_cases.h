#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "application/ports.h"

typedef struct {
    config_repository_port_t config_repo;
    queue_repository_port_t queue_repo;
    mqtt_port_t mqtt;
    fingerprint_sensor_port_t sensor;
    clock_port_t clock;
    const char *device_id;
} use_case_context_t;

bool use_case_register_fingerprint(use_case_context_t *ctx, const char *correlation_id, bool initiated_from_web_ui, operation_result_t *out_result);
bool use_case_check_in_once(use_case_context_t *ctx);
bool use_case_delete_fingerprint(use_case_context_t *ctx, uint32_t fingerprint_id, const char *correlation_id, operation_result_t *out_result);
bool use_case_wipe_all_fingerprints(use_case_context_t *ctx, const char *correlation_id, operation_result_t *out_result);
bool use_case_process_pending_queue(use_case_context_t *ctx, size_t max_items_to_flush);
