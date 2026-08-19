idf_component_register(
    SRCS "adc_module.c" "adc_module_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES hardware lvgl theme widgets nav
)
