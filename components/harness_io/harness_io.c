#include "harness_io.h"

esp_err_t harness_io_create(size_t rx_size, size_t tx_size, harness_io_t *io)
{
    if (!io) return ESP_ERR_INVALID_ARG;
    io->rx = xStreamBufferCreate(rx_size, 1);
    io->tx = xStreamBufferCreate(tx_size, 1);
    if (!io->rx || !io->tx) {
        harness_io_destroy(io);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void harness_io_destroy(harness_io_t *io)
{
    if (!io) return;
    if (io->rx) { vStreamBufferDelete(io->rx); io->rx = NULL; }
    if (io->tx) { vStreamBufferDelete(io->tx); io->tx = NULL; }
}
