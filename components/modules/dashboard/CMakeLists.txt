idf_component_register(
    SRCS "dashboard.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl theme widgets nav hardware gpio serial network
)
