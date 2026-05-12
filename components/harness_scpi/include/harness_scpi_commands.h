#pragma once

#include "scpi/scpi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the master SCPI command table. Implemented in harness_scpi_table.c;
 * its rows reference handler functions defined by harness_gpio, harness_bus,
 * and harness_dut.
 */
const scpi_command_t *harness_scpi_command_table(void);

/**
 * Called from the *RST callback. Each subsystem should expose its own
 * reset function (named scpi_*_reset) that this calls; that keeps the
 * top-level command table the single place that knows about every
 * subsystem.
 */
void harness_scpi_handle_reset(void);

#ifdef __cplusplus
}
#endif
