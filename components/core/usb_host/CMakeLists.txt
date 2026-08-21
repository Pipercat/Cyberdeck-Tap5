idf_component_register(
    SRCS "usb_host_manager.c" "usb_device_manager.c" "usb_serial.c"
    INCLUDE_DIRS "include"
    REQUIRES usb esp_common
)
