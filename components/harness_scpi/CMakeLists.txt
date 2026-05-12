idf_component_register(
    SRCS "harness_scpi.c" "harness_scpi_table.c"
    INCLUDE_DIRS "include"
    REQUIRES scpi_parser harness_io harness_gpio harness_bus harness_dut
             freertos esp_system esp_timer esp_hw_support log
)
