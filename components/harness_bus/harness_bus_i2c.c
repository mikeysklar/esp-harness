#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "driver/i2c_master.h"
#include "driver/i2c_slave.h"

#include "scpi/scpi.h"
#include "harness_bus_scpi.h"
#include "harness_bus_internal.h"
#include "harness_dut.h"

static const char *TAG = "bus.i2c";

#define I2C_TIMEOUT_MS    100
#define I2C_TARG_RX_RING  512
#define I2C_TARG_TX_DEPTH 256

/* ---- Controller state ---- */

static i2c_master_bus_handle_t s_ctrl_bus;
static uint32_t s_ctrl_hz = 100000;

void harness_bus_i2c_cont_teardown(void)
{
    if (s_ctrl_bus) {
        i2c_del_master_bus(s_ctrl_bus);
        s_ctrl_bus = NULL;
        ESP_LOGI(TAG, "controller torn down");
    }
}

scpi_result_t harness_bus_scpi_i2c_cont_init(scpi_t *ctx)
{
    int sda, scl;
    uint32_t hz;
    if (!harness_dut_resolve_pin(ctx, &sda)) return SCPI_RES_ERR;
    if (!harness_dut_resolve_pin(ctx, &scl)) return SCPI_RES_ERR;
    if (!SCPI_ParamUInt32(ctx, &hz, TRUE))   return SCPI_RES_ERR;

    harness_bus_i2c_cont_teardown();
    harness_bus_i2c_targ_teardown();

    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&cfg, &s_ctrl_bus) != ESP_OK) {
        s_ctrl_bus = NULL;
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    /* The configured clock-speed lives on each device, not the bus, so we
     * remember the requested speed and apply it when adding a transient
     * device handle in each transaction. */
    s_ctrl_hz = hz;
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_i2c_cont_deinit(scpi_t *ctx)
{
    (void)ctx;
    harness_bus_i2c_cont_teardown();
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_i2c_cont_scan_q(scpi_t *ctx)
{
    if (!s_ctrl_bus) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_INIT_IGNORED);
        return SCPI_RES_ERR;
    }
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(s_ctrl_bus, addr, I2C_TIMEOUT_MS) == ESP_OK) {
            SCPI_ResultUInt8(ctx, addr);
        }
    }
    return SCPI_RES_OK;
}

static bool open_dev(uint8_t addr, i2c_master_dev_handle_t *dev_out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = s_ctrl_hz,
    };
    return i2c_master_bus_add_device(s_ctrl_bus, &cfg, dev_out) == ESP_OK;
}

