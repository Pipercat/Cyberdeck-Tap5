idf_component_register(
    SRCS "i2c_module.c" "i2c_module_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES hardware lvgl theme widgets nav driver
)
