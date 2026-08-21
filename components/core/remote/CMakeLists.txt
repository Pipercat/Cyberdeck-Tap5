idf_component_register(
    SRCS "remote_auth.c" "remote_protocol.c" "remote_events.c" "remote_server.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_wifi esp_netif esp_event json settings network
             usb_host flash diagnostics esp_hw_support spi_flash esp_timer esp_app_format storage

)
