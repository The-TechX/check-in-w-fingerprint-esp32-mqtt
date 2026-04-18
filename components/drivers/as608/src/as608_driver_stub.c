#include "drivers/as608/as608_driver.h"

#include <string.h>
#include "driver/uart.h"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/gpio_num.h"

static const char *TAG = "as608_driver";

#define AS608_UART_NUM UART_NUM_2
#define AS608_UART_TX_PIN GPIO_NUM_17
#define AS608_UART_RX_PIN GPIO_NUM_16
#define AS608_BAUD_RATE 57600
#define AS608_UART_BUF_SIZE 512

#define AS608_START_CODE_H 0xEF
#define AS608_START_CODE_L 0x01
#define AS608_PACKET_COMMAND 0x01
#define AS608_PACKET_ACK 0x07

#define AS608_OK 0x00
#define AS608_NO_FINGER 0x02

#define AS608_CMD_GET_IMAGE 0x01
#define AS608_CMD_IMAGE_2_TZ 0x02
#define AS608_CMD_MATCH 0x03
#define AS608_CMD_SEARCH 0x04
#define AS608_CMD_REG_MODEL 0x05
#define AS608_CMD_STORE 0x06
#define AS608_CMD_DELETE 0x0C
#define AS608_CMD_EMPTY 0x0D

#define AS608_ACK_MAX_PAYLOAD 32
#define AS608_CAPTURE_TIMEOUT_MS 10000
#define AS608_IDENTIFY_WAIT_MS 900

static bool s_uart_ready = false;
static uint16_t s_next_slot = 1;
static SemaphoreHandle_t s_as608_mutex = NULL;

typedef struct {
    uint8_t code;
    uint8_t payload[AS608_ACK_MAX_PAYLOAD];
    size_t payload_len;
} as608_ack_t;

