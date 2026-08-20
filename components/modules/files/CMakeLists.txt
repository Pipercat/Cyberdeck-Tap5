idf_component_register(
    SRCS "files_module.c" "files_module_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES storage network lvgl theme widgets nav
)
