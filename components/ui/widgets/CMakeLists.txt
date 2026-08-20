idf_component_register(
    SRCS "statusbar.c" "module_card.c" "back_button.c" "screen_header.c" "bottom_nav.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl theme
)
