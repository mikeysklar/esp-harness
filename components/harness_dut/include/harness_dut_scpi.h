#pragma once

/**
 * SCPI handlers for the :DUT subsystem &mdash; persistent device-under-test
 * metadata stored in the NVS namespace `harness_dut`. The state survives
 * reboots and ESP-IDF app re-flashes (but not a `:DUT:CLE` or a chip erase).
 *
 * Records kept:
 *   - board name (`:DUT:NAME`)
 *   - free-form note (`:DUT:NOTE`)
 *   - up to HARNESS_DUT_MAX_PINS named pin → GPIO bindings (`:DUT:PIN`)
 *   - up to HARNESS_DUT_MAX_WIRES (dut_label, host_label) wire records
 *     (`:DUT:WIRE`)
 *
 * The label table also backs harness_dut_resolve_pin so any `:GPIO:*` or
 * `:BUS:*:INIT` command can address pins by name.
 */

#include "scpi/scpi.h"

#ifdef __cplusplus
extern "C" {
#endif

scpi_result_t harness_dut_scpi_name(scpi_t *ctx);
scpi_result_t harness_dut_scpi_name_q(scpi_t *ctx);
scpi_result_t harness_dut_scpi_note(scpi_t *ctx);
scpi_result_t harness_dut_scpi_note_q(scpi_t *ctx);
scpi_result_t harness_dut_scpi_pin(scpi_t *ctx);
scpi_result_t harness_dut_scpi_pin_q(scpi_t *ctx);
scpi_result_t harness_dut_scpi_pin_del(scpi_t *ctx);
scpi_result_t harness_dut_scpi_pin_list_q(scpi_t *ctx);
scpi_result_t harness_dut_scpi_wire(scpi_t *ctx);
scpi_result_t harness_dut_scpi_wire_del(scpi_t *ctx);
scpi_result_t harness_dut_scpi_wire_list_q(scpi_t *ctx);
scpi_result_t harness_dut_scpi_clear(scpi_t *ctx);

#ifdef __cplusplus
}
#endif
