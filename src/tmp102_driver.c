/**
 * @file tmp102_driver.c
 * @brief Driver core implementation for TMP102 digital temperature sensor.
 *
 * Implements sensor initialization via Dependency Injection (HAL),
 * temperature reading with 12-bit two's complement conversion, and
 * configuration register read/write operations.
 */

#include "tmp102_driver.h"

/* ========================================================================= */
/* Initialization & Lifecycle API                                            */
/* ========================================================================= */

/**
 * @brief Initialize a TMP102 driver instance via Dependency Injection.
 *
 * Validates all input parameters and stores the HAL function table inside
 * the driver context. After a successful call the driver is ready to
 * communicate with the sensor over I2C.
 *
 * @param[out] dev          Caller-allocated driver context to initialize.
 * @param[in]  i2c_address  7-bit I2C slave address (0x48 – 0x4B).
 * @param[in]  hal          HAL function table; i2c_write_read and
 *                          i2c_write must be non-NULL.
 *
 * @return TMP102_OK              on success.
 * @return TMP102_ERR_NULL_PTR    if @p dev, @p hal, or a mandatory HAL
 *                                function pointer is NULL.
 * @return TMP102_ERR_INVALID_PARAM if @p i2c_address is outside the
 *                                valid range (0x48 – 0x4B).
 *
 * @note The function performs a shallow copy of @p hal, so the caller
 *       does not need to keep the original struct alive after the call.
 */
tmp102_status_t tmp102_init(tmp102_driver_t *dev, uint8_t i2c_address,
                            const tmp102_hal_funcs_t *hal)
{
    /* Validate pointers defensively */
    if (dev == NULL || hal == NULL) {
        return TMP102_ERR_NULL_PTR;
    }

    /* Validate mandatory HAL function pointers */
    if (hal->i2c_write_read == NULL || hal->i2c_write == NULL) {
        return TMP102_ERR_NULL_PTR;
    }

    /* Validate 7-bit I2C address range for TMP102 (0x48 - 0x4B) */
    if (i2c_address < TMP102_I2C_ADDR_GND || i2c_address > TMP102_I2C_ADDR_SCL) {
        return TMP102_ERR_INVALID_PARAM;
    }

    /* Store configuration and inject HAL functions */
    dev->i2c_address = i2c_address;
    dev->hal = *hal;
    dev->initialized = true;

    return TMP102_OK;
}

/* ========================================================================= */
/* Temperature Reading API                                                   */
/* ========================================================================= */

/**
 * @brief Read the current temperature from the TMP102 sensor.
 *
 * The function first reads the Configuration Register to determine
 * whether the sensor is in Normal (12-bit) or Extended (13-bit) mode,
 * then reads the Temperature Register and converts the raw two's
 * complement value to degrees Celsius.
 *
 * @param[in]  dev       Pointer to an initialized driver context.
 * @param[out] temp_out  Receives the temperature in °C
 *                       (resolution 0.0625 °C / LSB).
 *
 * @return TMP102_OK                on success.
 * @return TMP102_ERR_NULL_PTR      if @p dev or @p temp_out is NULL.
 * @return TMP102_ERR_NOT_INITIALIZED if the driver has not been initialized.
 * @return TMP102_ERR_I2C           if an I2C transaction fails.
 */
