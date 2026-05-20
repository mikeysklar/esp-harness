idf_component_register(
    SRCS "harness_dut.c" "board_pins.c"
    INCLUDE_DIRS "include"
    REQUIRES scpi_parser nvs_flash log
)
