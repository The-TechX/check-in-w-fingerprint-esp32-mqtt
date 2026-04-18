#include "drivers/as608/as608.h"

#include <stdlib.h>
#include <string.h>
#include "as608_protocol.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "as608";

#define AS608_CMD_GET_IMAGE        0x01U
#define AS608_CMD_IMAGE_2_TZ       0x02U
#define AS608_CMD_MATCH            0x03U
#define AS608_CMD_SEARCH           0x04U
#define AS608_CMD_REG_MODEL        0x05U
#define AS608_CMD_STORE            0x06U
#define AS608_CMD_LOAD_CHAR        0x07U
#define AS608_CMD_UP_CHAR          0x08U
#define AS608_CMD_DOWN_CHAR        0x09U
#define AS608_CMD_DELETE           0x0CU
#define AS608_CMD_EMPTY            0x0DU
#define AS608_CMD_TEMPLATE_NUM     0x1DU
#define AS608_CMD_READ_INDEX_TABLE 0x1FU

#define AS608_ACK_OK            0x00U
#define AS608_ACK_PACKET_RECV   0x01U
#define AS608_ACK_NO_FINGER     0x02U
#define AS608_ACK_IMAGE_FAIL    0x03U
#define AS608_ACK_IMAGE_MESSY   0x06U
#define AS608_ACK_FEATURE_FAIL  0x07U
#define AS608_ACK_NO_MATCH      0x08U
#define AS608_ACK_NOT_FOUND     0x09U
#define AS608_ACK_ENROLL_MISM   0x0AU
#define AS608_ACK_BAD_LOCATION  0x0BU
#define AS608_ACK_DB_FULL       0x0CU
#define AS608_ACK_DELETE_FAIL   0x10U
#define AS608_ACK_CLEAR_FAIL    0x11U

static bool as608_slot_in_range(const as608_t *ctx, uint16_t slot)
{
    return slot < ctx->max_templates;
}

static as608_status_t as608_lock(as608_t *ctx)
{
    if ((ctx == NULL) || (ctx->mutex == NULL)) {
        return AS608_ERR_INTERNAL;
    }
    if (xSemaphoreTake(ctx->mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return AS608_ERR_TIMEOUT;
    }
    return AS608_OK;
}

static void as608_unlock(as608_t *ctx)
{
    if ((ctx != NULL) && (ctx->mutex != NULL)) {
        xSemaphoreGive(ctx->mutex);
    }
}

static as608_status_t as608_map_ack(uint8_t ack)
{
    switch (ack) {
    case AS608_ACK_OK:
        return AS608_OK;
    case AS608_ACK_PACKET_RECV:
        return AS608_ERR_COMM;
    case AS608_ACK_NO_FINGER:
        return AS608_ERR_NO_FINGER;
    case AS608_ACK_IMAGE_FAIL:
        return AS608_ERR_IMAGE_CAPTURE;
    case AS608_ACK_IMAGE_MESSY:
        return AS608_ERR_IMAGE_MESSY;
    case AS608_ACK_FEATURE_FAIL:
        return AS608_ERR_FEATURE_FAIL;
    case AS608_ACK_NO_MATCH:
    case AS608_ACK_NOT_FOUND:
        return AS608_ERR_NOT_FOUND;
    case AS608_ACK_ENROLL_MISM:
        return AS608_ERR_ENROLL_MISMATCH;
    case AS608_ACK_BAD_LOCATION:
        return AS608_ERR_INVALID_SLOT;
    case AS608_ACK_DB_FULL:
        return AS608_ERR_DB_FULL;
    case AS608_ACK_DELETE_FAIL:
    case AS608_ACK_CLEAR_FAIL:
        return AS608_ERR_DELETE_FAILED;
    default:
        return AS608_ERR_INTERNAL;
    }
}

static as608_status_t as608_exec_ack(as608_t *ctx, const uint8_t *cmd, uint16_t cmd_len,
                                     uint8_t *ack_payload, uint16_t *in_out_ack_payload_len,
                                     uint32_t timeout_ms)
{
    as608_packet_t pkt = {
        .packet_type = 0,
        .payload_len = 0,
        .payload = ack_payload,
    };
    as608_status_t st;

    if ((ctx == NULL) || (cmd == NULL) || (cmd_len == 0U) || (ack_payload == NULL) || (in_out_ack_payload_len == NULL)) {
        return AS608_ERR_INVALID_ARG;
    }

    pkt.payload = ack_payload;

    uart_flush_input(ctx->uart_num);
    st = as608_proto_send_packet(ctx, AS608_PACKET_COMMAND, cmd, cmd_len);
    if (st != AS608_OK) {
        return st;
    }

    if (*in_out_ack_payload_len == 0U) {
        return AS608_ERR_INVALID_ARG;
    }

    pkt.payload_len = 0;
    st = as608_proto_read_packet(ctx, &pkt, timeout_ms);
    if (st != AS608_OK) {
        return st;
    }

    if (pkt.packet_type != AS608_PACKET_ACK) {
        return AS608_ERR_COMM;
    }

    if (pkt.payload_len == 0U) {
        return AS608_ERR_INTERNAL;
    }

    if (pkt.payload_len > *in_out_ack_payload_len) {
        return AS608_ERR_INTERNAL;
    }

    *in_out_ack_payload_len = pkt.payload_len;
    return as608_map_ack(ack_payload[0]);
}

void as608_default_config(as608_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }

    *cfg = (as608_config_t){
        .uart_num = UART_NUM_2,
        .tx_pin = 17,
        .rx_pin = 16,
        .baudrate = AS608_DEFAULT_BAUDRATE,
        .address = AS608_DEFAULT_ADDRESS,
        .max_templates = AS608_DEFAULT_MAX_TEMPLATES,
        .rx_buffer_size = 512,
        .tx_buffer_size = 512,
        .power_on_delay_ms = AS608_POWER_ON_DELAY_MS,
    };
}

