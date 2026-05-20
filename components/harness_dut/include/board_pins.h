#pragma once

/**
 * @file board_pins.h
 * @brief Compile-time board pin tables, selected via Kconfig.
 *
 * Each entry maps a CircuitPython pin name (e.g. ``"I2C_SDA"``) to the GPIO
 * number that name refers to on that board.  This is the same data that lives
 * in a board's ``pins.c`` in the CircuitPython source tree.
 *
 * Wire mappings remain dynamic in NVS — see harness_dut.c.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A single pin binding: a human-readable label and its GPIO number. */
typedef struct {
    const char *label;
    int8_t      gpio;   /**< -1 = power/ground/NC (no GPIO) */
} board_pin_t;

/**
 * Return the pin table for the currently configured board.
 * @param[out] count  Receives the number of entries.
 * @return Pointer to a static const array of board_pin_t.
 */
const board_pin_t *board_get_pins(size_t *count);

/**
 * Return the human-readable board name (e.g. "adafruit_feather_esp32s3").
 * This is the value of CONFIG_HARNESS_BOARD at compile time.
 */
const char *board_get_name(void);

#ifdef __cplusplus
}
#endif
