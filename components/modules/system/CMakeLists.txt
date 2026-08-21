idf_component_register(
    SRCS "system_module.c" "system_module_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES hardware diagnostics network remote usb_host flash lvgl theme widgets nav
             esp_driver_tsens esp_hw_support esp_app_format spi_flash esp_timer
)