as608_status_t as608_init(as608_t *ctx, const as608_config_t *cfg)
{
    uart_config_t uart_cfg;
    as608_config_t local_cfg;

    if ((ctx == NULL) || (cfg == NULL)) {
        return AS608_ERR_INVALID_ARG;
    }
    if (cfg->max_templates == 0U) {
        return AS608_ERR_INVALID_ARG;
    }

    if (ctx->initialized) {
        return AS608_OK;
    }

    local_cfg = *cfg;
    if (local_cfg.baudrate == 0U) {
        local_cfg.baudrate = AS608_DEFAULT_BAUDRATE;
    }
    if (local_cfg.address == 0U) {
        local_cfg.address = AS608_DEFAULT_ADDRESS;
    }
    if (local_cfg.power_on_delay_ms == 0U) {
        local_cfg.power_on_delay_ms = AS608_POWER_ON_DELAY_MS;
    }

    uart_cfg = (uart_config_t){
        .baud_rate = (int)local_cfg.baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_driver_install(local_cfg.uart_num, local_cfg.rx_buffer_size, local_cfg.tx_buffer_size, 0, NULL, 0) != ESP_OK) {
        return AS608_ERR_COMM;
    }

    if (uart_param_config(local_cfg.uart_num, &uart_cfg) != ESP_OK) {
        uart_driver_delete(local_cfg.uart_num);
        return AS608_ERR_COMM;
    }

    if (uart_set_pin(local_cfg.uart_num, local_cfg.tx_pin, local_cfg.rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        uart_driver_delete(local_cfg.uart_num);
        return AS608_ERR_COMM;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->uart_num = local_cfg.uart_num;
    ctx->tx_pin = local_cfg.tx_pin;
    ctx->rx_pin = local_cfg.rx_pin;
    ctx->baudrate = local_cfg.baudrate;
    ctx->address = local_cfg.address;
    ctx->max_templates = local_cfg.max_templates;
    ctx->rx_tmp_size = 512U;
    ctx->rx_tmp = calloc(1U, ctx->rx_tmp_size);
    if (ctx->rx_tmp == NULL) {
        uart_driver_delete(local_cfg.uart_num);
        return AS608_ERR_INTERNAL;
    }

    ctx->mutex = xSemaphoreCreateMutex();
    if (ctx->mutex == NULL) {
        free(ctx->rx_tmp);
        ctx->rx_tmp = NULL;
        uart_driver_delete(local_cfg.uart_num);
        return AS608_ERR_INTERNAL;
    }

    ctx->initialized = true;
    ctx->uart_driver_owned = true;

    vTaskDelay(pdMS_TO_TICKS(local_cfg.power_on_delay_ms));
    return AS608_OK;
}

as608_status_t as608_deinit(as608_t *ctx)
{
    if (ctx == NULL) {
        return AS608_ERR_INVALID_ARG;
    }
    if (!ctx->initialized) {
        return AS608_OK;
    }

    if (ctx->mutex != NULL) {
        vSemaphoreDelete(ctx->mutex);
    }
    ctx->mutex = NULL;

    free(ctx->rx_tmp);
    ctx->rx_tmp = NULL;

    if (ctx->uart_driver_owned) {
        uart_driver_delete(ctx->uart_num);
    }

    memset(ctx, 0, sizeof(*ctx));
    return AS608_OK;
}

as608_status_t as608_capture_image(as608_t *ctx)
{
    uint8_t cmd[1] = {AS608_CMD_GET_IMAGE};
    uint8_t ack[8] = {0};
    uint16_t ack_len = sizeof(ack);

    as608_status_t st = as608_lock(ctx);
    if (st != AS608_OK) {
        return st;
    }
    st = as608_exec_ack(ctx, cmd, sizeof(cmd), ack, &ack_len, AS608_COMMAND_TIMEOUT_MS);
    as608_unlock(ctx);
    return st;
}

as608_status_t as608_image_to_char(as608_t *ctx, uint8_t buffer_id)
{
    uint8_t cmd[2] = {AS608_CMD_IMAGE_2_TZ, buffer_id};
    uint8_t ack[8] = {0};
    uint16_t ack_len = sizeof(ack);

    if ((buffer_id != 1U) && (buffer_id != 2U)) {
        return AS608_ERR_INVALID_ARG;
    }

    as608_status_t st = as608_lock(ctx);
    if (st != AS608_OK) {
        return st;
    }
    st = as608_exec_ack(ctx, cmd, sizeof(cmd), ack, &ack_len, AS608_COMMAND_TIMEOUT_MS);
    as608_unlock(ctx);
    return st;
}

as608_status_t as608_create_model(as608_t *ctx)
{
    uint8_t cmd[1] = {AS608_CMD_REG_MODEL};
    uint8_t ack[8] = {0};
    uint16_t ack_len = sizeof(ack);

    as608_status_t st = as608_lock(ctx);
    if (st != AS608_OK) {
        return st;
    }
    st = as608_exec_ack(ctx, cmd, sizeof(cmd), ack, &ack_len, AS608_COMMAND_TIMEOUT_MS);
    as608_unlock(ctx);
    return st;
}

as608_status_t as608_store_model(as608_t *ctx, uint8_t buffer_id, uint16_t slot)
{
    uint8_t cmd[4];
    uint8_t ack[8] = {0};
    uint16_t ack_len = sizeof(ack);

    if ((buffer_id != 1U) && (buffer_id != 2U)) {
        return AS608_ERR_INVALID_ARG;
    }
    if ((ctx == NULL) || !as608_slot_in_range(ctx, slot)) {
        return AS608_ERR_INVALID_SLOT;
    }

    cmd[0] = AS608_CMD_STORE;
    cmd[1] = buffer_id;
    cmd[2] = (uint8_t)((slot >> 8U) & 0xFFU);
    cmd[3] = (uint8_t)(slot & 0xFFU);

    as608_status_t st = as608_lock(ctx);
    if (st != AS608_OK) {
        return st;
    }
    st = as608_exec_ack(ctx, cmd, sizeof(cmd), ack, &ack_len, AS608_COMMAND_TIMEOUT_MS);
    as608_unlock(ctx);
    return st;
}

as608_status_t as608_search(as608_t *ctx, uint8_t buffer_id, uint16_t start_slot, uint16_t count,
                            as608_match_result_t *out_result)
{
    uint8_t cmd[6];
    uint8_t ack[16] = {0};
    uint16_t ack_len = sizeof(ack);
    as608_status_t st;

    if ((ctx == NULL) || (out_result == NULL) || (count == 0U)) {
        return AS608_ERR_INVALID_ARG;
    }
    if ((buffer_id != 1U) && (buffer_id != 2U)) {
        return AS608_ERR_INVALID_ARG;
    }
    if ((uint32_t)start_slot + (uint32_t)count > ctx->max_templates) {
        return AS608_ERR_INVALID_SLOT;
    }

    cmd[0] = AS608_CMD_SEARCH;
    cmd[1] = buffer_id;
    cmd[2] = (uint8_t)((start_slot >> 8U) & 0xFFU);
    cmd[3] = (uint8_t)(start_slot & 0xFFU);
    cmd[4] = (uint8_t)((count >> 8U) & 0xFFU);
    cmd[5] = (uint8_t)(count & 0xFFU);

    st = as608_lock(ctx);
    if (st != AS608_OK) {
        return st;
    }

    st = as608_exec_ack(ctx, cmd, sizeof(cmd), ack, &ack_len, AS608_COMMAND_TIMEOUT_MS);
    if ((st == AS608_OK) && (ack_len >= 5U)) {
        out_result->slot = (uint16_t)(((uint16_t)ack[1] << 8U) | ack[2]);
        out_result->score = (uint16_t)(((uint16_t)ack[3] << 8U) | ack[4]);
    } else if (st == AS608_OK) {
        st = AS608_ERR_INTERNAL;
    }

    as608_unlock(ctx);
    return st;
}

as608_status_t as608_match(as608_t *ctx, uint16_t *out_score)
{
    uint8_t cmd[1] = {AS608_CMD_MATCH};
    uint8_t ack[8] = {0};
    uint16_t ack_len = sizeof(ack);
    as608_status_t st;

    if (out_score == NULL) {
        return AS608_ERR_INVALID_ARG;
    }

    st = as608_lock(ctx);
    if (st != AS608_OK) {
        return st;
    }

    st = as608_exec_ack(ctx, cmd, sizeof(cmd), ack, &ack_len, AS608_COMMAND_TIMEOUT_MS);
    if ((st == AS608_OK) && (ack_len >= 3U)) {
        *out_score = (uint16_t)(((uint16_t)ack[1] << 8U) | ack[2]);
    } else if (st == AS608_OK) {
        st = AS608_ERR_INTERNAL;
    }

    as608_unlock(ctx);
    return st;
}

as608_status_t as608_delete_many(as608_t *ctx, uint16_t start_slot, uint16_t count)
{
    uint8_t cmd[5];
    uint8_t ack[8] = {0};
    uint16_t ack_len = sizeof(ack);

    if ((ctx == NULL) || (count == 0U)) {
        return AS608_ERR_INVALID_ARG;
    }
    if (((uint32_t)start_slot + (uint32_t)count) > ctx->max_templates) {
        return AS608_ERR_INVALID_SLOT;
    }

    cmd[0] = AS608_CMD_DELETE;
    cmd[1] = (uint8_t)((start_slot >> 8U) & 0xFFU);
    cmd[2] = (uint8_t)(start_slot & 0xFFU);
    cmd[3] = (uint8_t)((count >> 8U) & 0xFFU);
    cmd[4] = (uint8_t)(count & 0xFFU);

    as608_status_t st = as608_lock(ctx);
    if (st != AS608_OK) {
        return st;
    }
    st = as608_exec_ack(ctx, cmd, sizeof(cmd), ack, &ack_len, AS608_COMMAND_TIMEOUT_MS);
    as608_unlock(ctx);
    return st;
}

as608_status_t as608_delete(as608_t *ctx, uint16_t slot)
{
    return as608_delete_many(ctx, slot, 1U);
}

as608_status_t as608_empty_database(as608_t *ctx)
{
    uint8_t cmd[1] = {AS608_CMD_EMPTY};
    uint8_t ack[8] = {0};
    uint16_t ack_len = sizeof(ack);

    as608_status_t st = as608_lock(ctx);
    if (st != AS608_OK) {
        return st;
    }
    st = as608_exec_ack(ctx, cmd, sizeof(cmd), ack, &ack_len, 3000U);
    as608_unlock(ctx);
    return st;
}

as608_status_t as608_get_template_count(as608_t *ctx, uint16_t *out_count)
{
    uint8_t cmd[1] = {AS608_CMD_TEMPLATE_NUM};
    uint8_t ack[8] = {0};
    uint16_t ack_len = sizeof(ack);
    as608_status_t st;

    if (out_count == NULL) {
        return AS608_ERR_INVALID_ARG;
    }

    st = as608_lock(ctx);
    if (st != AS608_OK) {
        return st;
    }

    st = as608_exec_ack(ctx, cmd, sizeof(cmd), ack, &ack_len, AS608_COMMAND_TIMEOUT_MS);
    if ((st == AS608_OK) && (ack_len >= 3U)) {
        *out_count = (uint16_t)(((uint16_t)ack[1] << 8U) | ack[2]);
    } else if (st == AS608_OK) {
        st = AS608_ERR_INTERNAL;
    }

    as608_unlock(ctx);
    return st;
}

as608_status_t as608_read_index_table(as608_t *ctx, uint8_t page, as608_index_page_t *out_page)
{
    uint8_t cmd[2] = {AS608_CMD_READ_INDEX_TABLE, page};
    uint8_t ack[64] = {0};
    uint16_t ack_len = sizeof(ack);
    as608_status_t st;

    if ((ctx == NULL) || (out_page == NULL)) {
        return AS608_ERR_INVALID_ARG;
    }

    st = as608_lock(ctx);
    if (st != AS608_OK) {
        return st;
    }

    st = as608_exec_ack(ctx, cmd, sizeof(cmd), ack, &ack_len, AS608_COMMAND_TIMEOUT_MS);
    if ((st == AS608_OK) && (ack_len >= (AS608_INDEX_TABLE_PAGE_SIZE + 1U))) {
        memcpy(out_page->bits, &ack[1], AS608_INDEX_TABLE_PAGE_SIZE);
    } else if (st == AS608_OK) {
        st = AS608_ERR_INTERNAL;
    }

    as608_unlock(ctx);
    return st;
}

as608_status_t as608_find_free_slot(as608_t *ctx, uint16_t *out_slot)
{
    as608_index_page_t page;
    uint16_t page_count;

    if ((ctx == NULL) || (out_slot == NULL)) {
        return AS608_ERR_INVALID_ARG;
    }

    page_count = (uint16_t)((ctx->max_templates + 255U) / 256U);

    for (uint16_t p = 0; p < page_count; ++p) {
        as608_status_t st = as608_read_index_table(ctx, (uint8_t)p, &page);
        if (st != AS608_OK) {
            return st;
        }
        for (uint16_t byte_i = 0; byte_i < AS608_INDEX_TABLE_PAGE_SIZE; ++byte_i) {
            uint8_t b = page.bits[byte_i];
            if (b != 0xFFU) {
                for (uint8_t bit = 0; bit < 8U; ++bit) {
                    if ((b & (1U << bit)) == 0U) {
                        uint16_t slot = (uint16_t)(p * 256U + byte_i * 8U + bit);
                        if (slot < ctx->max_templates) {
                            *out_slot = slot;
                            return AS608_OK;
                        }
                        return AS608_ERR_DB_FULL;
                    }
                }
            }
        }
    }

    return AS608_ERR_DB_FULL;
}

as608_status_t as608_wait_finger_present(as608_t *ctx, uint32_t timeout_ms, uint32_t poll_interval_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline) {
        as608_status_t st = as608_capture_image(ctx);
        if (st == AS608_OK) {
            return AS608_OK;
        }
        if ((st != AS608_ERR_NO_FINGER) && (st != AS608_ERR_TIMEOUT)) {
            return st;
        }
        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
    }

    return AS608_ERR_TIMEOUT;
}

as608_status_t as608_wait_finger_removed(as608_t *ctx, uint32_t timeout_ms, uint32_t poll_interval_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline) {
        as608_status_t st = as608_capture_image(ctx);
        if (st == AS608_ERR_NO_FINGER) {
            return AS608_OK;
        }
        if ((st != AS608_OK) && (st != AS608_ERR_TIMEOUT)) {
            return st;
        }
        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
    }

    return AS608_ERR_TIMEOUT;
}

