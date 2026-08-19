idf_component_register(
    SRCS "log_sink.c" "self_test.c"
    INCLUDE_DIRS "include"
    REQUIRES log heap
)
