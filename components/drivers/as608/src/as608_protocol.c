#include "as608_protocol.h"

#include <string.h>
#include "driver/uart.h"

#define AS608_START_CODE_HIGH  0xEFU
#define AS608_START_CODE_LOW   0x01U
#define AS608_PACKET_HEADER_SZ 9U

static as608_status_t as608_read_exact(as608_t *ctx, uint8_t *dst, size_t len, uint32_t timeout_ms)
{
    size_t total = 0;
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);

    while (total < len) {
        int r = uart_read_bytes(ctx->uart_num, &dst[total], len - total, ticks);
        if (r < 0) {
            return AS608_ERR_COMM;
        }
        if (r == 0) {
            return AS608_ERR_TIMEOUT;
        }
        total += (size_t)r;
    }

    return AS608_OK;
}

uint16_t as608_proto_checksum(uint8_t packet_type, uint16_t length, const uint8_t *payload)
{
    uint32_t sum = (uint32_t)packet_type + ((length >> 8U) & 0xFFU) + (length & 0xFFU);
    uint16_t payload_len = (uint16_t)(length - 2U);

    for (uint16_t i = 0; i < payload_len; ++i) {
        sum += payload[i];
    }

    return (uint16_t)(sum & 0xFFFFU);
}

as608_status_t as608_proto_send_packet(as608_t *ctx, uint8_t packet_type, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t frame[256];
    uint16_t length = (uint16_t)(payload_len + 2U);
    uint16_t checksum;
    size_t idx = 0;

    if ((ctx == NULL) || (!ctx->initialized)) {
        return AS608_ERR_INTERNAL;
    }
    if ((payload_len > 0U) && (payload == NULL)) {
        return AS608_ERR_INVALID_ARG;
    }
    if ((size_t)payload_len + 11U > sizeof(frame)) {
        return AS608_ERR_INVALID_ARG;
    }

    checksum = as608_proto_checksum(packet_type, length, payload);

    frame[idx++] = AS608_START_CODE_HIGH;
    frame[idx++] = AS608_START_CODE_LOW;
    frame[idx++] = (uint8_t)((ctx->address >> 24U) & 0xFFU);
    frame[idx++] = (uint8_t)((ctx->address >> 16U) & 0xFFU);
    frame[idx++] = (uint8_t)((ctx->address >> 8U) & 0xFFU);
    frame[idx++] = (uint8_t)(ctx->address & 0xFFU);
    frame[idx++] = packet_type;
    frame[idx++] = (uint8_t)((length >> 8U) & 0xFFU);
    frame[idx++] = (uint8_t)(length & 0xFFU);

    if (payload_len > 0U) {
        memcpy(&frame[idx], payload, payload_len);
        idx += payload_len;
    }

    frame[idx++] = (uint8_t)((checksum >> 8U) & 0xFFU);
    frame[idx++] = (uint8_t)(checksum & 0xFFU);

    if (uart_write_bytes(ctx->uart_num, (const char *)frame, idx) != (int)idx) {
        return AS608_ERR_COMM;
    }

    return AS608_OK;
}

as608_status_t as608_proto_read_packet(as608_t *ctx, as608_packet_t *out_pkt, uint32_t timeout_ms)
{
    uint8_t header[AS608_PACKET_HEADER_SZ];
    uint16_t length;
    uint16_t expected;
    uint16_t received;
    as608_status_t st;

    if ((ctx == NULL) || (out_pkt == NULL) || (out_pkt->payload == NULL)) {
        return AS608_ERR_INVALID_ARG;
    }

    st = as608_read_exact(ctx, header, sizeof(header), timeout_ms);
    if (st != AS608_OK) {
        return st;
    }

    if ((header[0] != AS608_START_CODE_HIGH) || (header[1] != AS608_START_CODE_LOW)) {
        return AS608_ERR_COMM;
    }

    length = (uint16_t)(((uint16_t)header[7] << 8U) | (uint16_t)header[8]);
    if ((length < 2U) || ((size_t)length > ctx->rx_tmp_size)) {
        return AS608_ERR_INTERNAL;
    }

    st = as608_read_exact(ctx, ctx->rx_tmp, length, timeout_ms);
    if (st != AS608_OK) {
        return st;
    }

    expected = as608_proto_checksum(header[6], length, ctx->rx_tmp);
    received = (uint16_t)(((uint16_t)ctx->rx_tmp[length - 2U] << 8U) | (uint16_t)ctx->rx_tmp[length - 1U]);
    if (expected != received) {
        return AS608_ERR_CHECKSUM;
    }

    out_pkt->packet_type = header[6];
    out_pkt->payload_len = (uint16_t)(length - 2U);
    memcpy(out_pkt->payload, ctx->rx_tmp, out_pkt->payload_len);

    return AS608_OK;
}
