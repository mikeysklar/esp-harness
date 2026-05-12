#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"

#include "harness_io.h"
#include "harness_scpi.h"
#include "harness_dut.h"

static const char *TAG = "main";

#define IO_RX_SIZE   256
#define IO_TX_SIZE   (8 * 1024)

static harness_io_t s_io;

/* USB CDC RX callback: drain into the SCPI parser's RX stream buffer. */
static void cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    (void)event;
    uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
    size_t got = 0;
    if (tinyusb_cdcacm_read(itf, buf, sizeof(buf), &got) == ESP_OK && got > 0) {
        xStreamBufferSendFromISR(s_io.rx, buf, got, NULL);
    }
}

/* Drain the SCPI parser's TX stream buffer and push to USB CDC. */
static void cdc_tx_task(void *arg)
{
    (void)arg;
    uint8_t buf[256];
    while (1) {
        size_t n = xStreamBufferReceive(s_io.tx, buf, sizeof(buf), pdMS_TO_TICKS(20));
        if (n == 0) continue;
        size_t off = 0;
        while (off < n) {
            size_t chunk = n - off;
            tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, buf + off, chunk);
            if (tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(100)) == ESP_OK) {
                off += chunk;
            } else {
                break;   /* host not draining: drop the rest of this batch */
            }
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "esp-harness starting");

    /* DUT metadata first -- harness_gpio's pin resolver consults it. */
    ESP_ERROR_CHECK(harness_dut_init());

    /* TinyUSB CDC */
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = &cdc_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg));

    /* Stream-buffer transport */
    ESP_ERROR_CHECK(harness_io_create(IO_RX_SIZE, IO_TX_SIZE, &s_io));
    xTaskCreate(cdc_tx_task, "cdc_tx", 4096, NULL, 6, NULL);

    /* SCPI parser */
    harness_scpi_config_t scpi_cfg = {
        .io = &s_io,
        .task_stack_size = 8192,
        .task_priority = 5,
    };
    ESP_ERROR_CHECK(harness_scpi_init(&scpi_cfg));

    ESP_LOGI(TAG, "ready: USB CDC up, SCPI listening");
}
