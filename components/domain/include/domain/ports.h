#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "domain/domain_models.h"

typedef struct {
    bool (*load)(device_config_t *out_cfg);
    bool (*save)(const device_config_t *cfg);
    bool (*factory_reset_config)(void);
} config_repository_port_t;

typedef struct {
    bool (*enqueue)(const queue_item_t *item);
    bool (*peek)(queue_item_t *out_item);
    bool (*ack)(const char *event_id);
    size_t (*size)(void);
    bool (*clear)(void);
} queue_repository_port_t;

typedef struct {
    bool (*is_connected)(void);
    bool (*emit_event)(const queue_item_t *item);
    bool (*send_operation_result)(const operation_result_t *result, const char *correlation_id);
} websocket_transport_port_t;

typedef struct {
    bool (*connect)(const device_config_t *cfg);
    bool (*is_ready)(void);
} network_port_t;

typedef struct {
    void (*set_enroll_progress_callback)(void (*cb)(const char *step, void *user_ctx), void *user_ctx);
    bool (*enroll)(uint32_t *out_fingerprint_id);
    bool (*identify)(uint32_t *out_fingerprint_id);
    bool (*delete_fingerprint)(uint32_t fingerprint_id);
    bool (*wipe_all)(void);
    bool (*list_fingerprints)(uint32_t *out_ids, size_t max_ids, size_t *out_count);
    bool (*export_template)(uint32_t fingerprint_id, uint8_t *buffer, size_t *in_out_len);
    bool (*import_template)(uint32_t fingerprint_id, const uint8_t *buffer, size_t len);
} fingerprint_sensor_port_t;

typedef struct {
    int64_t (*now_epoch_ms)(void);
} clock_port_t;
