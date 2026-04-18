#include "drivers/as608/as608.h"
#include "esp_log.h"

static const char *TAG = "as608_example";

void app_main(void)
{
    as608_t sensor = {0};
    as608_config_t cfg;
    as608_match_result_t match;
    as608_status_t st;

    as608_default_config(&cfg);
    cfg.uart_num = UART_NUM_2;
    cfg.tx_pin = 17;
    cfg.rx_pin = 16;
    cfg.max_templates = 300;

    st = as608_init(&sensor, &cfg);
    if (st != AS608_OK) {
        ESP_LOGE(TAG, "init error: %s", as608_status_str(st));
        return;
    }

    ESP_LOGI(TAG, "Place finger for identify...");
    st = as608_identify(&sensor, 8000, &match);
    if (st == AS608_OK) {
        ESP_LOGI(TAG, "Matched slot=%u score=%u", match.slot, match.score);
    } else {
        ESP_LOGW(TAG, "identify failed: %s", as608_status_str(st));
    }

    as608_deinit(&sensor);
}