tmp102_status_t tmp102_get_temperature_celsius(tmp102_driver_t *dev,
                                                float *temp_out)
{
    /* Validate input arguments defensively */
    if (dev == NULL || temp_out == NULL) {
        return TMP102_ERR_NULL_PTR;
    }

    /* Guard against uninitialized driver instance */
    if (!dev->initialized) {
        return TMP102_ERR_NOT_INITIALIZED;
    }

    /*
     * Read the Configuration Register to detect Extended Mode (EM) bit.
     * EM=0: Normal 12-bit mode, EM=1: Extended 13-bit mode.
     */
    uint16_t config = 0;
    tmp102_status_t cfg_status = tmp102_read_config(dev, &config);
    if (cfg_status != TMP102_OK) {
        return cfg_status;
    }

    /* Write pointer register for Temperature (0x00) and read 2 bytes */
    uint8_t tx_buf[1] = { TMP102_REG_TEMP };
    uint8_t rx_buf[2] = { 0, 0 };

    int ret = dev->hal.i2c_write_read(dev->hal.user_data, dev->i2c_address,
                                      tx_buf, sizeof(tx_buf),
                                      rx_buf, sizeof(rx_buf));
    if (ret != 1) {
        return TMP102_ERR_I2C;
    }

    /*
     * Combine received bytes into 16-bit word (Big-Endian):
     * rx_buf[0] is MSB (bits 15..8), rx_buf[1] is LSB (bits 7..0).
     */
    uint16_t raw = (uint16_t)(((uint16_t)rx_buf[0] << 8) | (uint16_t)rx_buf[1]);

    int16_t count;

    if ((config & TMP102_CONFIG_EM_MASK) != 0U) {
        /*
         * Extended Mode (13-bit): data is left-justified in bits 15..3.
         * Shift right by 3 bits to obtain 13-bit count.
         */
        count = (int16_t)(raw >> 3);

        /* Sign-extend if negative (bit 12 is sign bit in 13-bit mode) */
        if ((count & 0x1000) != 0) {
            count = (int16_t)(count | (int16_t)0xE000);
        }
    } else {
        /*
         * Normal Mode (12-bit): data is left-justified in bits 15..4.
         * Shift right by 4 bits to obtain 12-bit count.
         */
        count = (int16_t)(raw >> 4);

        /* Sign-extend if negative (bit 11 is sign bit in 12-bit mode) */
        if ((count & 0x0800) != 0) {
            count = (int16_t)(count | (int16_t)0xF000);
        }
    }

    /* Multiply count by temperature resolution (0.0625°C / LSB) */
    *temp_out = (float)count * TMP102_TEMP_RESOLUTION;

    return TMP102_OK;
}

/* ========================================================================= */
/* Configuration Register Access API                                         */
/* ========================================================================= */

/**
 * @brief Read the 16-bit Configuration Register from the TMP102.
 *
 * Sends the Configuration Register pointer (0x01) and reads the two
 * result bytes in Big-Endian order.
 *
 * @param[in]  dev         Pointer to an initialized driver context.
 * @param[out] config_out  Receives the raw 16-bit config register value.
 *
 * @return TMP102_OK                on success.
 * @return TMP102_ERR_NULL_PTR      if @p dev or @p config_out is NULL.
 * @return TMP102_ERR_NOT_INITIALIZED if the driver has not been initialized.
 * @return TMP102_ERR_I2C           if the I2C transaction fails.
 */
tmp102_status_t tmp102_read_config(tmp102_driver_t *dev, uint16_t *config_out)
{
    /* Validate input arguments defensively */
    if (dev == NULL || config_out == NULL) {
        return TMP102_ERR_NULL_PTR;
    }

    /* Guard against uninitialized driver instance */
    if (!dev->initialized) {
        return TMP102_ERR_NOT_INITIALIZED;
    }

    /* Write pointer register for Configuration (0x01) and read 2 bytes */
    uint8_t tx_buf[1] = { TMP102_REG_CONFIG };
    uint8_t rx_buf[2] = { 0, 0 };

    int ret = dev->hal.i2c_write_read(dev->hal.user_data, dev->i2c_address,
                                      tx_buf, sizeof(tx_buf),
                                      rx_buf, sizeof(rx_buf));
    if (ret != 1) {
        return TMP102_ERR_I2C;
    }

    /* Combine received bytes into 16-bit word (Big-Endian) */
    *config_out = (uint16_t)(((uint16_t)rx_buf[0] << 8) | (uint16_t)rx_buf[1]);

    return TMP102_OK;
}

/**
 * @brief Write a 16-bit value to the TMP102 Configuration Register.
 *
 * Constructs a 3-byte I2C write payload: register pointer (0x01)
 * followed by the MSB and LSB of @p config.
 *
 * @param[in] dev     Pointer to an initialized driver context.
 * @param[in] config  The 16-bit value to write.
 *
 * @return TMP102_OK                on success.
 * @return TMP102_ERR_NULL_PTR      if @p dev is NULL.
 * @return TMP102_ERR_NOT_INITIALIZED if the driver has not been initialized.
 * @return TMP102_ERR_I2C           if the I2C transaction fails.
 */