as608_status_t as608_enroll(as608_t *ctx, uint16_t preferred_slot, uint32_t timeout_ms, uint16_t *out_slot)
{
    as608_status_t st;
    uint16_t target_slot = preferred_slot;

    if (out_slot == NULL) {
        return AS608_ERR_INVALID_ARG;
    }

    if (preferred_slot == UINT16_MAX) {
        st = as608_find_free_slot(ctx, &target_slot);
        if (st != AS608_OK) {
            return st;
        }
    }

    ESP_LOGI(TAG, "Enroll: place finger");
    st = as608_wait_finger_present(ctx, timeout_ms, 200U);
    if (st != AS608_OK) {
        return st;
    }

    st = as608_image_to_char(ctx, 1U);
    if (st != AS608_OK) {
        return st;
    }

    ESP_LOGI(TAG, "Enroll: remove finger");
    st = as608_wait_finger_removed(ctx, timeout_ms, 200U);
    if (st != AS608_OK) {
        return st;
    }

    ESP_LOGI(TAG, "Enroll: place same finger again");
    st = as608_wait_finger_present(ctx, timeout_ms, 200U);
    if (st != AS608_OK) {
        return st;
    }

    st = as608_image_to_char(ctx, 2U);
    if (st != AS608_OK) {
        return st;
    }

    st = as608_create_model(ctx);
    if (st != AS608_OK) {
        return st;
    }

    st = as608_store_model(ctx, 1U, target_slot);
    if (st != AS608_OK) {
        return st;
    }

    *out_slot = target_slot;
    return AS608_OK;
}

