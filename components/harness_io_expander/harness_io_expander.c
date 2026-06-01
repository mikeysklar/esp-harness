#include "harness_io_expander.h"

#include <string.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "io_expander";

/* PI4IOE5V9535 register addresses */
#define REG_INPUT_PORT0    0x00
#define REG_INPUT_PORT1    0x01
#define REG_OUTPUT_PORT0   0x02
#define REG_OUTPUT_PORT1   0x03
#define REG_POLARITY_PORT0 0x04
#define REG_POLARITY_PORT1 0x05
#define REG_CONFIG_PORT0   0x06
#define REG_CONFIG_PORT1   0x07

#define I2C_TIMEOUT_MS  100

/* ---- Static state (single expander instance) ---- */
static struct {
    bool   initialized;
    int    sda;
    int    scl;
    uint8_t addr;
    i2c_master_bus_handle_t   bus;
    i2c_master_dev_handle_t   dev;
    uint16_t output_cache;   /* shadow of output port registers */
    uint16_t config_cache;   /* shadow of config port registers */
} s_exp;

/* ---- Low-level I2C register access ---- */

static esp_err_t read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_exp.dev, &reg, 1, val, 1, I2C_TIMEOUT_MS);
}

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_exp.dev, buf, 2, I2C_TIMEOUT_MS);
}

/* ---- Public API ---- */

esp_err_t harness_io_expander_init(int sda, int scl, uint8_t i2c_addr,
                                   uint32_t i2c_clk_hz)
{
    if (s_exp.initialized) {
        ESP_LOGW(TAG, "already initialized, deinitializing first");
        harness_io_expander_deinit();
    }

    memset(&s_exp, 0, sizeof(s_exp));
    s_exp.sda  = sda;
    s_exp.scl  = scl;
    s_exp.addr = i2c_addr;
    s_exp.output_cache = 0x0000;
    s_exp.config_cache = 0xFFFF;  /* default: all inputs */

    /* Create I2C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port   = I2C_NUM_1,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_exp.bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Add the expander device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = i2c_addr,
        .scl_speed_hz    = i2c_clk_hz,
    };
    err = i2c_master_bus_add_device(s_exp.bus, &dev_cfg, &s_exp.dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        i2c_del_master_bus(s_exp.bus);
        s_exp.bus = NULL;
        return err;
    }

    /* Probe to verify the expander is alive */
    uint8_t dummy;
    err = i2c_master_transmit_receive(s_exp.dev, (uint8_t[]){REG_INPUT_PORT0}, 1,
                                      &dummy, 1, I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "expander at 0x%02x not responding: %s", i2c_addr, esp_err_to_name(err));
        i2c_master_bus_rm_device(s_exp.dev);
        i2c_del_master_bus(s_exp.bus);
        s_exp.dev = NULL;
        s_exp.bus = NULL;
        return err;
    }

    /* Read back current config and output registers to sync caches */
    {
        uint8_t cfg0, cfg1, out0, out1;
        if (read_reg(REG_CONFIG_PORT0, &cfg0)  == ESP_OK &&
            read_reg(REG_CONFIG_PORT1, &cfg1)  == ESP_OK &&
            read_reg(REG_OUTPUT_PORT0, &out0) == ESP_OK &&
            read_reg(REG_OUTPUT_PORT1, &out1) == ESP_OK) {
            s_exp.config_cache  = (uint16_t)cfg0 | ((uint16_t)cfg1 << 8);
            s_exp.output_cache  = (uint16_t)out0 | ((uint16_t)out1 << 8);
        }
    }

    s_exp.initialized = true;
    ESP_LOGI(TAG, "PI4IOE5V9535 at 0x%02x (SDA=%d, SCL=%d, %lu Hz)",
             i2c_addr, sda, scl, (unsigned long)i2c_clk_hz);
    return ESP_OK;
}

void harness_io_expander_deinit(void)
{
    if (!s_exp.initialized) return;
    if (s_exp.dev) i2c_master_bus_rm_device(s_exp.dev);
    if (s_exp.bus) i2c_del_master_bus(s_exp.bus);
    memset(&s_exp, 0, sizeof(s_exp));
    ESP_LOGI(TAG, "deinitialized");
}

bool harness_io_expander_is_initialized(void)
{
    return s_exp.initialized;
}