tmp102_status_t tmp102_write_config(tmp102_driver_t *dev, uint16_t config)
{
    /* Validate input arguments defensively */
    if (dev == NULL) {
        return TMP102_ERR_NULL_PTR;
    }

    /* Guard against uninitialized driver instance */
    if (!dev->initialized) {
        return TMP102_ERR_NOT_INITIALIZED;
    }

    /*
     * Prepare 3-byte I2C write payload:
     * tx[0]: Register pointer (0x01 for Configuration)
     * tx[1]: MSB (bits 15..8)
     * tx[2]: LSB (bits 7..0)
     */
    uint8_t tx_buf[3];
    tx_buf[0] = TMP102_REG_CONFIG;
    tx_buf[1] = (uint8_t)((config >> 8) & 0xFFU);
    tx_buf[2] = (uint8_t)(config & 0xFFU);

    int ret = dev->hal.i2c_write(dev->hal.user_data, dev->i2c_address,
                                 tx_buf, sizeof(tx_buf));
    if (ret != 1) {
        return TMP102_ERR_I2C;
    }

    return TMP102_OK;
}

/* ========================================================================= */
/* Configuration Bit-Field Operations API                                     */
/* ========================================================================= */

/**
 * @brief Set (OR) specific bits in the Configuration Register.
 *
 * Performs a read-modify-write cycle: reads the current value, ORs
 * in @p mask, and writes the result back.
 *
 * @param[in] dev   Pointer to an initialized driver context.
 * @param[in] mask  Bitmask of bits to set
 *                  (e.g. TMP102_CONFIG_SD_MASK).
 *
 * @return TMP102_OK on success, or the first error encountered during
 *         the read-modify-write sequence.
 */
tmp102_status_t tmp102_set_config_bits(tmp102_driver_t *dev, uint16_t mask)
{
    /* Read current configuration */
    uint16_t config = 0;
    tmp102_status_t status = tmp102_read_config(dev, &config);
    if (status != TMP102_OK) {
        return status;
    }

    /* Set specified bits and write back */
    config |= mask;
    return tmp102_write_config(dev, config);
}

/**
 * @brief Clear (AND ~mask) specific bits in the Configuration Register.
 *
 * Performs a read-modify-write cycle: reads the current value, ANDs
 * with the inverse of @p mask, and writes the result back.
 *
 * @param[in] dev   Pointer to an initialized driver context.
 * @param[in] mask  Bitmask of bits to clear
 *                  (e.g. TMP102_CONFIG_SD_MASK).
 *
 * @return TMP102_OK on success, or the first error encountered during
 *         the read-modify-write sequence.
 */
tmp102_status_t tmp102_clear_config_bits(tmp102_driver_t *dev, uint16_t mask)
{
    /* Read current configuration */
    uint16_t config = 0;
    tmp102_status_t status = tmp102_read_config(dev, &config);
    if (status != TMP102_OK) {
        return status;
    }

    /* Clear specified bits and write back */
    config &= (uint16_t)(~mask);
    return tmp102_write_config(dev, config);
}

/**
 * @brief Set the Fault Queue count (F1:F0 field) in the Configuration Register.
 *
 * Reads the current configuration, clears the F1:F0 bits, inserts
 * the two LSBs of @p value, and writes the result back.
 *
 * @param[in] dev    Pointer to an initialized driver context.
 * @param[in] value  Fault queue selector (0–3).
 *                   Use TMP102_FAULT_QUEUE_1 / _2 / _4 / _6.
 *
 * @return TMP102_OK on success, or the first error encountered during
 *         the read-modify-write sequence.
 */
tmp102_status_t tmp102_set_fault_queue(tmp102_driver_t *dev, uint8_t value)
{
    /* Read current configuration */
    uint16_t config = 0;
    tmp102_status_t status = tmp102_read_config(dev, &config);
    if (status != TMP102_OK) {
        return status;
    }

    /* Clear the F1:F0 field, then set the new value */
    config &= (uint16_t)(~TMP102_CONFIG_F1_F0_MASK);
    config |= (uint16_t)(((uint16_t)value & 0x03U) << TMP102_CONFIG_F1_F0_SHIFT);
    return tmp102_write_config(dev, config);
}

/**
 * @brief Set the Conversion Rate (CR1:CR0 field) in the Configuration Register.
 *
 * Reads the current configuration, clears the CR1:CR0 bits, inserts
 * the two LSBs of @p value, and writes the result back.
 *
 * @param[in] dev    Pointer to an initialized driver context.
 * @param[in] value  Conversion rate selector (0–3).
 *                   Use TMP102_CONV_RATE_0_25HZ / _1HZ / _4HZ / _8HZ.
 *
 * @return TMP102_OK on success, or the first error encountered during
 *         the read-modify-write sequence.
 */
