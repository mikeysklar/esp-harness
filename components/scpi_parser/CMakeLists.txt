idf_component_register(
    SRCS
        "upstream/libscpi/src/error.c"
        "upstream/libscpi/src/expression.c"
        "upstream/libscpi/src/fifo.c"
        "upstream/libscpi/src/ieee488.c"
        "upstream/libscpi/src/lexer.c"
        "upstream/libscpi/src/minimal.c"
        "upstream/libscpi/src/parser.c"
        "upstream/libscpi/src/units.c"
        "upstream/libscpi/src/utils.c"
    INCLUDE_DIRS "upstream/libscpi/inc" "user_config"
    PRIV_INCLUDE_DIRS "upstream/libscpi/src"
)

# scpi_user_config.h (in user_config/) overrides library defaults (LF line ending).
target_compile_definitions(${COMPONENT_LIB} PUBLIC SCPI_USER_CONFIG=1)