esp_err_t harness_io_expander_set_dir(uint8_t pin, bool input)
{
    if (pin >= IO_EXPANDER_PIN_COUNT) return ESP_ERR_INVALID_ARG;
    if (!s_exp.initialized) return ESP_ERR_INVALID_STATE;

    uint16_t new_config = s_exp.config_cache;
    if (input) {
        new_config |=  (1U << pin);
    } else {
        new_config &= ~(1U << pin);
    }

    if (new_config == s_exp.config_cache) return ESP_OK;  /* no change */

    uint8_t reg = (pin < 8) ? REG_CONFIG_PORT0 : REG_CONFIG_PORT1;
    uint8_t val = (uint8_t)(new_config >> ((pin < 8) ? 0 : 8));
    esp_err_t err = write_reg(reg, val);
    if (err != ESP_OK) return err;

    s_exp.config_cache = new_config;
    return ESP_OK;
}

esp_err_t harness_io_expander_write_pin(uint8_t pin, bool level)
{
    if (pin >= IO_EXPANDER_PIN_COUNT) return ESP_ERR_INVALID_ARG;
    if (!s_exp.initialized) return ESP_ERR_INVALID_STATE;

    uint16_t new_output = s_exp.output_cache;
    if (level) {
        new_output |=  (1U << pin);
    } else {
        new_output &= ~(1U << pin);
    }

    if (new_output == s_exp.output_cache) return ESP_OK;

    uint8_t reg = (pin < 8) ? REG_OUTPUT_PORT0 : REG_OUTPUT_PORT1;
    uint8_t val = (uint8_t)(new_output >> ((pin < 8) ? 0 : 8));
    esp_err_t err = write_reg(reg, val);
    if (err != ESP_OK) return err;

    s_exp.output_cache = new_output;
    return ESP_OK;
}

int harness_io_expander_read_pin(uint8_t pin)
{
    if (pin >= IO_EXPANDER_PIN_COUNT || !s_exp.initialized) return -1;

    uint8_t reg = (pin < 8) ? REG_INPUT_PORT0 : REG_INPUT_PORT1;
    uint8_t val;
    esp_err_t err = read_reg(reg, &val);
    if (err != ESP_OK) return -1;

    return (val >> (pin & 7)) & 1;
}

esp_err_t harness_io_expander_toggle_pin(uint8_t pin)
{
    if (pin >= IO_EXPANDER_PIN_COUNT) return ESP_ERR_INVALID_ARG;
    if (!s_exp.initialized) return ESP_ERR_INVALID_STATE;

    int cur = harness_io_expander_read_pin(pin);
    if (cur < 0) return ESP_FAIL;

    /* For output pins, read from the output cache; for input, read live.
     * Toggle the cached output value and write it. */
    uint16_t bit = 1U << pin;
    uint16_t new_output = s_exp.output_cache ^ bit;

    uint8_t reg = (pin < 8) ? REG_OUTPUT_PORT0 : REG_OUTPUT_PORT1;
    uint8_t val = (uint8_t)(new_output >> ((pin < 8) ? 0 : 8));
    esp_err_t err = write_reg(reg, val);
    if (err != ESP_OK) return err;

    s_exp.output_cache = new_output;
    return ESP_OK;
}

esp_err_t harness_io_expander_read_all(uint16_t *values)
{
    if (!values) return ESP_ERR_INVALID_ARG;
    if (!s_exp.initialized) return ESP_ERR_INVALID_STATE;

    uint8_t port0, port1;
    esp_err_t err = read_reg(REG_INPUT_PORT0, &port0);
    if (err != ESP_OK) return err;
    err = read_reg(REG_INPUT_PORT1, &port1);
    if (err != ESP_OK) return err;

    *values = (uint16_t)port0 | ((uint16_t)port1 << 8);
    return ESP_OK;
}

esp_err_t harness_io_expander_write_all(uint16_t values)
{
    if (!s_exp.initialized) return ESP_ERR_INVALID_STATE;

    uint8_t buf[3] = { REG_OUTPUT_PORT0, (uint8_t)(values & 0xFF), (uint8_t)(values >> 8) };
    esp_err_t err = i2c_master_transmit(s_exp.dev, buf, 3, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    s_exp.output_cache = values;
    return ESP_OK;
}

esp_err_t harness_io_expander_get_dir_all(uint16_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    if (!s_exp.initialized) return ESP_ERR_INVALID_STATE;

    uint8_t cfg0, cfg1;
    esp_err_t err = read_reg(REG_CONFIG_PORT0, &cfg0);
    if (err != ESP_OK) return err;
    err = read_reg(REG_CONFIG_PORT1, &cfg1);
    if (err != ESP_OK) return err;

    *config = (uint16_t)cfg0 | ((uint16_t)cfg1 << 8);
    s_exp.config_cache = *config;
    return ESP_OK;
}

int harness_io_expander_get_count(void)
{
    return s_exp.initialized ? 1 : 0;
}
