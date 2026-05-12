idf_component_register(
    SRCS
        "harness_bus_common.c"
        "harness_bus_i2c.c"
        "harness_bus_spi.c"
        "harness_bus_uart.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES scpi_parser harness_dut
             esp_driver_i2c esp_driver_spi esp_driver_uart esp_driver_gpio
             freertos log esp_hw_support
)
