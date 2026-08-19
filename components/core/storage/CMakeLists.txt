idf_component_register(
    SRCS "sd_module.c"
    INCLUDE_DIRS "include"
    REQUIRES sdmmc esp_driver_sdmmc fatfs vfs
)
