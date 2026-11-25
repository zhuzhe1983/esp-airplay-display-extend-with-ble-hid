#pragma once

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;
    uint8_t scanout_bytes_per_pixel;
} board_hw_t;

esp_err_t board_hw_init(board_hw_t *hw);
esp_err_t board_hw_backlight(uint8_t percent);
