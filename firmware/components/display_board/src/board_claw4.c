#include "sdkconfig.h"
#include "board_private.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if CONFIG_CLAW4_LCD_FL7707N
#include "esp_lcd_fl7707n.h"
#else
#include "esp_lcd_nv3051f.h"
#endif

static const char *TAG = "board_claw4";
static i2c_master_bus_handle_t i2c_bus;
static esp_io_expander_handle_t expander;
static esp_ldo_channel_handle_t dsi_ldo;
static esp_lcd_dsi_bus_handle_t dsi_bus;
static esp_lcd_panel_io_handle_t display_io;
static esp_lcd_panel_io_handle_t touch_io;

static esp_err_t init_power_and_io(void)
{
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = GPIO_NUM_7,
        .scl_io_num = GPIO_NUM_8,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&config, &i2c_bus), TAG, "I2C init");
    ESP_RETURN_ON_ERROR(esp_io_expander_new_i2c_tca95xx_16bit(
        i2c_bus, ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000, &expander), TAG, "TCA9555 init");

    // Match the hardware mapping in upstream IOExpander.hpp. Program output
    // latches before direction, so active-low camera/SD power does not glitch on.
    const uint32_t outputs = (1U << 0) | (1U << 1) | (1U << 2) | (1U << 3) |
                             (1U << 4) | (1U << 6) | (1U << 7) | (1U << 8) | (1U << 12);
    const uint32_t inputs = (1U << 5) | (1U << 9) | (1U << 10) | (1U << 11);
    const uint32_t high = (1U << 1) | (1U << 2) | (1U << 3) | (1U << 7);
    // The generic set_level API rejects pins still configured as inputs. Use
    // the driver's public register operation here, which also updates its
    // register cache, before enabling output direction. All unspecified/input
    // output latches retain their reset value (1).
    ESP_RETURN_ON_ERROR(expander->write_output_reg(expander, (0xffffU & ~outputs) | high),
                        TAG, "Initial power output register");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(expander, inputs, IO_EXPANDER_INPUT), TAG, "Input directions");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(expander, outputs, IO_EXPANDER_OUTPUT), TAG, "Output directions");
    // TCA9555 P6 supplies a separate BT-audio chip; keep it off. The C5 radio
    // is on the board supply and uses GPIO54 reset, not this expander output.
    const gpio_config_t reset_gpio = {
        .pin_bit_mask = 1ULL << GPIO_NUM_54,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_54, 1), TAG, "C5 reset latch");
    ESP_RETURN_ON_ERROR(gpio_config(&reset_gpio), TAG, "C5 reset GPIO");
    // ESP-Hosted performs the reset pulse and its configured 1500ms boot delay.

    const esp_ldo_channel_config_t ldo_config = {.chan_id = 3, .voltage_mv = 2500};
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_config, &dsi_ldo), TAG, "DSI PHY LDO");
    return ESP_OK;
}

static esp_err_t init_backlight(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel = {
        .gpio_num = GPIO_NUM_52,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "Backlight timer");
    return ledc_channel_config(&channel);
}

esp_err_t board_hw_backlight(uint8_t percent)
{
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                    (1023U * percent) / 100U), TAG, "Backlight duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static esp_err_t init_display(board_hw_t *hw)
{
    const esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 1000,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &dsi_bus), TAG, "DSI bus");
    const esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, &display_io), TAG, "DBI IO");
    // Preserve upstream panel timings and RGB888 scanout. Two owned buffers are
    // sufficient because our presenter waits before recycling the retired one.
    const esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 48,
        .in_color_format = LCD_COLOR_FMT_RGB888,
        .out_color_format = LCD_COLOR_FMT_RGB888,
        .num_fbs = 2,
        .video_timing = {
            .h_size = 720,
            .v_size = 720,
#if CONFIG_CLAW4_LCD_FL7707N
            .hsync_back_porch = 120, .hsync_pulse_width = 60, .hsync_front_porch = 106,
            .vsync_back_porch = 20, .vsync_pulse_width = 4, .vsync_front_porch = 20,
#else
            .hsync_back_porch = 44, .hsync_pulse_width = 2, .hsync_front_porch = 46,
            .vsync_back_porch = 14, .vsync_pulse_width = 2, .vsync_front_porch = 16,
#endif
        },
    };
#if CONFIG_CLAW4_LCD_FL7707N
    const fl7707n_vendor_config_t vendor = {
        .mipi_config = {.dsi_bus = dsi_bus, .dpi_config = &dpi_config},
    };
#else
    const nv3051f_vendor_config_t vendor = {
        .mipi_config = {.dsi_bus = dsi_bus, .dpi_config = &dpi_config},
    };
#endif
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_3,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#if CONFIG_CLAW4_LCD_FL7707N
        // Upstream FL7707N's panel-side setting is 16 while DSI is RGB888.
        // Retain the vendor-tested combination; do not infer another COLMOD.
        .bits_per_pixel = 16,
#else
        .bits_per_pixel = 24,
#endif
        .vendor_config = (void *)&vendor,
    };
#if CONFIG_CLAW4_LCD_FL7707N
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_fl7707n(display_io, &panel_config, &hw->panel), TAG, "FL7707N");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_nv3051f(display_io, &panel_config, &hw->panel), TAG, "NV3051F");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(hw->panel), TAG, "LCD reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(hw->panel), TAG, "LCD init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(hw->panel, true), TAG, "LCD display on");
    hw->scanout_bytes_per_pixel = 3;
    return ESP_OK;
}

static esp_err_t init_touch(board_hw_t *hw)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    uint8_t address = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    if (i2c_master_probe(i2c_bus, address, 100) != ESP_OK) {
        address = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
        ESP_RETURN_ON_ERROR(i2c_master_probe(i2c_bus, address, 100), TAG, "GT911 missing at both addresses");
    }
    const esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = address,
        .scl_speed_hz = 200000,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .flags.disable_control_phase = 1,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &touch_io), TAG, "Touch IO");
    const esp_lcd_touch_config_t config = {
        .x_max = 720,
        .y_max = 720,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_33,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = false, .mirror_x = false, .mirror_y = false},
    };
    ESP_LOGI(TAG, "GT911 detected at 0x%02x", address);
    return esp_lcd_touch_new_i2c_gt911(touch_io, &config, &hw->touch);
}

esp_err_t board_hw_init(board_hw_t *hw)
{
    ESP_RETURN_ON_ERROR(init_power_and_io(), TAG, "Power/IO");
    ESP_RETURN_ON_ERROR(init_backlight(), TAG, "Backlight");
    ESP_RETURN_ON_ERROR(init_display(hw), TAG, "Display");
    return init_touch(hw);
}
