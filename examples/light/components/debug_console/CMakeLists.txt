idf_component_register(
    SRCS "debug_console.c" "console_bridge.c"
    INCLUDE_DIRS "."
    REQUIRES bt console
)
