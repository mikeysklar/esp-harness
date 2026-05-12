#pragma once

#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bidirectional byte-stream pair used between the USB CDC transport and the
 * SCPI parser task. RX carries host -> device bytes; TX carries device -> host
 * bytes. Bytes are unframed: the SCPI parser handles line termination
 * internally.
 */
typedef struct {
    StreamBufferHandle_t rx;
    StreamBufferHandle_t tx;
} harness_io_t;

/** Create stream buffers. */
esp_err_t harness_io_create(size_t rx_size, size_t tx_size, harness_io_t *io);

/** Free buffers created by harness_io_create. */
void harness_io_destroy(harness_io_t *io);

static inline size_t harness_io_read(harness_io_t *io, void *buf, size_t len, TickType_t timeout)
{
    return xStreamBufferReceive(io->rx, buf, len, timeout);
}

static inline size_t harness_io_write(harness_io_t *io, const void *data, size_t len, TickType_t timeout)
{
    return xStreamBufferSend(io->tx, data, len, timeout);
}

#ifdef __cplusplus
}
#endif
