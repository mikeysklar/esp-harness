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
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of pins on the PI4IOE5V9535. */
#define IO_EXPANDER_PIN_COUNT  16

/** I2C address when A2=A1=A0=0 (default). */
#define IO_EXPANDER_DEFAULT_ADDR  0x20

/**
 * @brief Initialize the IO expander over I2C.
 *
 * @param sda           GPIO number for I2C SDA
 * @param scl           GPIO number for I2C SCL
 * @param i2c_addr      7-bit I2C address (e.g. 0x20)
 * @param i2c_clk_hz    I2C clock speed in Hz (e.g. 100000)
 * @return ESP_OK on success
 */
esp_err_t harness_io_expander_init(int sda, int scl, uint8_t i2c_addr,
                                   uint32_t i2c_clk_hz);

/**
 * @brief Check if the expander has been initialized.
 */
bool harness_io_expander_is_initialized(void);

/**
 * @brief Set the direction of a single pin (0=output, 1=input).
 */
esp_err_t harness_io_expander_set_dir(uint8_t pin, bool input);

/**
 * @brief Set the output level of a single pin.
 */
esp_err_t harness_io_expander_write_pin(uint8_t pin, bool level);

/**
 * @brief Read the input level of a single pin.
 * @return 0 or 1, or -1 on error.
 */
int harness_io_expander_read_pin(uint8_t pin);

/**
 * @brief Toggle a pin (if it's an output).
 */
esp_err_t harness_io_expander_toggle_pin(uint8_t pin);

/**
 * @brief Read all 16 input pins at once.
 * @param[out] values  Bitmask of input values.
 * @return ESP_OK on success.
 */
esp_err_t harness_io_expander_read_all(uint16_t *values);

/**
 * @brief Write all 16 output pins at once.
 * @param values  Bitmask of output values.
 * @return ESP_OK on success.
 */
esp_err_t harness_io_expander_write_all(uint16_t values);

/**
 * @brief Read both configuration registers (direction).
 * @param[out] config  Bitmask: 1 = input, 0 = output.
 * @return ESP_OK on success.
 */
esp_err_t harness_io_expander_get_dir_all(uint16_t *config);

/**
 * @brief Deinitialize the expander and tear down the I2C bus.
 */
void harness_io_expander_deinit(void);

/**
 * @brief Get the number of expanders attached (0 or 1).
 */
int harness_io_expander_get_count(void);

#ifdef __cplusplus
}
#endif
