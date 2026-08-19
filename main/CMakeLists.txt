idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES hardware settings diagnostics nav dashboard gpio pwm adc i2c serial system audio settings_module network network_tools sensors storage files widgets esp_lvgl_port
)
