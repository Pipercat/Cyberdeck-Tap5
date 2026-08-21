idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES hardware settings diagnostics nav dashboard gpio pwm adc i2c serial system audio settings_module network network_tools sensors widgets esp_lvgl_port flash remote usb_host files
)
