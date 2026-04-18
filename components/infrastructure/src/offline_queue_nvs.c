#include "infrastructure/offline_queue_nvs.h"

#include <stdbool.h>
#include <string.h>

#define OFFLINE_QUEUE_MAX_ITEMS 64

/*
 * NOTE: This queue storage is an in-memory placeholder.
 * Production v1 must persist this queue in NVS/LittleFS to survive reboot and power loss.
 */
static queue_item_t s_items[OFFLINE_QUEUE_MAX_ITEMS];
static size_t s_count = 0;

static bool enqueue_impl(const queue_item_t *item)
{
    if (item == NULL || s_count >= OFFLINE_QUEUE_MAX_ITEMS) {
        return false;
    }
    s_items[s_count++] = *item;
    return true;
}

static bool peek_impl(queue_item_t *out_item)
{
    if (s_count == 0 || out_item == NULL) {
        return false;
    }
    *out_item = s_items[0];
    return true;
}

static bool ack_impl(const char *event_id)
{
    (void)event_id;
    if (s_count == 0) {
        return false;
    }
    memmove(&s_items[0], &s_items[1], (s_count - 1) * sizeof(queue_item_t));
    s_count--;
    return true;
}

static size_t size_impl(void)
{
    return s_count;
}

static bool clear_impl(void)
{
    s_count = 0;
    return true;
}

queue_repository_port_t offline_queue_nvs_port(void)
{
    return (queue_repository_port_t){
        .enqueue = enqueue_impl,
        .peek = peek_impl,
        .ack = ack_impl,
        .size = size_impl,
        .clear = clear_impl,
    };
}
