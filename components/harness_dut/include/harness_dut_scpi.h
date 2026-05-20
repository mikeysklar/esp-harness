#pragma once

/**
 * SCPI handlers for the :DUT subsystem &mdash; persistent device-under-test
 * metadata stored in the NVS namespace `harness_dut`. The state survives
 * reboots and ESP-IDF app re-flashes (but not a `:DUT:CLE` or a chip erase).
 *
 * Records kept:
 *   - board name (`:DUT:NAME`)
 *   - free-form note (`:DUT:NOTE`)
 *   - up to HARNESS_DUT_MAX_WIRES (dut_label, host_label) wire records
 *     (`:DUT:WIRE`)
 *
 * Pin-to-GPIO mappings are compile-time constants defined per board in
 * board_pins.h.  They are visible to :GPIO:* and :BUS:*:INIT commands
 * via harness_dut_resolve_pin() but cannot be changed at runtime.
 */

#include "scpi/scpi.h"

#ifdef __cplusplus
extern "C" {
#endif

scpi_result_t harness_dut_scpi_name(scpi_t *ctx);
scpi_result_t harness_dut_scpi_name_q(scpi_t *ctx);
scpi_result_t harness_dut_scpi_note(scpi_t *ctx);
scpi_result_t harness_dut_scpi_note_q(scpi_t *ctx);
scpi_result_t harness_dut_scpi_board_q(scpi_t *ctx);
scpi_result_t harness_dut_scpi_wire(scpi_t *ctx);
scpi_result_t harness_dut_scpi_wire_del(scpi_t *ctx);
scpi_result_t harness_dut_scpi_wire_list_q(scpi_t *ctx);
scpi_result_t harness_dut_scpi_clear(scpi_t *ctx);

#ifdef __cplusplus
}
#endif
