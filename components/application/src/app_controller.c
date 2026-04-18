#include "application/app_controller.h"

#include <string.h>
#include "domain/domain_models.h"
#include "infrastructure/config_repository_nvs.h"
#include "infrastructure/mqtt_adapter.h"
#include "infrastructure/network_manager.h"
#include "infrastructure/offline_queue_nvs.h"
#include "drivers/as608/as608_driver.h"
#include "esp_log.h"

static const char *TAG = "app_controller";

app_controller_t app_controller_create_default(void)
{
    app_controller_t c = {0};

    c.uc.config_repo = config_repository_nvs_port();
    c.uc.queue_repo = offline_queue_nvs_port();
    c.uc.mqtt = mqtt_adapter_port();
    c.uc.sensor = as608_driver_port();
    c.uc.clock = mqtt_adapter_clock_port();
    c.network = network_manager_port();
    c.uc.device_id = c.config.device_id;

    return c;
}

runtime_mode_t app_controller_bootstrap(app_controller_t *controller)
{
    if (!controller->uc.config_repo.load(&controller->config)) {
        memset(&controller->config, 0, sizeof(controller->config));
    }

    if (!controller->config.initialized) {
        if (domain_is_demo_mode_allowed(&controller->config)) {
            return RUNTIME_MODE_DEMO;
        }
        return RUNTIME_MODE_INITIAL_SETUP;
    }

    return RUNTIME_MODE_CONFIGURED;
}

void app_controller_start(app_controller_t *controller, runtime_mode_t mode)
{
    (void)mode;

    if (controller->config.initialized) {
        controller->network.connect(&controller->config);
    }

    ESP_LOGI(TAG, "Web UI and MQTT tasks should be started here in follow-up implementation");
}
