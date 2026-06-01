idf_component_register(
    SRCS "harness_io_expander.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c log
)