as608_status_t as608_identify(as608_t *ctx, uint32_t timeout_ms, as608_match_result_t *out_result)
{
    as608_status_t st;

    if (out_result == NULL) {
        return AS608_ERR_INVALID_ARG;
    }

    st = as608_wait_finger_present(ctx, timeout_ms, 200U);
    if (st != AS608_OK) {
        return st;
    }

    st = as608_image_to_char(ctx, 1U);
    if (st != AS608_OK) {
        return st;
    }

    return as608_search(ctx, 1U, 0U, ctx->max_templates, out_result);
}

as608_status_t as608_verify_slot(as608_t *ctx, uint16_t slot, uint32_t timeout_ms, uint16_t *out_score)
{
    as608_match_result_t result;
    as608_status_t st;

    if (out_score == NULL) {
        return AS608_ERR_INVALID_ARG;
    }

    st = as608_wait_finger_present(ctx, timeout_ms, 200U);
    if (st != AS608_OK) {
        return st;
    }
    st = as608_image_to_char(ctx, 1U);
    if (st != AS608_OK) {
        return st;
    }

    st = as608_search(ctx, 1U, slot, 1U, &result);
    if (st != AS608_OK) {
        return st;
    }

    if (result.slot != slot) {
        return AS608_ERR_NOT_FOUND;
    }

    *out_score = result.score;
    return AS608_OK;
}

