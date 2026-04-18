#pragma once

#include "application/ports.h"
#include "application/use_cases.h"
#include "platform/runtime_mode.h"

typedef struct {
    use_case_context_t uc;
    network_port_t network;
    device_config_t config;
} app_controller_t;

app_controller_t app_controller_create_default(void);
runtime_mode_t app_controller_bootstrap(app_controller_t *controller);
void app_controller_start(app_controller_t *controller, runtime_mode_t mode);
