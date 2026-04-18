#include "application/app_controller.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "platform/runtime_mode.h"
#include "webui/webui_server.h"

static const char *TAG = "main";
static app_controller_t s_controller;

void app_main(void)
{
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    ESP_LOGI(TAG, "Booting fingerprint terminal firmware...");

    s_controller = app_controller_create_default();
    runtime_mode_t mode = app_controller_bootstrap(&s_controller);

    if (mode == RUNTIME_MODE_INITIAL_SETUP) {
        ESP_LOGI(TAG, "Starting setup web UI");
    } else if (mode == RUNTIME_MODE_DEMO) {
        ESP_LOGI(TAG, "Starting pre-init demo mode");
    } else {
        ESP_LOGI(TAG, "Starting configured runtime");
    }

    app_controller_start(&s_controller, mode);
    webui_server_start(&s_controller);
}
