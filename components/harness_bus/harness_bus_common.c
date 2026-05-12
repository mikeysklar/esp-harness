#include "harness_bus_scpi.h"
#include "harness_bus_internal.h"

/* Tear down every bus that has been brought up. Order: controllers first,
 * then targets, then UART. Each individual function is a no-op if its bus
 * isn't currently active. */
void harness_bus_scpi_reset(void)
{
    harness_bus_i2c_cont_teardown();
    harness_bus_i2c_targ_teardown();
    harness_bus_spi_cont_teardown();
    harness_bus_spi_targ_teardown();
    harness_bus_uart_teardown();
}
