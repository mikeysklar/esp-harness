/**
 * @file board_pins.c
 * @brief Board-specific pin tables, selected at compile time via Kconfig.
 *
 * Each table maps CircuitPython pin names (the labels users see when they
 * ``import board``) to the GPIO numbers those names refer to on that board.
 *
 * Data is taken from each board's ``pins.c`` in the CircuitPython source.
 *
 * To add a new board:
 *   1. Add a CONFIG_HARNESS_BOARD_<NAME> bool and corresponding
 *      CONFIG_HARNESS_BOARD_NAME mapping in Kconfig.projbuild.
 *   2. Add an #elif block below with the pin labels and GPIO numbers.
 */

#include "board_pins.h"

/* ------------------------------------------------------------------
 * espressif_esp32p4_function_ev
 * circuitpython/ports/espressif/boards/espressif_esp32p4_function_ev/pins.c
 * ------------------------------------------------------------------ */
#if defined(CONFIG_HARNESS_BOARD_ESPRESSIF_ESP32P4_FUNCTION_EV)

static const board_pin_t k_pins[] = {
    /* Header Block J1 */
    {"I2C_SDA",    7},
    {"IO7",        7},

    {"I2C_SCL",    8},
    {"IO8",        8},

    {"IO23",      23},

    {"TX",        37},
    {"IO37",      37},

    {"RX",        38},
    {"IO38",      38},

    {"IO21",      21},
    {"IO22",      22},
    {"IO20",      20},

    {"C6_WAKEUP",  6},
    {"IO6",        6},

    {"IO5",        5},
    {"IO4",        4},
    {"IO3",        3},
    {"IO2",        2},
    {"IO36",      36},

    {"IO32",      32},
    {"IO24",      24},
    {"IO25",      25},

    {"IO33",      33},
    {"IO26",      26},

    {"C6_EN",     54},
    {"IO54",      54},

    {"IO48",      48},

    {"PA_CTRL",   53},
    {"IO53",      53},

    {"IO46",      46},
    {"IO47",      47},
    {"IO27",      27},

    /* I2S */
    {"I2S_DSDIN",  9},
    {"I2S_LRCK",  10},
    {"I2S_ASDOUT", 11},
    {"I2S_SCLK",  12},
    {"I2S_MCLK",  13},

    /* Ethernet */
    {"RMII_RXDV", 28},
    {"RMII_RXD0", 29},
    {"RMII_RXD1", 30},
    {"MDC",       31},
    {"RMII_TXD0", 34},
    {"RMII_TXD1", 35},
    {"RMII_TXEN", 49},
    {"RMII_CLK",  50},
    {"PHY_RSTN",  51},
    {"MDIO",      52},

    /* SD Card */
    {"SD_DATA0",  39},
    {"SD_DATA1",  40},
    {"SD_DATA2",  41},
    {"SD_DATA3",  42},
    {"SD_CLK",    43},
    {"SD_CMD",    44},
    {"SD_PWRN",   45},
};

/* ------------------------------------------------------------------
 * espressif_esp32p4x_function_ev
 * circuitpython-left-field/ports/espressif/boards/espressif_esp32p4x_function_ev/pins.c
 * (same pinout as P4 EV)
 * ------------------------------------------------------------------ */
#elif defined(CONFIG_HARNESS_BOARD_ESPRESSIF_ESP32P4X_FUNCTION_EV)

static const board_pin_t k_pins[] = {
    /* Header Block J1 */
    {"I2C_SDA",    7},
    {"IO7",        7},

    {"I2C_SCL",    8},
    {"IO8",        8},

    {"IO23",      23},

    {"TX",        37},
    {"IO37",      37},

    {"RX",        38},
    {"IO38",      38},

    {"IO21",      21},
    {"IO22",      22},
    {"IO20",      20},

    {"C6_WAKEUP",  6},
    {"IO6",        6},

    {"IO5",        5},
    {"IO4",        4},
    {"IO3",        3},
    {"IO2",        2},
    {"IO36",      36},

    {"IO32",      32},
    {"IO24",      24},
    {"IO25",      25},

    {"IO33",      33},
    {"IO26",      26},

    {"C6_EN",     54},
    {"IO54",      54},

    {"IO48",      48},

    {"PA_CTRL",   53},
    {"IO53",      53},

    {"IO46",      46},
    {"IO47",      47},
    {"IO27",      27},

    /* I2S */
    {"I2S_DSDIN",  9},
    {"I2S_LRCK",  10},
    {"I2S_ASDOUT", 11},
    {"I2S_SCLK",  12},
    {"I2S_MCLK",  13},

    /* Ethernet */
    {"RMII_RXDV", 28},
    {"RMII_RXD0", 29},
    {"RMII_RXD1", 30},
    {"MDC",       31},
    {"RMII_TXD0", 34},
    {"RMII_TXD1", 35},
    {"RMII_TXEN", 49},
    {"RMII_CLK",  50},
    {"PHY_RSTN",  51},
    {"MDIO",      52},

    /* SD Card */
    {"SD_DATA0",  39},
    {"SD_DATA1",  40},
    {"SD_DATA2",  41},
    {"SD_DATA3",  42},
    {"SD_CLK",    43},
    {"SD_CMD",    44},
    {"SD_PWRN",   45},
};

/* ------------------------------------------------------------------
 * p4hil_board  (no CircuitPython port yet)
 * ------------------------------------------------------------------ */
#elif defined(CONFIG_HARNESS_BOARD_P4HIL_BOARD)

static const board_pin_t k_pins[] = {
};

/* ------------------------------------------------------------------
 * Default: empty pin table
 * ------------------------------------------------------------------ */
#else

static const board_pin_t k_pins[] = {
};

#endif

/* ------------------------------------------------------------------ */

const board_pin_t *board_get_pins(size_t *count)
{
    *count = sizeof(k_pins) / sizeof(k_pins[0]);
    return k_pins;
}

const char *board_get_name(void)
{
    return CONFIG_HARNESS_BOARD_NAME;
}
