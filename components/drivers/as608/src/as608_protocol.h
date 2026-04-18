#pragma once

#include <stddef.h>
#include <stdint.h>
#include "drivers/as608/as608_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AS608_PACKET_COMMAND   0x01U
#define AS608_PACKET_DATA      0x02U
#define AS608_PACKET_ACK       0x07U
#define AS608_PACKET_END_DATA  0x08U

typedef struct {
    uint8_t packet_type;
    uint16_t payload_len;
    uint8_t *payload;
} as608_packet_t;

uint16_t as608_proto_checksum(uint8_t packet_type, uint16_t length, const uint8_t *payload);
as608_status_t as608_proto_send_packet(as608_t *ctx, uint8_t packet_type, const uint8_t *payload, uint16_t payload_len);
as608_status_t as608_proto_read_packet(as608_t *ctx, as608_packet_t *out_pkt, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
