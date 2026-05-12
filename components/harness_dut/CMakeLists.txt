idf_component_register(
    SRCS "harness_dut.c"
    INCLUDE_DIRS "include"
    REQUIRES scpi_parser nvs_flash log
)
