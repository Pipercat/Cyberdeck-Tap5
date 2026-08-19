idf_component_register(
    SRCS "sensors_module.c" "sensors_module_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES hardware lvgl theme widgets nav esp_driver_i2c
)
