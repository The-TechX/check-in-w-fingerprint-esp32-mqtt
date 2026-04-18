#pragma once

#include <stdbool.h>
#include "application/use_cases.h"
#include "domain/ports.h"

mqtt_port_t mqtt_adapter_port(void);
clock_port_t mqtt_adapter_clock_port(void);

bool mqtt_adapter_start(const device_config_t *cfg, use_case_context_t *ctx);
void mqtt_adapter_stop(void);
