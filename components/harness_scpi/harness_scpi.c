#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"

#include "scpi/scpi.h"
#include "harness_scpi.h"
#include "harness_scpi_commands.h"

static const char *TAG = "scpi";

#define SCPI_INPUT_BUFFER_LENGTH 256
#define SCPI_ERROR_QUEUE_SIZE    17

static char s_input_buffer[SCPI_INPUT_BUFFER_LENGTH];
static scpi_error_t s_error_queue[SCPI_ERROR_QUEUE_SIZE];
static scpi_t s_scpi;
static harness_io_t *s_io;
static char s_serial[13];   // 6-byte MAC as hex string

static size_t scpi_write(scpi_t *context, const char *data, size_t len)
{
    (void)context;
    if (!s_io) return 0;
    return harness_io_write(s_io, data, len, portMAX_DELAY);
}

static int scpi_error_cb(scpi_t *context, int_fast16_t err)
{
    (void)context;
    ESP_LOGW(TAG, "scpi error %d", (int)err);
    return 0;
}

static scpi_result_t scpi_control_cb(scpi_t *context, scpi_ctrl_name_t ctrl, scpi_reg_val_t val)
{
    (void)context; (void)ctrl; (void)val;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_reset_cb(scpi_t *context)
{
    (void)context;
    harness_scpi_handle_reset();
    return SCPI_RES_OK;
}

static scpi_result_t scpi_flush_cb(scpi_t *context)
{
    (void)context;
    return SCPI_RES_OK;
}

static scpi_interface_t s_interface = {
    .error   = scpi_error_cb,
    .write   = scpi_write,
    .control = scpi_control_cb,
    .flush   = scpi_flush_cb,
    .reset   = scpi_reset_cb,
};

static void build_serial(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BASE);
    snprintf(s_serial, sizeof(s_serial), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void scpi_task(void *arg)
{
    (void)arg;
    uint8_t buf[64];

    ESP_LOGI(TAG, "SCPI task running, fw %s, serial %s", HARNESS_FW_VERSION, s_serial);
    while (1) {
        size_t n = harness_io_read(s_io, buf, sizeof(buf), portMAX_DELAY);
        if (n > 0) {
            SCPI_Input(&s_scpi, (const char *)buf, (int)n);
        }
    }
}

esp_err_t harness_scpi_init(const harness_scpi_config_t *cfg)
{
    if (!cfg || !cfg->io) return ESP_ERR_INVALID_ARG;
    s_io = cfg->io;

    build_serial();

    SCPI_Init(&s_scpi,
              harness_scpi_command_table(),
              &s_interface,
              scpi_units_def,
              "Chickadee", "esp-harness", s_serial, HARNESS_FW_VERSION,
              s_input_buffer, SCPI_INPUT_BUFFER_LENGTH,
              s_error_queue, SCPI_ERROR_QUEUE_SIZE);

    size_t stack = cfg->task_stack_size ? cfg->task_stack_size : 8192;
    int prio = cfg->task_priority ? cfg->task_priority : 5;
    if (xTaskCreate(scpi_task, "scpi", stack, NULL, prio, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

scpi_t *harness_scpi_context(void)
{
    return &s_scpi;
}
