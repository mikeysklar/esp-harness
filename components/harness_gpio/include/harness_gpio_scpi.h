#pragma once

/**
 * SCPI handlers for the :GPIO subsystem. Each function is referenced from
 * harness_scpi's command table and follows the scpi-parser callback
 * convention: parse arguments via SCPI_Param*, push errors via
 * SCPI_ErrorPush, return SCPI_RES_OK / SCPI_RES_ERR.
 *
 * Pin arguments accept either a numeric GPIO or a quoted DUT pin label,
 * resolved via harness_dut_resolve_pin (declared in harness_dut.h).
 */

#include "scpi/scpi.h"

#ifdef __cplusplus
extern "C" {
#endif

scpi_result_t harness_gpio_scpi_dir(scpi_t *ctx);
scpi_result_t harness_gpio_scpi_dir_q(scpi_t *ctx);
scpi_result_t harness_gpio_scpi_pull(scpi_t *ctx);
scpi_result_t harness_gpio_scpi_write(scpi_t *ctx);
scpi_result_t harness_gpio_scpi_read_q(scpi_t *ctx);
scpi_result_t harness_gpio_scpi_toggle(scpi_t *ctx);
scpi_result_t harness_gpio_scpi_pulse(scpi_t *ctx);

/** Tear down all manually-configured GPIO state. Called from *RST. */
void harness_gpio_scpi_reset(void);

#ifdef __cplusplus
}
#endif
