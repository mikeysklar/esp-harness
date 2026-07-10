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

/* ---- Per-expander state ---- */
typedef struct {
    bool     present;
    uint8_t  addr;
    i2c_master_dev_handle_t dev;
    uint16_t output_cache;   /* shadow of output port registers */
    uint16_t config_cache;   /* shadow of config port registers */
} expander_t;

/* ---- Static state ---- */
static struct {
    bool       initialized;
    int        sda;
    int        scl;
    int        count;            /* number of expanders (0..IO_EXPANDER_MAX_COUNT) */
    i2c_master_bus_handle_t bus;
    expander_t ex[IO_EXPANDER_MAX_COUNT];
} s_state;

/* ---- Low-level I2C register access ---- */

static esp_err_t read_reg(expander_t *e, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(e->dev, &reg, 1, val, 1, I2C_TIMEOUT_MS);
}

static esp_err_t write_reg(expander_t *e, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(e->dev, buf, 2, I2C_TIMEOUT_MS);
}

/* ---- I2C scan ---- */

esp_err_t harness_io_expander_i2c_scan(int sda, int scl, uint32_t clk_hz)
{
    i2c_master_bus_handle_t scan_bus = NULL;

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port   = I2C_NUM_1,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &scan_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed for scan: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "--- I2C bus scan (SDA=%d, SCL=%d) ---", sda, scl);
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = addr,
            .scl_speed_hz    = clk_hz,
        };
        i2c_master_dev_handle_t tmp_dev;
        err = i2c_master_bus_add_device(scan_bus, &dev_cfg, &tmp_dev);
        if (err != ESP_OK) continue;

        /* Probe: try to read one byte */
        uint8_t dummy;
        err = i2c_master_transmit_receive(tmp_dev, (uint8_t[]){0x00}, 1,
                                          &dummy, 1, I2C_TIMEOUT_MS);
        i2c_master_bus_rm_device(tmp_dev);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "  I2C device found at 0x%02x", addr);
            found++;
        }
    }
    ESP_LOGI(TAG, "--- I2C scan complete: %d device(s) found ---", found);

    i2c_del_master_bus(scan_bus);
    return ESP_OK;
}

/* ---- Public API ---- */

esp_err_t harness_io_expander_init(int sda, int scl,
                                   const uint8_t *i2c_addrs, int count,
                                   uint32_t i2c_clk_hz)
{
    if (count < 1 || count > IO_EXPANDER_MAX_COUNT) {
        ESP_LOGE(TAG, "invalid expander count %d (must be 1..%d)",
                 count, IO_EXPANDER_MAX_COUNT);
        return ESP_ERR_INVALID_ARG;
    }

    if (s_state.initialized) {
        ESP_LOGW(TAG, "already initialized, deinitializing first");
        harness_io_expander_deinit();
    }

    memset(&s_state, 0, sizeof(s_state));
    s_state.sda   = sda;
    s_state.scl   = scl;
    s_state.count = count;

    /* Create I2C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port   = I2C_NUM_1,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_state.bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Add each expander */
    for (int i = 0; i < count; i++) {
        expander_t *e = &s_state.ex[i];
        e->addr         = i2c_addrs[i];
        e->output_cache = 0x0000;
        e->config_cache = 0xFFFF;  /* default: all inputs */

        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = e->addr,
            .scl_speed_hz    = i2c_clk_hz,
        };
        err = i2c_master_bus_add_device(s_state.bus, &dev_cfg, &e->dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c_master_bus_add_device(0x%02x) failed: %s",
                     e->addr, esp_err_to_name(err));
            /* Tear down previous devices */
            for (int j = 0; j < i; j++) {
                i2c_master_bus_rm_device(s_state.ex[j].dev);
            }
            i2c_del_master_bus(s_state.bus);
            s_state.bus = NULL;
            return err;
        }

        /* Probe to verify the expander is alive */
        uint8_t dummy;
        err = i2c_master_transmit_receive(e->dev, (uint8_t[]){REG_INPUT_PORT0}, 1,
                                          &dummy, 1, I2C_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "expander %d at 0x%02x not responding: %s",
                     i, e->addr, esp_err_to_name(err));
            i2c_master_bus_rm_device(e->dev);
            e->dev = NULL;
            /* Tear down previous devices */
            for (int j = 0; j < i; j++) {
                i2c_master_bus_rm_device(s_state.ex[j].dev);
            }
            i2c_del_master_bus(s_state.bus);
            s_state.bus = NULL;
            return err;
        }

        /* Read back current config and output registers to sync caches */
        {
            uint8_t cfg0, cfg1, out0, out1;
            if (read_reg(e, REG_CONFIG_PORT0, &cfg0)  == ESP_OK &&
                read_reg(e, REG_CONFIG_PORT1, &cfg1)  == ESP_OK &&
                read_reg(e, REG_OUTPUT_PORT0, &out0) == ESP_OK &&
                read_reg(e, REG_OUTPUT_PORT1, &out1) == ESP_OK) {
                e->config_cache = (uint16_t)cfg0 | ((uint16_t)cfg1 << 8);
                e->output_cache = (uint16_t)out0 | ((uint16_t)out1 << 8);
            }
        }

        e->present = true;
        ESP_LOGI(TAG, "PI4IOE5V9535[%d] at 0x%02x (SDA=%d, SCL=%d, %lu Hz)",
                 i, e->addr, sda, scl, (unsigned long)i2c_clk_hz);
    }

    s_state.initialized = true;
    return ESP_OK;
}

void harness_io_expander_deinit(void)
{
    if (!s_state.initialized) return;
    for (int i = 0; i < s_state.count; i++) {
        if (s_state.ex[i].dev) {
            i2c_master_bus_rm_device(s_state.ex[i].dev);
        }
    }
    if (s_state.bus) i2c_del_master_bus(s_state.bus);
    memset(&s_state, 0, sizeof(s_state));
    ESP_LOGI(TAG, "deinitialized");
}

