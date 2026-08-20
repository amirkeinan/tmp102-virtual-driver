/**
 * @file tmp102_driver.c
 * @brief Driver core implementation for TMP102 digital temperature sensor.
 *
 * Implements sensor initialization via Dependency Injection (HAL) and
 * temperature reading with 12-bit two's complement conversion.
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

    /*
     * Extract 12-bit Normal Mode value:
     * Shift right by 4 bits to obtain 12-bit count.
     */
    int16_t count = (int16_t)(raw >> 4);

    /* Sign-extend if negative (bit 11 is sign bit) */
    if ((count & 0x0800) != 0) {
        count = (int16_t)(count | (int16_t)0xF000);
    }

    /* Multiply count by temperature resolution (0.0625°C / LSB) */
    *temp_out = (float)count * TMP102_TEMP_RESOLUTION;

    return TMP102_OK;
}
