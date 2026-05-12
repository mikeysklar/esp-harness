idf_component_register(
    SRCS "harness_gpio.c"
    INCLUDE_DIRS "include"
    REQUIRES scpi_parser harness_dut esp_driver_gpio esp_hw_support log
)
