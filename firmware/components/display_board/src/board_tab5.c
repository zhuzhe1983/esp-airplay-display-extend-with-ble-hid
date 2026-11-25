#include <inttypes.h>
#include "sdkconfig.h"
#include "board_private.h"
#include "bsp/m5stack_tab5.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_BSP_LCD_DPI_BUFFER_NUMS != 2
#error "display_board requires CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2"
#endif

static const char *TAG = "board_tab5";

static esp_err_t prepare_radio(void)
{
    // Official Tab5 schematic: P4 GPIO15 -> R4 -> C6 EN, with a 10k pull-up.
    // HIGH releases reset. ESP-Hosted 2.12.12's SDIO reset helper ends at its
    // H_RESET_VAL_ACTIVE; therefore this board must select ACTIVE_HIGH there.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_15, 1), TAG, "C6 reset latch");
    const gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << GPIO_NUM_15,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&reset_config), TAG, "C6 reset GPIO");
    esp_io_expander_handle_t io = bsp_io_expander_init();
    if (!io || !bsp_io_expander1_init()) {
        return ESP_ERR_NOT_FOUND;
    }
    // PI4IOE at 0x43 P0 is RF_PTH_L_INT_H_EXT. LOW selects the built-in
    // antenna; WLAN_PWR_EN is the separate 0x44 P0 controlled by the BSP.
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(io, IO_EXPANDER_PIN_NUM_0, IO_EXPANDER_OUTPUT), TAG, "Antenna direction");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(io, IO_EXPANDER_PIN_NUM_0, 0), TAG, "Internal antenna latch");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_output_mode(io, IO_EXPANDER_PIN_NUM_0,
                                                       IO_EXPANDER_OUTPUT_MODE_PUSH_PULL), TAG, "Antenna mode");
    return bsp_feature_enable(BSP_FEATURE_WIFI, true);
}

static esp_err_t prepare_display(uint32_t *lane_mbps)
{
    // A real low pulse is needed after a warm reset. The BSP's LCD enable
    // implementation releases 0x43 P4 as input-with-pullup, as required by
    // ST7123/ST7121. Driving that line high push-pull can disrupt the I2C bus.
    ESP_RETURN_ON_ERROR(bsp_feature_enable(BSP_FEATURE_TOUCH, false), TAG, "Touch reset assert");
    ESP_RETURN_ON_ERROR(bsp_feature_enable(BSP_FEATURE_LCD, false), TAG, "LCD reset assert");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(bsp_feature_enable(BSP_FEATURE_LCD, true), TAG, "LCD reset release");
    ESP_RETURN_ON_ERROR(bsp_feature_enable(BSP_FEATURE_TOUCH, true), TAG, "Touch reset release");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Official map: GT911=0x14; ST7123 and ST7121=0x55. Probe with a bounded
    // startup grace period before entering the BSP (which asserts if absent).
    // The full panel/touch identification and commands remain in BSP 1.3.3.
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    bool st712x = false;
    bool found = false;
    for (unsigned int attempt = 0; attempt < 20; ++attempt) {
        if (i2c_master_probe(bus, 0x55, 100) == ESP_OK) {
            st712x = true;
            found = true;
            break;
        }
        if (i2c_master_probe(bus, 0x14, 100) == ESP_OK) {
            found = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!found) {
        ESP_LOGE(TAG, "No supported touch controller found after LCD/touch reset");
        return ESP_ERR_NOT_FOUND;
    }

    *lane_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS;
    if (st712x) {
        // In BSP 1.3.3, selecting 965Mbps for ST7121 exists in the LVGL wrapper
        // only. Replicate that small selection for the documented noglib API.
        // Register 0x0000 uses a two-byte address; firmware 1=ST7121, 3=ST7123.
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = 0x55,
            .scl_speed_hz = 100000,
        };
        i2c_master_dev_handle_t device = NULL;
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &config, &device), TAG, "ST712x probe device");
        const uint8_t reg[] = {0, 0};
        uint8_t firmware = 0;
        esp_err_t err = i2c_master_transmit_receive(device, reg, sizeof(reg), &firmware, 1, 100);
        esp_err_t delete_err = i2c_master_bus_rm_device(device);
        ESP_RETURN_ON_ERROR(err, TAG, "ST712x firmware version");
        ESP_RETURN_ON_ERROR(delete_err, TAG, "ST712x probe cleanup");
        if (firmware == 1) {
            *lane_mbps = 965;
        } else if (firmware != 3) {
            ESP_LOGE(TAG, "Unsupported ST712x firmware version %u", firmware);
            return ESP_ERR_NOT_SUPPORTED;
        }
        ESP_LOGI(TAG, "ST712x firmware=%u, DSI lane rate=%" PRIu32 " Mbps", firmware, *lane_mbps);
    }
    return ESP_OK;
}

esp_err_t board_hw_backlight(uint8_t percent)
{
    return bsp_display_brightness_set(percent);
}

esp_err_t board_hw_init(board_hw_t *hw)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "I2C init");
    ESP_RETURN_ON_ERROR(prepare_radio(), TAG, "C6 power/antenna");
    uint32_t lane_mbps;
    ESP_RETURN_ON_ERROR(prepare_display(&lane_mbps), TAG, "LCD/touch reset and detection");
    const bsp_display_config_t config = {
        .dsi_bus = {.phy_clk_src = 0, .lane_bit_rate_mbps = lane_mbps},
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(bsp_display_new(&config, &hw->panel, &io), TAG, "BSP display");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(hw->panel, true), TAG, "Display on");
    ESP_RETURN_ON_ERROR(bsp_touch_new(NULL, &hw->touch), TAG, "BSP touch");
    hw->scanout_bytes_per_pixel = 2;
    return ESP_OK;
}
