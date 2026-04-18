#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AS608_DEFAULT_BAUDRATE        57600
#define AS608_DEFAULT_ADDRESS         UINT32_C(0xFFFFFFFF)
#define AS608_DEFAULT_MAX_TEMPLATES   300U
#define AS608_INDEX_TABLE_PAGE_SIZE   32U
#define AS608_COMMAND_TIMEOUT_MS      1000U
#define AS608_POWER_ON_DELAY_MS       100U

typedef enum {
    AS608_OK = 0,
    AS608_ERR_TIMEOUT,
    AS608_ERR_COMM,
    AS608_ERR_CHECKSUM,
    AS608_ERR_NO_FINGER,
    AS608_ERR_IMAGE_CAPTURE,
    AS608_ERR_IMAGE_MESSY,
    AS608_ERR_FEATURE_FAIL,
    AS608_ERR_ENROLL_MISMATCH,
    AS608_ERR_NOT_FOUND,
    AS608_ERR_INVALID_SLOT,
    AS608_ERR_DB_FULL,
    AS608_ERR_DELETE_FAILED,
    AS608_ERR_UNSUPPORTED,
    AS608_ERR_INVALID_ARG,
    AS608_ERR_INTERNAL,
} as608_status_t;

typedef struct {
    uart_port_t uart_num;
    int tx_pin;
    int rx_pin;
    uint32_t baudrate;
    uint32_t address;
    uint16_t max_templates;
    uint16_t rx_buffer_size;
    uint16_t tx_buffer_size;
    uint32_t power_on_delay_ms;
} as608_config_t;

typedef struct {
    uint16_t slot;
    uint16_t score;
} as608_match_result_t;

typedef struct {
    uint8_t bits[AS608_INDEX_TABLE_PAGE_SIZE];
} as608_index_page_t;

typedef struct {
    bool initialized;
    uart_port_t uart_num;
    int tx_pin;
    int rx_pin;
    uint32_t baudrate;
    uint32_t address;
    uint16_t max_templates;
    SemaphoreHandle_t mutex;
    uint8_t *rx_tmp;
    size_t rx_tmp_size;
    bool uart_driver_owned;
} as608_t;

#ifdef __cplusplus
}
#endif
