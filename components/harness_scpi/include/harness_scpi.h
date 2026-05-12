#pragma once

#include <stddef.h>
#include "esp_err.h"
#include "scpi/scpi.h"
#include "harness_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HARNESS_FW_VERSION "0.1.0"

typedef struct {
    harness_io_t *io;           ///< Byte transport between USB CDC and parser.
    size_t task_stack_size;     ///< 0 -> default 8192.
    int task_priority;          ///< 0 -> default 5.
} harness_scpi_config_t;

/**
 * Start the SCPI parser task. Spins forever, draining io->rx into the parser
 * and pushing replies back into io->tx.
 */
esp_err_t harness_scpi_init(const harness_scpi_config_t *cfg);

/** Returns the global scpi_t context. Subsystem handlers do not normally
 *  need this -- it is passed to every callback as a parameter -- but the
 *  test main may. */
scpi_t *harness_scpi_context(void);

#ifdef __cplusplus
}
#endif
