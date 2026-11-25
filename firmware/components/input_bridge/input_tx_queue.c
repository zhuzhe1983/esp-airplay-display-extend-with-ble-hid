/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "input_tx_queue.h"
#include "input_core.h"
#include <string.h>

void input_tx_init(input_tx_queue_t *queue) {
    if (!queue) return;
    memset(queue, 0, sizeof *queue);
    queue->generation = 1;
}

void input_tx_reset(input_tx_queue_t *queue) {
    if (!queue) return;
    queue->head = queue->count = 0;
    queue->resetting = true;
    ++queue->generation;
}

bool input_tx_push(input_tx_queue_t *queue, uint8_t id, const uint8_t *data, size_t len) {
    if (!queue || !data || !len || len != input_report_size(id) || queue->resetting) return false;
    if (queue->count == INPUT_TX_CAPACITY) {
        /* Preserve no stale press/motion events: the consumer must release before resuming. */
        input_tx_reset(queue);
        return false;
    }
    size_t index = (queue->head + queue->count) % INPUT_TX_CAPACITY;
    input_tx_report_t *report = &queue->reports[index];
    report->id = id;
    report->len = (uint8_t)len;
    report->generation = queue->generation;
    memcpy(report->data, data, len);
    ++queue->count;
    return true;
}

bool input_tx_pop(input_tx_queue_t *queue, input_tx_report_t *report) {
    if (!queue || !report || queue->resetting || !queue->count) return false;
    *report = queue->reports[queue->head];
    queue->head = (queue->head + 1) % INPUT_TX_CAPACITY;
    --queue->count;
    return true;
}

bool input_tx_finish_reset(input_tx_queue_t *queue, uint32_t generation) {
    if (!queue || generation != queue->generation || !queue->resetting) return false;
    queue->resetting = false;
    return true;
}
