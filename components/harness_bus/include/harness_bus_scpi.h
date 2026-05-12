#pragma once

/**
 * SCPI handlers for :BUS:I2C, :BUS:SPI, and :BUS:UART. I²C and SPI come in
 * two role flavors: controller (CONT, the harness drives transactions) and
 * target (TARG, the harness is addressed by an external controller). The
 * two roles share one peripheral instance per bus, so switching roles
 * tears down the other side first.
 *
 * Byte payloads (writes, reads, transfers) use IEEE 488.2 definite-length
 * arbitrary blocks `#<n><len><bytes>`. See PROTOCOL.md for the full grammar.
 *
 * Pin arguments to *_init handlers accept either a numeric GPIO or a quoted
 * DUT pin label, resolved via harness_dut_resolve_pin. `-1` is accepted to
 * mean "unused" (e.g. SPI MISO on a write-only device).
 */

#include "scpi/scpi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I²C controller (this device drives transactions). */
scpi_result_t harness_bus_scpi_i2c_cont_init(scpi_t *ctx);
scpi_result_t harness_bus_scpi_i2c_cont_deinit(scpi_t *ctx);
scpi_result_t harness_bus_scpi_i2c_cont_scan_q(scpi_t *ctx);
scpi_result_t harness_bus_scpi_i2c_cont_write(scpi_t *ctx);
scpi_result_t harness_bus_scpi_i2c_cont_read_q(scpi_t *ctx);
scpi_result_t harness_bus_scpi_i2c_cont_xfer_q(scpi_t *ctx);

/* I²C target (this device responds to an external controller). */
scpi_result_t harness_bus_scpi_i2c_targ_init(scpi_t *ctx);
scpi_result_t harness_bus_scpi_i2c_targ_deinit(scpi_t *ctx);
scpi_result_t harness_bus_scpi_i2c_targ_write(scpi_t *ctx);
scpi_result_t harness_bus_scpi_i2c_targ_read_q(scpi_t *ctx);
scpi_result_t harness_bus_scpi_i2c_targ_state_q(scpi_t *ctx);

/* SPI controller. */
scpi_result_t harness_bus_scpi_spi_cont_init(scpi_t *ctx);
scpi_result_t harness_bus_scpi_spi_cont_deinit(scpi_t *ctx);
scpi_result_t harness_bus_scpi_spi_cont_xfer_q(scpi_t *ctx);
scpi_result_t harness_bus_scpi_spi_cont_write(scpi_t *ctx);
scpi_result_t harness_bus_scpi_spi_cont_cs(scpi_t *ctx);

/* SPI target. */
scpi_result_t harness_bus_scpi_spi_targ_init(scpi_t *ctx);
scpi_result_t harness_bus_scpi_spi_targ_deinit(scpi_t *ctx);
scpi_result_t harness_bus_scpi_spi_targ_xfer_q(scpi_t *ctx);

/* UART (symmetric, no controller/target split). */
scpi_result_t harness_bus_scpi_uart_init(scpi_t *ctx);
scpi_result_t harness_bus_scpi_uart_deinit(scpi_t *ctx);
scpi_result_t harness_bus_scpi_uart_write(scpi_t *ctx);
scpi_result_t harness_bus_scpi_uart_read_q(scpi_t *ctx);
scpi_result_t harness_bus_scpi_uart_drain(scpi_t *ctx);

/** Tear down all bus state. Called from *RST. */
void harness_bus_scpi_reset(void);

#ifdef __cplusplus
}
#endif
