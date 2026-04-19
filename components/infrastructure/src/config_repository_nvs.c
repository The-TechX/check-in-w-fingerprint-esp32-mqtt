#include "infrastructure/config_repository_nvs.h"

#include <stdbool.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config_repo";
static const char *NVS_NS = "fp_cfg";
static const char *NVS_KEY_CFG = "device_cfg";
static bool s_nvs_ready = false;

static bool ensure_nvs_ready(void)
{
    if (s_nvs_ready) return true;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init needs erase (%s), erasing", esp_err_to_name(err));
        if (nvs_flash_erase() != ESP_OK) return false;
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return false;
    }

    s_nvs_ready = true;
    return true;
}

static bool load_impl(device_config_t *out_cfg)
{
    nvs_handle_t nvs = 0;
    size_t len = sizeof(device_config_t);
    esp_err_t err;

    if (out_cfg == NULL || !ensure_nvs_ready()) {
        return false;
    }

    err = nvs_open(NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) return false;
    err = nvs_get_blob(nvs, NVS_KEY_CFG, out_cfg, &len);
    nvs_close(nvs);
    if (err != ESP_OK || len != sizeof(device_config_t)) return false;
    return out_cfg->initialized;
}

static bool save_impl(const device_config_t *cfg)
{
    nvs_handle_t nvs = 0;
    esp_err_t err;

    if (cfg == NULL || !ensure_nvs_ready()) {
        return false;
    }

    err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return false;
    err = nvs_set_blob(nvs, NVS_KEY_CFG, cfg, sizeof(device_config_t));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed saving config to NVS: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool factory_reset_config_impl(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t err;

    if (!ensure_nvs_ready()) return false;
    err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return false;
    err = nvs_erase_key(nvs, NVS_KEY_CFG);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed clearing config in NVS: %s", esp_err_to_name(err));
        return false;
    }
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
