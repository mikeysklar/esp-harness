#pragma once

/* Shared internal hooks among harness_bus_*.c files. Each xxx_teardown
 * function is idempotent and safe to call when the bus is not active. */

void harness_bus_i2c_cont_teardown(void);
void harness_bus_i2c_targ_teardown(void);
void harness_bus_spi_cont_teardown(void);
void harness_bus_spi_targ_teardown(void);
void harness_bus_uart_teardown(void);
