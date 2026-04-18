#include "application/app_controller.h"
#include "esp_log.h"
#include "platform/runtime_mode.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Booting fingerprint terminal firmware...");

    app_controller_t controller = app_controller_create_default();
    runtime_mode_t mode = app_controller_bootstrap(&controller);

    if (mode == RUNTIME_MODE_INITIAL_SETUP) {
        ESP_LOGI(TAG, "Starting setup web UI");
    } else if (mode == RUNTIME_MODE_DEMO) {
        ESP_LOGI(TAG, "Starting pre-init demo mode");
    } else {
        ESP_LOGI(TAG, "Starting configured runtime");
    }

    app_controller_start(&controller, mode);
}
