/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INPUT_TX_CAPACITY 64u

typedef struct {
    uint8_t id, len, data[8];
    uint32_t generation;
} input_tx_report_t;

/* Pure bounded queue. Its caller must serialize every access. */
typedef struct {
    input_tx_report_t reports[INPUT_TX_CAPACITY];
    size_t head, count;
    uint32_t generation;
    bool resetting;
} input_tx_queue_t;

void input_tx_init(input_tx_queue_t *queue);
void input_tx_reset(input_tx_queue_t *queue);
bool input_tx_push(input_tx_queue_t *queue, uint8_t id, const uint8_t *data, size_t len);
bool input_tx_pop(input_tx_queue_t *queue, input_tx_report_t *report);
bool input_tx_finish_reset(input_tx_queue_t *queue, uint32_t generation);
