#pragma once

/**
 * @file harness_io_expander.h
 * @brief Driver for the PI4IOE5V9535ZDEX 16-bit I2C GPIO expander.
 *
 * Registers (identical to PCA9535):
 *   0x00  Input Port 0       (read-only)  — IO0_0–IO0_7
 *   0x01  Input Port 1       (read-only)  — IO1_0–IO1_7
 *   0x02  Output Port 0      (read/write)
 *   0x03  Output Port 1      (read/write)
 *   0x04  Polarity Inversion Port 0 (read/write)  — 1 = inverted
 *   0x05  Polarity Inversion Port 1 (read/write)
 *   0x06  Configuration Port 0 (read/write) — 1 = input, 0 = output
 *   0x07  Configuration Port 1 (read/write)
 *
 * Supports up to IO_EXPANDER_MAX_COUNT expanders on a single I2C bus.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of pins per PI4IOE5V9535. */
#define IO_EXPANDER_PIN_COUNT  16

/** Maximum number of expanders supported on one bus. */
#define IO_EXPANDER_MAX_COUNT  3

/** I2C address when A2=A1=A0=0 (default). */
#define IO_EXPANDER_DEFAULT_ADDR  0x20

/**
 * @brief Scan the I2C bus and log all responding addresses.
 *
 * Must be called after the I2C bus is created but before any expander
 * devices are added.  Probes addresses 0x08–0x77 and logs found devices.
 *
 * @param sda  GPIO for SDA
 * @param scl  GPIO for SCL
 * @param clk_hz  I2C clock speed
 * @return ESP_OK on success (bus is torn down after scan)
 */
esp_err_t harness_io_expander_i2c_scan(int sda, int scl, uint32_t clk_hz);

/**
 * @brief Initialise up to IO_EXPANDER_MAX_COUNT IO expanders on one bus.
 *
 * Creates an I2C master bus on (sda, scl), then probes and adds each
 * address in i2c_addrs[0..count-1].  Expanders are indexed 0..count-1
 * in the same order as i2c_addrs[].
 *
 * @param sda           GPIO number for I2C SDA
 * @param scl           GPIO number for I2C SCL
 * @param i2c_addrs     Array of 7-bit I2C addresses (e.g. {0x20, 0x21, 0x22})
 * @param count         Number of addresses in i2c_addrs (1..IO_EXPANDER_MAX_COUNT)
 * @param i2c_clk_hz    I2C clock speed in Hz (e.g. 100000)
 * @return ESP_OK on success
 */
esp_err_t harness_io_expander_init(int sda, int scl,
                                   const uint8_t *i2c_addrs, int count,
                                   uint32_t i2c_clk_hz);

/**
 * @brief Check if any expander has been initialised.
 */
bool harness_io_expander_is_initialized(void);

/**
 * @brief Get the number of expanders attached.
 * @return 0 if not initialised, otherwise the count passed to init().
 */
int harness_io_expander_get_count(void);

/**
 * @brief Set the direction of a single pin.
 *
 * @param exp_idx  Expander index (0 .. count-1)
 * @param pin      Pin number (0 .. IO_EXPANDER_PIN_COUNT-1)
 * @param input    true = input, false = output
 */
esp_err_t harness_io_expander_set_dir(int exp_idx, uint8_t pin, bool input);

/**
 * @brief Set the output level of a single pin.
 *
 * @param exp_idx  Expander index (0 .. count-1)
 * @param pin      Pin number (0 .. IO_EXPANDER_PIN_COUNT-1)
 * @param level    true = high, false = low
 */
esp_err_t harness_io_expander_write_pin(int exp_idx, uint8_t pin, bool level);

/**
 * @brief Read the input level of a single pin.
 *
 * @param exp_idx  Expander index (0 .. count-1)
 * @param pin      Pin number (0 .. IO_EXPANDER_PIN_COUNT-1)
 * @return 0 or 1, or -1 on error.
 */
int harness_io_expander_read_pin(int exp_idx, uint8_t pin);

/**
 * @brief Toggle a pin (if it's an output).
 *
 * @param exp_idx  Expander index (0 .. count-1)
 * @param pin      Pin number (0 .. IO_EXPANDER_PIN_COUNT-1)
 */
esp_err_t harness_io_expander_toggle_pin(int exp_idx, uint8_t pin);

/**
 * @brief Read all 16 input pins of one expander at once.
 *
 * @param exp_idx  Expander index (0 .. count-1)
 * @param[out] values  Bitmask of input values.
 * @return ESP_OK on success.
 */
esp_err_t harness_io_expander_read_all(int exp_idx, uint16_t *values);

/**
 * @brief Write all 16 output pins of one expander at once.
 *
 * @param exp_idx  Expander index (0 .. count-1)
 * @param values  Bitmask of output values.
 * @return ESP_OK on success.
 */
esp_err_t harness_io_expander_write_all(int exp_idx, uint16_t values);

/**
 * @brief Read both configuration registers (direction) of one expander.
 *
 * @param exp_idx  Expander index (0 .. count-1)
 * @param[out] config  Bitmask: 1 = input, 0 = output.
 * @return ESP_OK on success.
 */
esp_err_t harness_io_expander_get_dir_all(int exp_idx, uint16_t *config);

/**
 * @brief Deinitialize all expanders and tear down the I2C bus.
 */
void harness_io_expander_deinit(void);

#ifdef __cplusplus
}
#endif