scpi_result_t harness_bus_scpi_i2c_cont_write(scpi_t *ctx)
{
    if (!s_ctrl_bus) { SCPI_ErrorPush(ctx, SCPI_ERROR_INIT_IGNORED); return SCPI_RES_ERR; }
    uint32_t addr; const char *data; size_t len;
    if (!SCPI_ParamUInt32(ctx, &addr, TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamArbitraryBlock(ctx, &data, &len, TRUE)) return SCPI_RES_ERR;

    i2c_master_dev_handle_t dev;
    if (!open_dev((uint8_t)addr, &dev)) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    esp_err_t err = i2c_master_transmit(dev, (const uint8_t *)data, len, I2C_TIMEOUT_MS);
    i2c_master_bus_rm_device(dev);
    if (err != ESP_OK) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_i2c_cont_read_q(scpi_t *ctx)
{
    if (!s_ctrl_bus) { SCPI_ErrorPush(ctx, SCPI_ERROR_INIT_IGNORED); return SCPI_RES_ERR; }
    uint32_t addr, n;
    if (!SCPI_ParamUInt32(ctx, &addr, TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamUInt32(ctx, &n,    TRUE)) return SCPI_RES_ERR;
    if (n > 4096) { SCPI_ErrorPush(ctx, SCPI_ERROR_DATA_OUT_OF_RANGE); return SCPI_RES_ERR; }

    uint8_t *buf = malloc(n);
    if (!buf) { SCPI_ErrorPush(ctx, SCPI_ERROR_OUT_OF_MEMORY); return SCPI_RES_ERR; }

    i2c_master_dev_handle_t dev;
    if (!open_dev((uint8_t)addr, &dev)) {
        free(buf);
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    esp_err_t err = i2c_master_receive(dev, buf, n, I2C_TIMEOUT_MS);
    i2c_master_bus_rm_device(dev);
    if (err != ESP_OK) {
        free(buf);
        SCPI_ErrorPush(ctx, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    SCPI_ResultArbitraryBlock(ctx, buf, n);
    free(buf);
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_i2c_cont_xfer_q(scpi_t *ctx)
{
    if (!s_ctrl_bus) { SCPI_ErrorPush(ctx, SCPI_ERROR_INIT_IGNORED); return SCPI_RES_ERR; }
    uint32_t addr; const char *tx; size_t tx_len; uint32_t rx_n;
    if (!SCPI_ParamUInt32(ctx, &addr, TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamArbitraryBlock(ctx, &tx, &tx_len, TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamUInt32(ctx, &rx_n, TRUE)) return SCPI_RES_ERR;
    if (rx_n > 4096) { SCPI_ErrorPush(ctx, SCPI_ERROR_DATA_OUT_OF_RANGE); return SCPI_RES_ERR; }

    uint8_t *rx = malloc(rx_n);
    if (!rx) { SCPI_ErrorPush(ctx, SCPI_ERROR_OUT_OF_MEMORY); return SCPI_RES_ERR; }

    i2c_master_dev_handle_t dev;
    if (!open_dev((uint8_t)addr, &dev)) {
        free(rx);
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    esp_err_t err = i2c_master_transmit_receive(dev, (const uint8_t *)tx, tx_len,
                                                rx, rx_n, I2C_TIMEOUT_MS);
    i2c_master_bus_rm_device(dev);
    if (err != ESP_OK) {
        free(rx);
        SCPI_ErrorPush(ctx, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    SCPI_ResultArbitraryBlock(ctx, rx, rx_n);
    free(rx);
    return SCPI_RES_OK;
}

/* ---- Target state ---- */

static i2c_slave_dev_handle_t s_targ_dev;
static StreamBufferHandle_t   s_targ_rx_ring;     /* bytes the controller wrote to us */

static IRAM_ATTR bool targ_on_receive(i2c_slave_dev_handle_t dev,
                                      const i2c_slave_rx_done_event_data_t *evt,
                                      void *user_ctx)
{
    (void)dev; (void)user_ctx;
    BaseType_t hp_wake = pdFALSE;
    if (s_targ_rx_ring && evt->length > 0) {
        xStreamBufferSendFromISR(s_targ_rx_ring, evt->buffer, evt->length, &hp_wake);
    }
    return hp_wake == pdTRUE;
}

void harness_bus_i2c_targ_teardown(void)
{
    if (s_targ_dev) {
        i2c_del_slave_device(s_targ_dev);
        s_targ_dev = NULL;
    }
    if (s_targ_rx_ring) {
        vStreamBufferDelete(s_targ_rx_ring);
        s_targ_rx_ring = NULL;
    }
}

scpi_result_t harness_bus_scpi_i2c_targ_init(scpi_t *ctx)
{
    int sda, scl;
    uint32_t addr;
    uint32_t hz = 100000;
    if (!harness_dut_resolve_pin(ctx, &sda))  return SCPI_RES_ERR;
    if (!harness_dut_resolve_pin(ctx, &scl))  return SCPI_RES_ERR;
    if (!SCPI_ParamUInt32(ctx, &addr, TRUE))  return SCPI_RES_ERR;
    SCPI_ParamUInt32(ctx, &hz, FALSE);
    (void)hz;  /* hz is ignored by target driver but accepted for symmetry */

    harness_bus_i2c_cont_teardown();
    harness_bus_i2c_targ_teardown();

    s_targ_rx_ring = xStreamBufferCreate(I2C_TARG_RX_RING, 1);
    if (!s_targ_rx_ring) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_OUT_OF_MEMORY);
        return SCPI_RES_ERR;
    }

    i2c_slave_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .send_buf_depth = I2C_TARG_TX_DEPTH,
        .receive_buf_depth = I2C_TARG_RX_RING,
        .slave_addr = (uint16_t)addr,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7,
    };
    if (i2c_new_slave_device(&cfg, &s_targ_dev) != ESP_OK) {
        s_targ_dev = NULL;
        harness_bus_i2c_targ_teardown();
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    i2c_slave_event_callbacks_t cbs = { .on_receive = targ_on_receive };
    i2c_slave_register_event_callbacks(s_targ_dev, &cbs, NULL);
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_i2c_targ_deinit(scpi_t *ctx)
{
    (void)ctx;
    harness_bus_i2c_targ_teardown();
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_i2c_targ_write(scpi_t *ctx)
{
    if (!s_targ_dev) { SCPI_ErrorPush(ctx, SCPI_ERROR_INIT_IGNORED); return SCPI_RES_ERR; }
    const char *data; size_t len;
    if (!SCPI_ParamArbitraryBlock(ctx, &data, &len, TRUE)) return SCPI_RES_ERR;
    uint32_t written = 0;
    if (i2c_slave_write(s_targ_dev, (const uint8_t *)data, len, &written, 100) != ESP_OK) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_i2c_targ_read_q(scpi_t *ctx)
{
    if (!s_targ_dev) { SCPI_ErrorPush(ctx, SCPI_ERROR_INIT_IGNORED); return SCPI_RES_ERR; }
    uint32_t n, timeout_ms;
    if (!SCPI_ParamUInt32(ctx, &n,           TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamUInt32(ctx, &timeout_ms,  TRUE)) return SCPI_RES_ERR;
    if (n > 4096) { SCPI_ErrorPush(ctx, SCPI_ERROR_DATA_OUT_OF_RANGE); return SCPI_RES_ERR; }

    uint8_t *buf = malloc(n);
    if (!buf) { SCPI_ErrorPush(ctx, SCPI_ERROR_OUT_OF_MEMORY); return SCPI_RES_ERR; }
    size_t got = xStreamBufferReceive(s_targ_rx_ring, buf, n, pdMS_TO_TICKS(timeout_ms));
    SCPI_ResultArbitraryBlock(ctx, buf, got);
    free(buf);
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_i2c_targ_state_q(scpi_t *ctx)
{
    size_t rx_queued = s_targ_rx_ring ? xStreamBufferBytesAvailable(s_targ_rx_ring) : 0;
    /* TX-queued count isn't exposed by the slave driver; report 0 as a placeholder. */
    SCPI_ResultUInt32(ctx, 0);
    SCPI_ResultUInt32(ctx, (uint32_t)rx_queued);
    return SCPI_RES_OK;
}