as608_status_t as608_export_template(as608_t *ctx, uint16_t slot, uint8_t *buffer, size_t *in_out_len)
{
    (void)ctx;
    (void)slot;
    (void)buffer;
    (void)in_out_len;
    /*
     * Requires complete multi-packet handling and exact payload semantics for UpChar (0x08),
     * which are not fully defined in project requirements. Kept explicit and honest.
     */
    return AS608_ERR_UNSUPPORTED;
}

as608_status_t as608_import_template(as608_t *ctx, uint16_t slot, const uint8_t *buffer, size_t len)
{
    (void)ctx;
    (void)slot;
    (void)buffer;
    (void)len;
    /*
     * Requires complete multi-packet handling and exact payload semantics for DownChar (0x09),
     * which are not fully defined in project requirements. Kept explicit and honest.
     */
    return AS608_ERR_UNSUPPORTED;
}

const char *as608_status_str(as608_status_t status)
{
    switch (status) {
    case AS608_OK:
        return "ok";
    case AS608_ERR_TIMEOUT:
        return "timeout";
    case AS608_ERR_COMM:
        return "communication error";
    case AS608_ERR_CHECKSUM:
        return "checksum error";
    case AS608_ERR_NO_FINGER:
        return "no finger";
    case AS608_ERR_IMAGE_CAPTURE:
        return "image capture failed";
    case AS608_ERR_IMAGE_MESSY:
        return "image too messy";
    case AS608_ERR_FEATURE_FAIL:
        return "feature extraction failed";
    case AS608_ERR_ENROLL_MISMATCH:
        return "enroll mismatch";
    case AS608_ERR_NOT_FOUND:
        return "not found";
    case AS608_ERR_INVALID_SLOT:
        return "invalid slot";
    case AS608_ERR_DB_FULL:
        return "database full";
    case AS608_ERR_DELETE_FAILED:
        return "delete failed";
    case AS608_ERR_UNSUPPORTED:
        return "unsupported";
    case AS608_ERR_INVALID_ARG:
        return "invalid argument";
    case AS608_ERR_INTERNAL:
    default:
        return "internal error";
    }
}
