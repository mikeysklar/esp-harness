#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "scpi/scpi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HARNESS_DUT_MAX_LABEL_LEN  31
#define HARNESS_DUT_MAX_NAME_LEN   63
#define HARNESS_DUT_MAX_NOTE_LEN   255
#define HARNESS_DUT_MAX_PINS       32
#define HARNESS_DUT_MAX_WIRES      32

/** Open the NVS namespace and load any pre-existing metadata. */
esp_err_t harness_dut_init(void);

/** Look up a DUT pin label and return its GPIO number. */
bool harness_dut_pin_lookup(const char *label, int *gpio_out);

/**
 * Read the next SCPI argument as a pin reference. Accepts either:
 *   - an integer (passed through as-is; callers may permit -1 as a "unused" sentinel)
 *   - a quoted string label, resolved via the DUT pin table
 *
 * On success returns true and writes the GPIO number to *gpio_out.
 * On failure pushes a SCPI error and returns false.
 *
 * This intentionally does NOT range-check the integer; the caller decides
 * what's acceptable (e.g. :BUS:SPI:INIT accepts -1 for unused pins, but
 * :GPIO:WRITE doesn't).
 */
bool harness_dut_resolve_pin(scpi_t *ctx, int *gpio_out);

#ifdef __cplusplus
}
#endif
