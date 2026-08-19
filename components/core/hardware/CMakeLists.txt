idf_component_register(
    SRCS "pin_table.c" "hal_gpio.c" "board_init.c" "io_expander.c" "hal_pwm.c" "hal_adc.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd driver esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_adc
             esp_lcd_st7121 esp_lcd_st7123 esp_lcd_touch_st7123 esp_lcd_touch esp_lvgl_port lvgl
)
