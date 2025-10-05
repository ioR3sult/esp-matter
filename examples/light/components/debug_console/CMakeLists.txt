idf_component_register(
    SRCS "console_gatt.c" "console_bridge.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES 
        esp_matter
        chip
        bt
)
