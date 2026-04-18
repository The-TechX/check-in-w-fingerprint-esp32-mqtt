#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "domain/ports.h"

network_port_t network_manager_port(void);

bool network_manager_is_softap_mode(void);
bool network_manager_get_softap_ip(char *out, size_t out_len);
bool network_manager_get_sta_ip(char *out, size_t out_len);
