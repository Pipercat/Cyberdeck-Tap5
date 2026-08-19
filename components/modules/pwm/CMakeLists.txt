idf_component_register(
    SRCS "pwm_module.c" "pwm_module_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES hardware lvgl theme widgets nav
)
