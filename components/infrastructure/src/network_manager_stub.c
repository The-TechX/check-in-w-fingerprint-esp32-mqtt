#include "infrastructure/network_manager.h"

#include <esp_log.h>

static const char *TAG = "network_manager";
static bool s_ready = false;

static bool connect_impl(const device_config_t *cfg)
{
    ESP_LOGI(TAG, "Stub network connect to SSID=%s broker=%s:%u", cfg->wifi_ssid, cfg->mqtt_broker_host, cfg->mqtt_port);
    s_ready = true;
    return true;
}

static bool is_ready_impl(void)
{
    return s_ready;
}

network_port_t network_manager_port(void)
{
    return (network_port_t){
        .connect = connect_impl,
        .is_ready = is_ready_impl,
    };
}
