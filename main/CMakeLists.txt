idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES harness_io harness_scpi harness_gpio harness_bus harness_dut nvs_flash
)
