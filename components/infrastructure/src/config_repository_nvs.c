#include "infrastructure/config_repository_nvs.h"

#include <string.h>

/*
 * NOTE: This is a persistence stub for scaffolding.
 * Production implementation should store encrypted/sensitive fields in NVS with proper keying and versioned schema migration.
 */
static device_config_t s_config;
static bool s_has_config = false;

static bool load_impl(device_config_t *out_cfg)
{
    if (!s_has_config || out_cfg == NULL) {
        return false;
    }
    *out_cfg = s_config;
    return true;
}

static bool save_impl(const device_config_t *cfg)
{
    if (cfg == NULL) {
        return false;
    }
    s_config = *cfg;
    s_has_config = true;
    return true;
}

static bool factory_reset_config_impl(void)
{
    memset(&s_config, 0, sizeof(s_config));
    s_has_config = false;
    return true;
}

config_repository_port_t config_repository_nvs_port(void)
{
    return (config_repository_port_t){
        .load = load_impl,
        .save = save_impl,
        .factory_reset_config = factory_reset_config_impl,
    };
}