tmp102_status_t tmp102_set_conversion_rate(tmp102_driver_t *dev, uint8_t value)
{
    /* Read current configuration */
    uint16_t config = 0;
    tmp102_status_t status = tmp102_read_config(dev, &config);
    if (status != TMP102_OK) {
        return status;
    }

    /* Clear the CR1:CR0 field, then set the new value */
    config &= (uint16_t)(~TMP102_CONFIG_CR1_CR0_MASK);
    config |= (uint16_t)(((uint16_t)value & 0x03U) << TMP102_CONFIG_CR1_CR0_SHIFT);
    return tmp102_write_config(dev, config);
}

/**
 * @brief Trigger a single temperature conversion (One-Shot mode).
 *
 * Sets the OS bit in the Configuration Register. This is only
 * meaningful when the sensor is in Shutdown mode (SD = 1); the
 * hardware clears the OS bit automatically once the conversion
 * completes.
 *
 * @param[in] dev  Pointer to an initialized driver context.
 *
 * @return TMP102_OK on success, or the first error encountered.
 */
tmp102_status_t tmp102_trigger_one_shot(tmp102_driver_t *dev)
{
    /* Set the OS bit to trigger a single conversion in Shutdown mode */
    return tmp102_set_config_bits(dev, TMP102_CONFIG_OS_MASK);
}

/* ========================================================================= */
/* Alert Threshold Registers API                                              */
/* ========================================================================= */

/**
 * @brief Write a temperature threshold to a 12-bit alert register.
 *
 * Converts a floating-point temperature (°C) to a 12-bit two's
 * complement count (0.0625 °C / LSB), left-justifies it into the
 * 16-bit Big-Endian register format, and sends a 3-byte I2C write
 * to the specified register.
 *
 * @param[in] dev          Pointer to an initialized driver context.
 * @param[in] reg_ptr      Register pointer byte
 *                         (TMP102_REG_T_LOW or TMP102_REG_T_HIGH).
 * @param[in] temp_celsius Threshold temperature in °C.
 *
 * @return TMP102_OK                on success.
 * @return TMP102_ERR_NULL_PTR      if @p dev is NULL.
 * @return TMP102_ERR_NOT_INITIALIZED if the driver has not been initialized.
 * @return TMP102_ERR_I2C           if the I2C transaction fails.
 */
static tmp102_status_t write_threshold_register(tmp102_driver_t *dev,
                                                uint8_t reg_ptr,
                                                float temp_celsius)
{
    /* Validate input arguments defensively */
    if (dev == NULL) {
        return TMP102_ERR_NULL_PTR;
    }

    /* Guard against uninitialized driver instance */
    if (!dev->initialized) {
        return TMP102_ERR_NOT_INITIALIZED;
    }

    /*
     * Convert float temperature to 12-bit two's complement count.
     * Resolution: 0.0625°C per LSB. Round symmetrically.
     */
    float counts_f = temp_celsius / TMP102_TEMP_RESOLUTION;
    int16_t counts = (counts_f >= 0.0f) ? (int16_t)(counts_f + 0.5f)
                                        : (int16_t)(counts_f - 0.5f);

    /*
     * Left-justify 12-bit count into 16-bit register (bits 15..4, bits 3..0=0).
     * Mask to 16-bit to preserve two's complement encoding.
     */
    uint16_t reg_val = (uint16_t)(((uint16_t)counts << 4) & 0xFFF0U);

    /*
     * Prepare 3-byte I2C write payload:
     * tx[0]: Register pointer (0x02 or 0x03)
     * tx[1]: MSB (bits 15..8)
     * tx[2]: LSB (bits 7..0)
     */
    uint8_t tx_buf[3];
    tx_buf[0] = reg_ptr;
    tx_buf[1] = (uint8_t)((reg_val >> 8) & 0xFFU);
    tx_buf[2] = (uint8_t)(reg_val & 0xFFU);

    int ret = dev->hal.i2c_write(dev->hal.user_data, dev->i2c_address,
                                 tx_buf, sizeof(tx_buf));
    if (ret != 1) {
        return TMP102_ERR_I2C;
    }

    return TMP102_OK;
}

