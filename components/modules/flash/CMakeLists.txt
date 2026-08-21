idf_component_register(
    SRCS "flash_target.c" "flash_manager.c" "flash_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES usb_host esp_timer lvgl theme widgets nav
)
