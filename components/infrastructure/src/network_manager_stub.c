#include "infrastructure/network_manager.h"

#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include <esp_log.h>
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "network_manager";
static bool s_ready = false;
static bool s_softap_mode = true;
static bool s_initialized = false;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

#define SOFTAP_SSID "FP-Terminal-Setup"
#define SOFTAP_PASSWORD "setup1234"

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_ready = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_ready = true;
    }
}

static bool wifi_stack_init_once(void)
{
    if (s_initialized) {
        return true;
    }

    if (esp_netif_init() != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed");
        return false;
    }
    if (esp_event_loop_create_default() != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed");
        return false;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_sta_netif == NULL || s_ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create default wifi netifs");
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed");
        return false;
    }

    if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Register WIFI_EVENT handler failed");
        return false;
    }
    if (esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Register IP_EVENT handler failed");
        return false;
    }

    s_initialized = true;
    return true;
}

static bool start_softap(void)
{
    wifi_config_t ap_cfg = {
        .ap = {
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .ssid_len = strlen(SOFTAP_SSID),
        },
    };

    strlcpy((char *)ap_cfg.ap.ssid, SOFTAP_SSID, sizeof(ap_cfg.ap.ssid));
    strlcpy((char *)ap_cfg.ap.password, SOFTAP_PASSWORD, sizeof(ap_cfg.ap.password));

    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(AP) failed");
        return false;
    }
    if (esp_wifi_set_config(WIFI_IF_AP, &ap_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed");
        return false;
    }
    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start(AP) failed");
        return false;
    }

    s_softap_mode = true;
    s_ready = true;
    ESP_LOGI(TAG, "SoftAP ready. SSID=%s password=%s", SOFTAP_SSID, SOFTAP_PASSWORD);
    return true;
}

static bool start_sta(const device_config_t *cfg)
{
    wifi_config_t sta_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };

    strlcpy((char *)sta_cfg.sta.ssid, cfg->wifi_ssid, sizeof(sta_cfg.sta.ssid));
    strlcpy((char *)sta_cfg.sta.password, cfg->wifi_password, sizeof(sta_cfg.sta.password));

    if (esp_wifi_stop() != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_stop returned error while switching to STA");
    }
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(STA) failed");
        return false;
    }
    if (esp_wifi_set_config(WIFI_IF_STA, &sta_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(STA) failed");
        return false;
    }
    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start(STA) failed");
        return false;
    }
    if (esp_wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed");
        return false;
    }

    s_softap_mode = false;
    s_ready = false;
    ESP_LOGI(TAG, "Connecting to STA SSID=%s", cfg->wifi_ssid);
    return true;
}

static bool connect_impl(const device_config_t *cfg)
{
    if (!wifi_stack_init_once()) {
        return false;
    }

    if (cfg == NULL) {
        return start_softap();
    }

    return start_sta(cfg);
}

static bool is_ready_impl(void)
{
    return s_ready;
}

bool network_manager_is_softap_mode(void)
{
    return s_softap_mode;
}

bool network_manager_get_softap_ip(char *out, size_t out_len)
{
    esp_netif_ip_info_t ip_info = {0};
    if (out == NULL || out_len < 8 || s_ap_netif == NULL) {
        return false;
    }
    if (esp_netif_get_ip_info(s_ap_netif, &ip_info) != ESP_OK) {
        return false;
    }
    snprintf(out, out_len, IPSTR, IP2STR(&ip_info.ip));
    return true;
}

bool network_manager_get_sta_ip(char *out, size_t out_len)
{
    esp_netif_ip_info_t ip_info = {0};
    if (out == NULL || out_len < 8 || s_sta_netif == NULL) {
        return false;
    }
    if (esp_netif_get_ip_info(s_sta_netif, &ip_info) != ESP_OK) {
        return false;
    }
    snprintf(out, out_len, IPSTR, IP2STR(&ip_info.ip));
    return true;
}

network_port_t network_manager_port(void)
{
    return (network_port_t){
        .connect = connect_impl,
        .is_ready = is_ready_impl,
    };
}
