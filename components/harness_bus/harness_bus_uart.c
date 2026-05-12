#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"

#include "scpi/scpi.h"
#include "harness_bus_scpi.h"
#include "harness_bus_internal.h"
#include "harness_dut.h"

static const char *TAG = "bus.uart";

#define UART_PORT       UART_NUM_1
#define UART_BUF_BYTES  2048

static bool s_active;

void harness_bus_uart_teardown(void)
{
    if (s_active) {
        uart_driver_delete(UART_PORT);
        s_active = false;
        ESP_LOGI(TAG, "uart torn down");
    }
}

static const scpi_choice_def_t k_parity[] = {
    {"NONE", UART_PARITY_DISABLE},
    {"EVEN", UART_PARITY_EVEN},
    {"ODD",  UART_PARITY_ODD},
    SCPI_CHOICE_LIST_END
};

scpi_result_t harness_bus_scpi_uart_init(scpi_t *ctx)
{
    int tx, rx;
    uint32_t baud;
    int32_t databits = 8, stop = 1, parity = UART_PARITY_DISABLE;
    if (!harness_dut_resolve_pin(ctx, &tx))  return SCPI_RES_ERR;
    if (!harness_dut_resolve_pin(ctx, &rx))  return SCPI_RES_ERR;
    if (!SCPI_ParamUInt32(ctx, &baud,TRUE)) return SCPI_RES_ERR;
    SCPI_ParamInt32(ctx, &databits, FALSE);
    SCPI_ParamChoice(ctx, k_parity, &parity, FALSE);
    SCPI_ParamInt32(ctx, &stop,     FALSE);

    harness_bus_uart_teardown();

    uart_config_t cfg = {
        .baud_rate = baud,
        .data_bits = (databits == 5 ? UART_DATA_5_BITS :
                      databits == 6 ? UART_DATA_6_BITS :
                      databits == 7 ? UART_DATA_7_BITS : UART_DATA_8_BITS),
        .parity    = parity,
        .stop_bits = (stop == 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1),
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    if (uart_param_config(UART_PORT, &cfg) != ESP_OK ||
        uart_set_pin(UART_PORT, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK ||
        uart_driver_install(UART_PORT, UART_BUF_BYTES, UART_BUF_BYTES, 0, NULL, 0) != ESP_OK) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    s_active = true;
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_uart_deinit(scpi_t *ctx)
{
    (void)ctx;
    harness_bus_uart_teardown();
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_uart_write(scpi_t *ctx)
{
    if (!s_active) { SCPI_ErrorPush(ctx, SCPI_ERROR_INIT_IGNORED); return SCPI_RES_ERR; }
    const char *data; size_t len;
    if (!SCPI_ParamArbitraryBlock(ctx, &data, &len, TRUE)) return SCPI_RES_ERR;
    int written = uart_write_bytes(UART_PORT, data, len);
    if (written < 0 || (size_t)written != len) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_uart_read_q(scpi_t *ctx)
{
    if (!s_active) { SCPI_ErrorPush(ctx, SCPI_ERROR_INIT_IGNORED); return SCPI_RES_ERR; }
    uint32_t n, timeout_ms;
    if (!SCPI_ParamUInt32(ctx, &n,           TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamUInt32(ctx, &timeout_ms,  TRUE)) return SCPI_RES_ERR;
    if (n > 4096) { SCPI_ErrorPush(ctx, SCPI_ERROR_DATA_OUT_OF_RANGE); return SCPI_RES_ERR; }

    uint8_t *buf = malloc(n);
    if (!buf) { SCPI_ErrorPush(ctx, SCPI_ERROR_OUT_OF_MEMORY); return SCPI_RES_ERR; }
    int got = uart_read_bytes(UART_PORT, buf, n, pdMS_TO_TICKS(timeout_ms));
    if (got < 0) got = 0;
    SCPI_ResultArbitraryBlock(ctx, buf, got);
    free(buf);
    return SCPI_RES_OK;
}

scpi_result_t harness_bus_scpi_uart_drain(scpi_t *ctx)
{
    if (!s_active) { SCPI_ErrorPush(ctx, SCPI_ERROR_INIT_IGNORED); return SCPI_RES_ERR; }
    (void)ctx;
    uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(500));
    uart_flush_input(UART_PORT);
    return SCPI_RES_OK;
}
