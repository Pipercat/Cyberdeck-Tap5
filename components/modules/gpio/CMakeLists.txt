idf_component_register(
    SRCS "gpio_module.c" "gpio_module_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES hardware lvgl theme widgets nav driver
)