static bool as608_uart_init_once(void)
{
    if (s_uart_ready) {
        return true;
    }

    const uart_config_t uart_cfg = {
        .baud_rate = AS608_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_driver_install(AS608_UART_NUM, AS608_UART_BUF_SIZE, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed");
        return false;
    }
    if (uart_param_config(AS608_UART_NUM, &uart_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed");
        return false;
    }
    if (uart_set_pin(AS608_UART_NUM, AS608_UART_TX_PIN, AS608_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed");
        return false;
    }

    s_uart_ready = true;
    return true;
}

static bool as608_lock(void)
{
    if (s_as608_mutex == NULL) {
        s_as608_mutex = xSemaphoreCreateMutex();
        if (s_as608_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create AS608 mutex");
            return false;
        }
    }

    if (xSemaphoreTake(s_as608_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Timeout waiting AS608 mutex");
        return false;
    }
    return true;
}

static void as608_unlock(void)
{
    if (s_as608_mutex != NULL) {
        xSemaphoreGive(s_as608_mutex);
    }
}

static uint16_t as608_checksum(uint8_t packet_type, uint16_t length, const uint8_t *payload)
{
    uint32_t sum = packet_type + ((length >> 8) & 0xFF) + (length & 0xFF);
    size_t i;

    for (i = 0; i < (size_t)(length - 2); i++) {
        sum += payload[i];
    }

    return (uint16_t)sum;
}

static bool as608_send_command(const uint8_t *payload, uint16_t payload_len)
{
    uint8_t packet[64];
    uint16_t length;
    uint16_t checksum;
    int idx = 0;

    if (payload_len + 11 > sizeof(packet)) {
        return false;
    }

    length = payload_len + 2;
    checksum = as608_checksum(AS608_PACKET_COMMAND, length, payload);

    packet[idx++] = AS608_START_CODE_H;
    packet[idx++] = AS608_START_CODE_L;
    packet[idx++] = 0xFF;
    packet[idx++] = 0xFF;
    packet[idx++] = 0xFF;
    packet[idx++] = 0xFF;
    packet[idx++] = AS608_PACKET_COMMAND;
    packet[idx++] = (uint8_t)((length >> 8) & 0xFF);
    packet[idx++] = (uint8_t)(length & 0xFF);
    memcpy(&packet[idx], payload, payload_len);
    idx += payload_len;
    packet[idx++] = (uint8_t)((checksum >> 8) & 0xFF);
    packet[idx++] = (uint8_t)(checksum & 0xFF);

    return uart_write_bytes(AS608_UART_NUM, (const char *)packet, idx) == idx;
}

static bool as608_read_exact(uint8_t *dst, size_t len, TickType_t timeout_ticks)
{
    size_t total = 0;

    while (total < len) {
        int r = uart_read_bytes(AS608_UART_NUM, &dst[total], len - total, timeout_ticks);
        if (r <= 0) {
            return false;
        }
        total += (size_t)r;
    }

    return true;
}

static bool as608_read_ack(as608_ack_t *ack, uint32_t timeout_ms)
{
    uint8_t header[9];
    uint8_t raw_payload[AS608_ACK_MAX_PAYLOAD + 2];
    uint16_t length;
    uint16_t expected_checksum;
    uint16_t received_checksum;

    if (!as608_read_exact(header, sizeof(header), pdMS_TO_TICKS(timeout_ms))) {
        return false;
    }
    if (header[0] != AS608_START_CODE_H || header[1] != AS608_START_CODE_L) {
        return false;
    }
    if (header[6] != AS608_PACKET_ACK) {
        return false;
    }

    length = ((uint16_t)header[7] << 8) | header[8];
    if (length < 3 || length > (AS608_ACK_MAX_PAYLOAD + 2)) {
        return false;
    }

    if (!as608_read_exact(raw_payload, length, pdMS_TO_TICKS(timeout_ms))) {
        return false;
    }

    expected_checksum = as608_checksum(AS608_PACKET_ACK, length, raw_payload);
    received_checksum = ((uint16_t)raw_payload[length - 2] << 8) | raw_payload[length - 1];
    if (expected_checksum != received_checksum) {
        ESP_LOGW(TAG, "AS608 checksum mismatch exp=%04x got=%04x", expected_checksum, received_checksum);
        return false;
    }

    ack->code = raw_payload[0];
    ack->payload_len = (size_t)(length - 3);
    if (ack->payload_len > 0) {
        memcpy(ack->payload, &raw_payload[1], ack->payload_len);
    }
    return true;
}

static bool as608_exec(const uint8_t *payload, uint16_t payload_len, as608_ack_t *ack, uint32_t timeout_ms)
{
    if (!as608_send_command(payload, payload_len)) {
        return false;
    }
    return as608_read_ack(ack, timeout_ms);
}

static bool as608_wait_finger_image(uint32_t timeout_ms)
{
    as608_ack_t ack = {0};
    uint8_t cmd = AS608_CMD_GET_IMAGE;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline) {
        if (!as608_exec(&cmd, 1, &ack, 300)) {
            continue;
        }
        if (ack.code == AS608_OK) {
            return true;
        }
        if (ack.code != AS608_NO_FINGER) {
            ESP_LOGW(TAG, "GET_IMAGE returned code=%u", (unsigned)ack.code);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    return false;
}

static bool as608_wait_finger_removed(uint32_t timeout_ms)
{
    as608_ack_t ack = {0};
    uint8_t cmd = AS608_CMD_GET_IMAGE;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline) {
        if (!as608_exec(&cmd, 1, &ack, 300)) {
            continue;
        }
        if (ack.code == AS608_NO_FINGER) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    return false;
}

static bool as608_image_to_tz(uint8_t slot)
{
    as608_ack_t ack = {0};
    uint8_t cmd[2] = {AS608_CMD_IMAGE_2_TZ, slot};

    if (!as608_exec(cmd, sizeof(cmd), &ack, 1000)) {
        return false;
    }
    return ack.code == AS608_OK;
}

static bool as608_reg_model(void)
{
    as608_ack_t ack = {0};
    uint8_t cmd = AS608_CMD_REG_MODEL;

    if (!as608_exec(&cmd, 1, &ack, 1000)) {
        return false;
    }
    return ack.code == AS608_OK;
}

static bool as608_store_model(uint16_t slot)
{
    as608_ack_t ack = {0};
    uint8_t cmd[4] = {
        AS608_CMD_STORE,
        0x01,
        (uint8_t)((slot >> 8) & 0xFF),
        (uint8_t)(slot & 0xFF),
    };

    if (!as608_exec(cmd, sizeof(cmd), &ack, 1000)) {
        return false;
    }
    return ack.code == AS608_OK;
}

static bool enroll_impl(uint32_t *out_fingerprint_id)
{
    uint16_t slot;
    bool ok = false;

    if (out_fingerprint_id == NULL) {
        return false;
    }

    if (!as608_uart_init_once()) {
        return false;
    }
    if (!as608_lock()) {
        return false;
    }

    ESP_LOGI(TAG, "AS608 enroll step 1/2: place finger");
    if (!as608_wait_finger_image(AS608_CAPTURE_TIMEOUT_MS) || !as608_image_to_tz(1)) {
        ESP_LOGE(TAG, "AS608 enroll failed on first capture");
        goto done;
    }

    ESP_LOGI(TAG, "AS608 enroll: remove finger");
    as608_wait_finger_removed(AS608_CAPTURE_TIMEOUT_MS);

    ESP_LOGI(TAG, "AS608 enroll step 2/2: place same finger again");
    if (!as608_wait_finger_image(AS608_CAPTURE_TIMEOUT_MS) || !as608_image_to_tz(2)) {
        ESP_LOGE(TAG, "AS608 enroll failed on second capture");
        goto done;
    }

    if (!as608_reg_model()) {
        ESP_LOGE(TAG, "AS608 enroll failed while creating model");
        goto done;
    }

    slot = s_next_slot;
    if (!as608_store_model(slot)) {
        ESP_LOGE(TAG, "AS608 enroll failed storing model in slot=%u", (unsigned)slot);
        goto done;
    }

    s_next_slot++;
    *out_fingerprint_id = slot;
    ESP_LOGI(TAG, "AS608 enroll success slot=%u", (unsigned)slot);
    ok = true;

done:
    as608_unlock();
    return ok;
}

static bool identify_impl(uint32_t *out_fingerprint_id)
{
    as608_ack_t ack = {0};
    uint8_t search_cmd[6] = {AS608_CMD_SEARCH, 0x01, 0x00, 0x00, 0x00, 0xA3};
    bool ok = false;

    if (out_fingerprint_id == NULL) {
        return false;
    }

    if (!as608_uart_init_once()) {
        return false;
    }
    if (!as608_lock()) {
        return false;
    }

    if (!as608_wait_finger_image(AS608_IDENTIFY_WAIT_MS)) {
        goto done;
    }
    if (!as608_image_to_tz(1)) {
        goto done;
    }
    if (!as608_exec(search_cmd, sizeof(search_cmd), &ack, 1000)) {
        goto done;
    }
    if (ack.code != AS608_OK || ack.payload_len < 4) {
        goto done;
    }

    *out_fingerprint_id = ((uint16_t)ack.payload[0] << 8) | ack.payload[1];
    ok = true;

done:
    as608_unlock();
    return ok;
}

static bool delete_impl(uint32_t fingerprint_id)
{
    as608_ack_t ack = {0};
    uint8_t cmd[5];
    bool ok = false;

    if (!as608_uart_init_once()) {
        return false;
    }
    if (!as608_lock()) {
        return false;
    }

    cmd[0] = AS608_CMD_DELETE;
    cmd[1] = (uint8_t)((fingerprint_id >> 8) & 0xFF);
    cmd[2] = (uint8_t)(fingerprint_id & 0xFF);
    cmd[3] = 0x00;
    cmd[4] = 0x01;

    if (!as608_exec(cmd, sizeof(cmd), &ack, 1000)) {
        goto done;
    }
    ok = ack.code == AS608_OK;

done:
    as608_unlock();
    return ok;
}

static bool wipe_all_impl(void)
{
    as608_ack_t ack = {0};
    uint8_t cmd = AS608_CMD_EMPTY;
    bool ok = false;

    if (!as608_uart_init_once()) {
        return false;
    }
    if (!as608_lock()) {
        return false;
    }

    if (!as608_exec(&cmd, 1, &ack, 1500)) {
        goto done;
    }
    if (ack.code == AS608_OK) {
        s_next_slot = 1;
        ok = true;
    }

done:
    as608_unlock();
    return ok;
}

static bool export_template_impl(uint32_t fingerprint_id, uint8_t *buffer, size_t *in_out_len)
{
    (void)fingerprint_id;
    (void)buffer;
    (void)in_out_len;
    return false;
}

static bool import_template_impl(uint32_t fingerprint_id, const uint8_t *buffer, size_t len)
{
    (void)fingerprint_id;
    (void)buffer;
    (void)len;
    return false;
}

fingerprint_sensor_port_t as608_driver_port(void)
{
    return (fingerprint_sensor_port_t){
        .enroll = enroll_impl,
        .identify = identify_impl,
        .delete_fingerprint = delete_impl,
        .wipe_all = wipe_all_impl,
        .export_template = export_template_impl,
        .import_template = import_template_impl,
    };
}
