#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/spi_master.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"

#include "scpi/scpi.h"
#include "harness_bus_scpi.h"
#include "harness_bus_internal.h"
#include "harness_dut.h"

static const char *TAG = "bus.spi";

#define SPI_HOST_USED      SPI2_HOST
#define SPI_MAX_XFER       4096

/* ---- Controller state ---- */

static bool             s_cont_active;
static spi_device_handle_t s_cont_dev;
static int              s_cont_cs = -1;

void harness_bus_spi_cont_teardown(void)
{
    if (s_cont_active) {
        if (s_cont_dev) { spi_bus_remove_device(s_cont_dev); s_cont_dev = NULL; }
        spi_bus_free(SPI_HOST_USED);
        s_cont_active = false;
        s_cont_cs = -1;
        ESP_LOGI(TAG, "controller torn down");
    }
}

scpi_result_t harness_bus_scpi_spi_cont_init(scpi_t *ctx)
{
    int sck, mosi, miso, cs;
    int32_t mode;
    uint32_t hz;
    if (!harness_dut_resolve_pin(ctx, &sck))  return SCPI_RES_ERR;
    if (!harness_dut_resolve_pin(ctx, &mosi)) return SCPI_RES_ERR;
    if (!harness_dut_resolve_pin(ctx, &miso)) return SCPI_RES_ERR;
    if (!harness_dut_resolve_pin(ctx, &cs))   return SCPI_RES_ERR;
    if (!SCPI_ParamUInt32(ctx, &hz,  TRUE))   return SCPI_RES_ERR;
    if (!SCPI_ParamInt32(ctx, &mode, TRUE))   return SCPI_RES_ERR;
    if (mode < 0 || mode > 3) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    harness_bus_spi_cont_teardown();
    harness_bus_spi_targ_teardown();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .sclk_io_num = sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SPI_MAX_XFER,
    };
    if (spi_bus_initialize(SPI_HOST_USED, &bus_cfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = hz,
        .mode = (uint8_t)mode,
        .spics_io_num = cs,
        .queue_size = 1,
    };
    if (spi_bus_add_device(SPI_HOST_USED, &dev_cfg, &s_cont_dev) != ESP_OK) {
        spi_bus_free(SPI_HOST_USED);
        s_cont_dev = NULL;
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    s_cont_active = true;
    s_cont_cs = cs;
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_spi_cont_deinit(scpi_t *ctx)
{
    (void)ctx;
    harness_bus_spi_cont_teardown();
    return SCPI_RES_OK;
}

static scpi_result_t spi_cont_do_xfer(scpi_t *ctx, bool emit_rx)
{
    if (!s_cont_active) { SCPI_ErrorPush(ctx, SCPI_ERROR_INIT_IGNORED); return SCPI_RES_ERR; }
    const char *tx; size_t len;
    if (!SCPI_ParamArbitraryBlock(ctx, &tx, &len, TRUE)) return SCPI_RES_ERR;
    if (len > SPI_MAX_XFER) { SCPI_ErrorPush(ctx, SCPI_ERROR_DATA_OUT_OF_RANGE); return SCPI_RES_ERR; }

    uint8_t *rx = malloc(len);
    if (!rx) { SCPI_ErrorPush(ctx, SCPI_ERROR_OUT_OF_MEMORY); return SCPI_RES_ERR; }
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = emit_rx ? rx : NULL,
    };
    esp_err_t err = spi_device_transmit(s_cont_dev, &t);
    if (err != ESP_OK) {
        free(rx);
        SCPI_ErrorPush(ctx, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    if (emit_rx) SCPI_ResultArbitraryBlock(ctx, rx, len);
    free(rx);
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_spi_cont_xfer_q(scpi_t *ctx) { return spi_cont_do_xfer(ctx, true);  }
scpi_result_t harness_bus_scpi_spi_cont_write(scpi_t *ctx)  { return spi_cont_do_xfer(ctx, false); }

scpi_result_t harness_bus_scpi_spi_cont_cs(scpi_t *ctx)
{
    if (s_cont_cs < 0) { SCPI_ErrorPush(ctx, SCPI_ERROR_INIT_IGNORED); return SCPI_RES_ERR; }
    scpi_bool_t level;
    if (!SCPI_ParamBool(ctx, &level, TRUE)) return SCPI_RES_ERR;
    /* Forcibly switch the CS line to a manually-driven GPIO. The harness
     * user does this when they want to bracket multiple transactions under
     * a single chip-select assertion. */
    gpio_set_direction(s_cont_cs, GPIO_MODE_OUTPUT);
    gpio_set_level(s_cont_cs, level ? 1 : 0);
    return SCPI_RES_OK;
}

/* ---- Target state ---- */

static bool s_targ_active;

void harness_bus_spi_targ_teardown(void)
{
    if (s_targ_active) {
        spi_slave_free(SPI_HOST_USED);
        s_targ_active = false;
        ESP_LOGI(TAG, "target torn down");
    }
}

scpi_result_t harness_bus_scpi_spi_targ_init(scpi_t *ctx)
{
    int sck, mosi, miso, cs;
    int32_t mode;
    if (!harness_dut_resolve_pin(ctx, &sck))  return SCPI_RES_ERR;
    if (!harness_dut_resolve_pin(ctx, &mosi)) return SCPI_RES_ERR;
    if (!harness_dut_resolve_pin(ctx, &miso)) return SCPI_RES_ERR;
    if (!harness_dut_resolve_pin(ctx, &cs))   return SCPI_RES_ERR;
    if (!SCPI_ParamInt32(ctx, &mode, TRUE))   return SCPI_RES_ERR;
    if (mode < 0 || mode > 3) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    harness_bus_spi_cont_teardown();
    harness_bus_spi_targ_teardown();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .sclk_io_num = sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SPI_MAX_XFER,
    };
    spi_slave_interface_config_t slv_cfg = {
        .spics_io_num = cs,
        .flags = 0,
        .queue_size = 1,
        .mode = (uint8_t)mode,
    };
    if (spi_slave_initialize(SPI_HOST_USED, &bus_cfg, &slv_cfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    s_targ_active = true;
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_spi_targ_deinit(scpi_t *ctx)
{
    (void)ctx;
    harness_bus_spi_targ_teardown();
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_spi_targ_xfer_q(scpi_t *ctx)
{
    if (!s_targ_active) { SCPI_ErrorPush(ctx, SCPI_ERROR_INIT_IGNORED); return SCPI_RES_ERR; }
    const char *tx; size_t len; uint32_t timeout_ms;
    if (!SCPI_ParamArbitraryBlock(ctx, &tx, &len, TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamUInt32(ctx, &timeout_ms, TRUE)) return SCPI_RES_ERR;
    if (len > SPI_MAX_XFER) { SCPI_ErrorPush(ctx, SCPI_ERROR_DATA_OUT_OF_RANGE); return SCPI_RES_ERR; }

    uint8_t *rx = malloc(len);
    if (!rx) { SCPI_ErrorPush(ctx, SCPI_ERROR_OUT_OF_MEMORY); return SCPI_RES_ERR; }
    spi_slave_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_slave_transmit(SPI_HOST_USED, &t, pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK) {
        free(rx);
        SCPI_ErrorPush(ctx, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    SCPI_ResultArbitraryBlock(ctx, rx, len);
    free(rx);
    return SCPI_RES_OK;
}