bool harness_io_expander_is_initialized(void)
{
    return s_state.initialized;
}

int harness_io_expander_get_count(void)
{
    return s_state.initialized ? s_state.count : 0;
}

/* ---- Internal: validate expander index ---- */
static inline expander_t *expander_from_idx(int exp_idx)
{
    if (!s_state.initialized || exp_idx < 0 || exp_idx >= s_state.count)
        return NULL;
    expander_t *e = &s_state.ex[exp_idx];
    return e->present ? e : NULL;
}

/* ---- Pin-level API ---- */

esp_err_t harness_io_expander_set_dir(int exp_idx, uint8_t pin, bool input)
{
    if (pin >= IO_EXPANDER_PIN_COUNT) return ESP_ERR_INVALID_ARG;
    expander_t *e = expander_from_idx(exp_idx);
    if (!e) return ESP_ERR_INVALID_STATE;

    uint16_t new_config = e->config_cache;
    if (input) {
        new_config |=  (1U << pin);
    } else {
        new_config &= ~(1U << pin);
    }

    if (new_config == e->config_cache) return ESP_OK;

    uint8_t reg = (pin < 8) ? REG_CONFIG_PORT0 : REG_CONFIG_PORT1;
    uint8_t val = (uint8_t)(new_config >> ((pin < 8) ? 0 : 8));
    esp_err_t err = write_reg(e, reg, val);
    if (err != ESP_OK) return err;

    e->config_cache = new_config;
    return ESP_OK;
}

esp_err_t harness_io_expander_write_pin(int exp_idx, uint8_t pin, bool level)
{
    if (pin >= IO_EXPANDER_PIN_COUNT) return ESP_ERR_INVALID_ARG;
    expander_t *e = expander_from_idx(exp_idx);
    if (!e) return ESP_ERR_INVALID_STATE;

    uint16_t new_output = e->output_cache;
    if (level) {
        new_output |=  (1U << pin);
    } else {
        new_output &= ~(1U << pin);
    }

    if (new_output == e->output_cache) return ESP_OK;

    uint8_t reg = (pin < 8) ? REG_OUTPUT_PORT0 : REG_OUTPUT_PORT1;
    uint8_t val = (uint8_t)(new_output >> ((pin < 8) ? 0 : 8));
    esp_err_t err = write_reg(e, reg, val);
    if (err != ESP_OK) return err;

    e->output_cache = new_output;
    return ESP_OK;
}

int harness_io_expander_read_pin(int exp_idx, uint8_t pin)
{
    if (pin >= IO_EXPANDER_PIN_COUNT) return -1;
    expander_t *e = expander_from_idx(exp_idx);
    if (!e) return -1;

    uint8_t reg = (pin < 8) ? REG_INPUT_PORT0 : REG_INPUT_PORT1;
    uint8_t val;
    esp_err_t err = read_reg(e, reg, &val);
    if (err != ESP_OK) return -1;

    return (val >> (pin & 7)) & 1;
}

esp_err_t harness_io_expander_toggle_pin(int exp_idx, uint8_t pin)
{
    if (pin >= IO_EXPANDER_PIN_COUNT) return ESP_ERR_INVALID_ARG;
    expander_t *e = expander_from_idx(exp_idx);
    if (!e) return ESP_ERR_INVALID_STATE;

    uint16_t bit = 1U << pin;
    uint16_t new_output = e->output_cache ^ bit;

    uint8_t reg = (pin < 8) ? REG_OUTPUT_PORT0 : REG_OUTPUT_PORT1;
    uint8_t val = (uint8_t)(new_output >> ((pin < 8) ? 0 : 8));
    esp_err_t err = write_reg(e, reg, val);
    if (err != ESP_OK) return err;

    e->output_cache = new_output;
    return ESP_OK;
}

/* ---- Bulk API ---- */

esp_err_t harness_io_expander_read_all(int exp_idx, uint16_t *values)
{
    if (!values) return ESP_ERR_INVALID_ARG;
    expander_t *e = expander_from_idx(exp_idx);
    if (!e) return ESP_ERR_INVALID_STATE;

    uint8_t port0, port1;
    esp_err_t err = read_reg(e, REG_INPUT_PORT0, &port0);
    if (err != ESP_OK) return err;
    err = read_reg(e, REG_INPUT_PORT1, &port1);
    if (err != ESP_OK) return err;

    *values = (uint16_t)port0 | ((uint16_t)port1 << 8);
    return ESP_OK;
}

esp_err_t harness_io_expander_write_all(int exp_idx, uint16_t values)
{
    expander_t *e = expander_from_idx(exp_idx);
    if (!e) return ESP_ERR_INVALID_STATE;

    uint8_t buf[3] = { REG_OUTPUT_PORT0, (uint8_t)(values & 0xFF), (uint8_t)(values >> 8) };
    esp_err_t err = i2c_master_transmit(e->dev, buf, 3, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    e->output_cache = values;
    return ESP_OK;
}

esp_err_t harness_io_expander_get_dir_all(int exp_idx, uint16_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    expander_t *e = expander_from_idx(exp_idx);
    if (!e) return ESP_ERR_INVALID_STATE;

    uint8_t cfg0, cfg1;
    esp_err_t err = read_reg(e, REG_CONFIG_PORT0, &cfg0);
    if (err != ESP_OK) return err;
    err = read_reg(e, REG_CONFIG_PORT1, &cfg1);
    if (err != ESP_OK) return err;

    *config = (uint16_t)cfg0 | ((uint16_t)cfg1 << 8);
    e->config_cache = *config;
    return ESP_OK;
}