/**
 * @brief Read a temperature threshold from a 12-bit alert register.
 *
 * Sends the register pointer byte, reads the 16-bit Big-Endian
 * value, extracts the 12-bit two's complement count from
 * bits [15:4], sign-extends it, and converts to °C.
 *
 * @param[in]  dev       Pointer to an initialized driver context.
 * @param[in]  reg_ptr   Register pointer byte
 *                       (TMP102_REG_T_LOW or TMP102_REG_T_HIGH).
 * @param[out] temp_out  Receives the threshold temperature in °C.
 *
 * @return TMP102_OK                on success.
 * @return TMP102_ERR_NULL_PTR      if @p dev or @p temp_out is NULL.
 * @return TMP102_ERR_NOT_INITIALIZED if the driver has not been initialized.
 * @return TMP102_ERR_I2C           if the I2C transaction fails.
 */
static tmp102_status_t read_threshold_register(tmp102_driver_t *dev,
                                               uint8_t reg_ptr,
                                               float *temp_out)
{
    /* Validate input arguments defensively */
    if (dev == NULL || temp_out == NULL) {
        return TMP102_ERR_NULL_PTR;
    }

    /* Guard against uninitialized driver instance */
    if (!dev->initialized) {
        return TMP102_ERR_NOT_INITIALIZED;
    }

    /* Write pointer register and read 2 bytes */
    uint8_t tx_buf[1] = { reg_ptr };
    uint8_t rx_buf[2] = { 0, 0 };

    int ret = dev->hal.i2c_write_read(dev->hal.user_data, dev->i2c_address,
                                      tx_buf, sizeof(tx_buf),
                                      rx_buf, sizeof(rx_buf));
    if (ret != 1) {
        return TMP102_ERR_I2C;
    }

    /* Combine received bytes into 16-bit word (Big-Endian) */
    uint16_t raw = (uint16_t)(((uint16_t)rx_buf[0] << 8) | (uint16_t)rx_buf[1]);

    /* Extract 12-bit count: right-shift by 4 to remove padding bits */
    int16_t count = (int16_t)(raw >> 4);

    /* Sign-extend if negative (bit 11 is sign bit in 12-bit mode) */
    if ((count & 0x0800) != 0) {
        count = (int16_t)(count | (int16_t)0xF000);
    }

    /* Convert to Celsius: multiply by resolution (0.0625°C / LSB) */
    *temp_out = (float)count * TMP102_TEMP_RESOLUTION;

    return TMP102_OK;
}

/**
 * @brief Set the T_LOW alert threshold in °C.
 *
 * @param[in] dev           Pointer to an initialized driver context.
 * @param[in] temp_celsius  Threshold temperature in °C.
 *
 * @return TMP102_OK on success, or an error from write_threshold_register().
 */
tmp102_status_t tmp102_set_t_low(tmp102_driver_t *dev, float temp_celsius)
{
    return write_threshold_register(dev, TMP102_REG_T_LOW, temp_celsius);
}

/**
 * @brief Set the T_HIGH alert threshold in °C.
 *
 * @param[in] dev           Pointer to an initialized driver context.
 * @param[in] temp_celsius  Threshold temperature in °C.
 *
 * @return TMP102_OK on success, or an error from write_threshold_register().
 */
tmp102_status_t tmp102_set_t_high(tmp102_driver_t *dev, float temp_celsius)
{
    return write_threshold_register(dev, TMP102_REG_T_HIGH, temp_celsius);
}

/**
 * @brief Read the current T_LOW alert threshold in °C.
 *
 * @param[in]  dev       Pointer to an initialized driver context.
 * @param[out] temp_out  Receives the threshold temperature in °C.
 *
 * @return TMP102_OK on success, or an error from read_threshold_register().
 */
tmp102_status_t tmp102_get_t_low(tmp102_driver_t *dev, float *temp_out)
{
    return read_threshold_register(dev, TMP102_REG_T_LOW, temp_out);
}

/**
 * @brief Read the current T_HIGH alert threshold in °C.
 *
 * @param[in]  dev       Pointer to an initialized driver context.
 * @param[out] temp_out  Receives the threshold temperature in °C.
 *
 * @return TMP102_OK on success, or an error from read_threshold_register().
 */
tmp102_status_t tmp102_get_t_high(tmp102_driver_t *dev, float *temp_out)
{
    return read_threshold_register(dev, TMP102_REG_T_HIGH, temp_out);
}
