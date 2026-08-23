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
/* Alert Threshold Registers API                                              */
/* ========================================================================= */

/**
 * @brief Internal helper: write a temperature threshold to a 12-bit register.
 *
 * Converts float °C to 12-bit two's complement count, left-justifies into
 * 16-bit Big-Endian register format, and writes via I2C to the specified
 * register pointer (0x02 for T_LOW, 0x03 for T_HIGH).
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
 * @brief Internal helper: read a temperature threshold from a 12-bit register.
 *
 * Reads 16-bit Big-Endian register from the specified pointer (0x02 for T_LOW,
 * 0x03 for T_HIGH), extracts 12-bit two's complement count, sign-extends,
 * and converts to float °C.
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

tmp102_status_t tmp102_set_t_low(tmp102_driver_t *dev, float temp_celsius)
{
    return write_threshold_register(dev, TMP102_REG_T_LOW, temp_celsius);
}

tmp102_status_t tmp102_set_t_high(tmp102_driver_t *dev, float temp_celsius)
{
    return write_threshold_register(dev, TMP102_REG_T_HIGH, temp_celsius);
}

tmp102_status_t tmp102_get_t_low(tmp102_driver_t *dev, float *temp_out)
{
    return read_threshold_register(dev, TMP102_REG_T_LOW, temp_out);
}

tmp102_status_t tmp102_get_t_high(tmp102_driver_t *dev, float *temp_out)
{
    return read_threshold_register(dev, TMP102_REG_T_HIGH, temp_out);
}
