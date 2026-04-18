#include "application/app_controller.h"

#include <string.h>
#include "domain/domain_models.h"
#include "infrastructure/config_repository_nvs.h"
#include "infrastructure/mqtt_adapter.h"
#include "infrastructure/network_manager.h"
#include "infrastructure/offline_queue_nvs.h"
#include "drivers/as608/as608_driver.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_controller";

#define TOUCH_GPIO GPIO_NUM_4
#define TOUCH_ACTIVE_LOW 1
#define TOUCH_CHECKIN_TASK_STACK 4096
#define TOUCH_CHECKIN_TASK_PRIO 5
#define TOUCH_DEBOUNCE_MS 120
#define TOUCH_ATTEMPT_COOLDOWN_MS 900

static TaskHandle_t s_touch_checkin_task = NULL;
static bool s_touch_irq_ready = false;

static void IRAM_ATTR touch_isr_handler(void *arg)
{
    (void)arg;
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (s_touch_checkin_task != NULL) {
        vTaskNotifyGiveFromISR(s_touch_checkin_task, &higher_priority_task_woken);
    }

    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static bool touch_irq_init_once(void)
{
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << TOUCH_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
#if TOUCH_ACTIVE_LOW
        .intr_type = GPIO_INTR_NEGEDGE,
#else
        .intr_type = GPIO_INTR_POSEDGE,
#endif
    };

    esp_err_t err;

    if (s_touch_irq_ready) {
        return true;
    }

    err = gpio_config(&io_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch gpio_config failed: %s", esp_err_to_name(err));
        return false;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "touch gpio_install_isr_service failed: %s", esp_err_to_name(err));
        return false;
    }

    err = gpio_isr_handler_add(TOUCH_GPIO, touch_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch gpio_isr_handler_add failed: %s", esp_err_to_name(err));
        return false;
    }

    s_touch_irq_ready = true;
    ESP_LOGI(TAG, "TOUCH interrupt ready on GPIO=%d active_%s", (int)TOUCH_GPIO, TOUCH_ACTIVE_LOW ? "low" : "high");
    return true;
}

static void touch_checkin_task(void *arg)
{
    app_controller_t *controller = (app_controller_t *)arg;
    TickType_t last_event_tick = 0;
    TickType_t last_attempt_tick = 0;

    for (;;) {
        TickType_t now;
        bool matched;

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        now = xTaskGetTickCount();

        if ((now - last_event_tick) < pdMS_TO_TICKS(TOUCH_DEBOUNCE_MS)) {
            continue;
        }
        last_event_tick = now;

        if ((now - last_attempt_tick) < pdMS_TO_TICKS(TOUCH_ATTEMPT_COOLDOWN_MS)) {
            continue;
        }
        last_attempt_tick = now;

        ESP_LOGI(TAG, "TOUCH detected, running check-in attempt");
        matched = use_case_check_in_once(&controller->uc);

        if (matched) {
            ESP_LOGI(TAG, "TOUCH check-in matched and queued/published");
        } else {
            ESP_LOGI(TAG, "TOUCH check-in attempt finished without match");
        }

        use_case_process_pending_queue(&controller->uc, 8);
    }
}

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
        return RUNTIME_MODE_INITIAL_SETUP;
    }

    return RUNTIME_MODE_CONFIGURED;
}

void app_controller_start(app_controller_t *controller, runtime_mode_t mode)
{
    bool connected;

    if (mode == RUNTIME_MODE_INITIAL_SETUP) {
        connected = controller->network.connect(NULL);
    } else {
        connected = controller->network.connect(&controller->config);
    }

    if (!connected) {
        ESP_LOGE(TAG, "Network start failed for mode=%d", (int)mode);
        return;
    }

    ESP_LOGI(TAG, "Network started for mode=%d", (int)mode);

    if (s_touch_checkin_task == NULL) {
        if (xTaskCreate(touch_checkin_task,
                        "touch_checkin",
                        TOUCH_CHECKIN_TASK_STACK,
                        controller,
                        TOUCH_CHECKIN_TASK_PRIO,
                        &s_touch_checkin_task) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create TOUCH check-in task");
            s_touch_checkin_task = NULL;
        } else {
            ESP_LOGI(TAG, "TOUCH check-in task started");
        }
    }

    if (!touch_irq_init_once()) {
        ESP_LOGE(TAG, "TOUCH interrupt init failed; check pin wiring/config");
    }
}
