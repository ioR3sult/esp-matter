idf_component_register(
    SRCS "console_gatt.c" "console_bridge.cpp" "console_state.c" "console_selftest.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES 
        esp_matter
        chip
        bt
)
