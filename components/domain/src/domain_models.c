#include <stddef.h>
#include "domain/domain_models.h"

bool domain_is_demo_mode_allowed(const device_config_t *cfg)
{
    return (cfg != NULL) && (!cfg->initialized) && (!cfg->demo_consumed);
}
