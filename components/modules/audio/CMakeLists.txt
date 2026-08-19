idf_component_register(
    SRCS "audio_module.c" "audio_module_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES hardware lvgl theme widgets nav esp_driver_i2s esp_timer esp_codec_dev
)
