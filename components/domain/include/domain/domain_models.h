#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DOMAIN_DEVICE_ID_MAX 64
#define DOMAIN_DISPLAY_NAME_MAX 64
#define DOMAIN_ENDPOINT_HOST_MAX 128
#define DOMAIN_ENDPOINT_PATH_MAX 96
#define DOMAIN_AUTH_TOKEN_MAX 256
#define DOMAIN_EVENT_ID_MAX 48

typedef enum {
    EVENT_TYPE_CHECKIN = 0,
    EVENT_TYPE_REGISTER_RESULT = 1,
    EVENT_TYPE_OPERATION_RESULT = 2,
} domain_event_type_t;

typedef struct {
    bool initialized;
    bool demo_consumed;
    bool tls_enabled;
    uint16_t websocket_port;
    char wifi_ssid[33];
    char wifi_password[65];
    char websocket_host[DOMAIN_ENDPOINT_HOST_MAX];
    char websocket_path[DOMAIN_ENDPOINT_PATH_MAX];
    char device_id[DOMAIN_DEVICE_ID_MAX];
    char display_name[DOMAIN_DISPLAY_NAME_MAX];
    char websocket_auth_token[DOMAIN_AUTH_TOKEN_MAX];
} device_config_t;

typedef struct {
    domain_event_type_t type;
    uint32_t fingerprint_id;
    int64_t timestamp_epoch_ms;
    uint32_t retry_count;
    char event_id[DOMAIN_EVENT_ID_MAX];
    char correlation_id[DOMAIN_EVENT_ID_MAX];
} queue_item_t;

typedef struct {
    bool success;
    uint32_t fingerprint_id;
    char code[32];
    char message[96];
} operation_result_t;

bool domain_is_demo_mode_allowed(const device_config_t *cfg);
