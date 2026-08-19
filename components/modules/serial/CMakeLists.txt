idf_component_register(
    SRCS "serial_module.c" "serial_module_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES hardware lvgl theme widgets nav esp_driver_uart esp_timer
)
