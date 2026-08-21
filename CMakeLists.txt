cmake_minimum_required(VERSION 3.16)

# components/ ist zweistufig gegliedert (core/modules/ui), ESP-IDF sucht per
# Default nur eine Ebene tief - deshalb explizite Unterordnerliste.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/core/hardware"
    "${CMAKE_CURRENT_LIST_DIR}/components/core/network"
    "${CMAKE_CURRENT_LIST_DIR}/components/core/settings"
    "${CMAKE_CURRENT_LIST_DIR}/components/core/diagnostics"
    "${CMAKE_CURRENT_LIST_DIR}/components/core/usb_host"
    "${CMAKE_CURRENT_LIST_DIR}/components/core/remote"
    "${CMAKE_CURRENT_LIST_DIR}/components/ui/theme"
    "${CMAKE_CURRENT_LIST_DIR}/components/ui/widgets"
    "${CMAKE_CURRENT_LIST_DIR}/components/ui/nav"
    "${CMAKE_CURRENT_LIST_DIR}/components/modules/dashboard"
    "${CMAKE_CURRENT_LIST_DIR}/components/modules/flash"
    "${CMAKE_CURRENT_LIST_DIR}/components/modules/gpio"
    "${CMAKE_CURRENT_LIST_DIR}/components/modules/pwm"
    "${CMAKE_CURRENT_LIST_DIR}/components/modules/adc"
    "${CMAKE_CURRENT_LIST_DIR}/components/modules/serial"
    "${CMAKE_CURRENT_LIST_DIR}/components/modules/i2c"
    "${CMAKE_CURRENT_LIST_DIR}/components/modules/audio"
    "${CMAKE_CURRENT_LIST_DIR}/components/modules/sensors"
    "${CMAKE_CURRENT_LIST_DIR}/components/modules/network_tools"
    "${CMAKE_CURRENT_LIST_DIR}/components/modules/system"
    "${CMAKE_CURRENT_LIST_DIR}/components/modules/settings_module"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cyberdeck_tab5)
