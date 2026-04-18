#pragma once

#include "application/use_cases.h"

websocket_transport_port_t websocket_transport_port(void);
clock_port_t websocket_transport_clock_port(void);

bool websocket_transport_start(const device_config_t *cfg, use_case_context_t *ctx);
void websocket_transport_stop(void);
