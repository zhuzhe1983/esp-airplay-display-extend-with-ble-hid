set(board_src "src/display_board.c")
# Requirements are collected before sdkconfig exists. Keep both boards' public
# component dependencies unconditional; only source selection depends on config.
set(board_deps esp_lcd esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_hw_support
    esp_lcd_touch esp_io_expander_tca95xx_16bit esp_lcd_touch_gt911 m5stack_tab5_noglib)

if(CONFIG_DISPLAY_BOARD_M5STACK_TAB5)
    list(APPEND board_src "src/board_tab5.c")
else()
    list(APPEND board_src "src/board_claw4.c")
    if(CONFIG_CLAW4_LCD_FL7707N)
        list(APPEND board_src "vendor/metalio_claw4/esp_lcd_fl7707n.c")
    else()
        list(APPEND board_src "vendor/metalio_claw4/esp_lcd_nv3051f.c")
    endif()
endif()

idf_component_register(SRCS ${board_src}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src" "vendor/metalio_claw4"
    REQUIRES esp_common
    PRIV_REQUIRES ${board_deps})
