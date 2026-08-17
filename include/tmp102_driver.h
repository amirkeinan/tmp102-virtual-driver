/**
 * @file tmp102_driver.h
 * @brief Driver interface and type definitions for TMP102 digital temperature sensor.
 *
 * Texas Instruments TMP102 temperature sensor driver providing zero-dependency,
 * portable, and testable C99 implementation using Dependency Injection (HAL).
 */

#ifndef TMP102_DRIVER_H
#define TMP102_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ========================================================================= */
/* Device Constants & Addressing                                             */
/* ========================================================================= */

/** @brief TMP102 I2C 7-bit slave addresses based on ADD0 pin connection */
#define TMP102_I2C_ADDR_GND         (0x48U) /**< ADD0 connected to GND (Default) */
#define TMP102_I2C_ADDR_VCC         (0x49U) /**< ADD0 connected to V+ */
#define TMP102_I2C_ADDR_SDA         (0x4AU) /**< ADD0 connected to SDA */
#define TMP102_I2C_ADDR_SCL         (0x4BU) /**< ADD0 connected to SCL */

/** @brief TMP102 internal register pointer addresses */
#define TMP102_REG_TEMP             (0x00U) /**< Temperature Register (Read-Only) */
#define TMP102_REG_CONFIG           (0x01U) /**< Configuration Register (R/W) */
#define TMP102_REG_T_LOW            (0x02U) /**< T_LOW Alert Threshold (R/W) */
#define TMP102_REG_T_HIGH           (0x03U) /**< T_HIGH Alert Threshold (R/W) */

/** @brief TMP102 temperature conversion resolution */
#define TMP102_TEMP_RESOLUTION      (0.0625f) /**< 0.0625°C per LSB */

/** @brief Configuration Register default power-up value */
#define TMP102_DEFAULT_CONFIG       (0x60A0U)

/** @brief Configuration Register bit masks */
#define TMP102_CONFIG_OS_MASK       (0x8000U) /**< One-Shot / Extended Start */
#define TMP102_CONFIG_R1_R0_MASK    (0x6000U) /**< Converter Resolution (11 = 12/13-bit) */
#define TMP102_CONFIG_F1_F0_MASK    (0x1800U) /**< Fault Queue (00=1, 01=2, 10=4, 11=6) */
#define TMP102_CONFIG_POL_MASK      (0x0400U) /**< Alert Pin Polarity (0=Active Low, 1=High) */
#define TMP102_CONFIG_TM_MASK       (0x0200U) /**< Thermostat Mode (0=Comparator, 1=Interrupt) */
#define TMP102_CONFIG_SD_MASK       (0x0100U) /**< Shutdown Mode (0=Continuous, 1=Shutdown) */
#define TMP102_CONFIG_CR1_CR0_MASK  (0x00C0U) /**< Conversion Rate (00=0.25Hz, 01=1Hz, 10=4Hz, 11=8Hz) */
#define TMP102_CONFIG_AL_MASK       (0x0020U) /**< Alert Status (Read-Only) */
#define TMP102_CONFIG_EM_MASK       (0x0010U) /**< Extended Mode (0=12-bit, 1=13-bit) */

/* ========================================================================= */
/* Status & Error Codes                                                      */
/* ========================================================================= */

/**
 * @brief Standard return codes for all TMP102 driver operations.
 */
typedef enum {
    TMP102_OK = 0,                  /**< Operation completed successfully */
    TMP102_ERR_I2C,                 /**< I2C communication failure (HAL returned non-1) */
    TMP102_ERR_NULL_PTR,            /**< NULL pointer passed as argument */
    TMP102_ERR_NOT_INITIALIZED,     /**< Driver not initialized (tmp102_init() not called) */
    TMP102_ERR_INVALID_PARAM,       /**< Invalid parameter value (e.g., out-of-range address) */
    TMP102_ERR_TIMEOUT              /**< Operation timed out */
} tmp102_status_t;

/* ========================================================================= */
/* Hardware Abstraction Layer (HAL) & Context Structs                       */
/* ========================================================================= */

/**
 * @brief Hardware Abstraction Layer interface for I2C communication.
 *
 * Implements Dependency Injection to decouple the driver logic from physical
 * or virtual I2C peripheral implementations.
 */
typedef struct {
    /**
     * @brief Standard Write-then-Read sequence (Repeated Start).
     *
     * Writes tx_len bytes from tx_buf, then reads rx_len bytes into rx_buf.
     *
     * @param user_data Opaque pointer passed through to implementation.
     * @param addr      7-bit I2C slave address.
     * @param tx_buf    Pointer to buffer with data to transmit.
     * @param tx_len    Number of bytes to transmit.
     * @param rx_buf    Pointer to buffer where received data will be stored.
     * @param rx_len    Number of bytes to receive.
     * @return 1 on success, negative value on failure.
     */
    int (*i2c_write_read)(void *user_data, uint8_t addr,
                          const uint8_t *tx_buf, size_t tx_len,
                          uint8_t *rx_buf, size_t rx_len);

    /**
     * @brief Standard Write sequence.
     *
     * Writes tx_len bytes from tx_buf to device at addr.
     *
     * @param user_data Opaque pointer passed through to implementation.
     * @param addr      7-bit I2C slave address.
     * @param tx_buf    Pointer to buffer with data to transmit.
     * @param tx_len    Number of bytes to transmit.
     * @return 1 on success, negative value on failure.
     */
    int (*i2c_write)(void *user_data, uint8_t addr,
                     const uint8_t *tx_buf, size_t tx_len);

    /**
     * @brief Opaque user context pointer passed to every HAL function call.
     *        In testing/emulation, points to tmp102_virtual_hw_t state.
     */
    void *user_data;
} tmp102_hal_funcs_t;

/**
 * @brief TMP102 driver instance context.
 *
 * Stores configuration, initialization state, and injected HAL functions.
 * Caller allocates instance (e.g. stack or static); zero dynamic allocation.
 */
typedef struct {
    uint8_t i2c_address;          /**< 7-bit I2C address (0x48–0x4B) */
    bool initialized;             /**< Initialization guard flag */
    tmp102_hal_funcs_t hal;       /**< Injected I2C hardware abstraction functions */
} tmp102_driver_t;

/* ========================================================================= */
/* Driver Public API                                                         */
/* ========================================================================= */

/* ── Initialization & Lifecycle ── */

/**
 * @brief  Initializes a TMP102 driver instance with Dependency Injection.
 * @param  dev          Pointer to driver context struct (caller-allocated).
 * @param  i2c_address  7-bit I2C address (0x48–0x4B).
 * @param  hal          Pointer to populated HAL function struct.
 * @return TMP102_OK on success, or an error status code.
 * @pre    dev != NULL, hal != NULL, hal->i2c_write_read != NULL, hal->i2c_write != NULL.
 * @post   dev->initialized == true on success.
 */
tmp102_status_t tmp102_init(tmp102_driver_t *dev, uint8_t i2c_address,
                            const tmp102_hal_funcs_t *hal);

/* ── Temperature Reading ── */

/**
 * @brief  Reads the current temperature in Celsius from the sensor.
 *         Automatically handles 12-bit (Normal) or 13-bit (Extended) mode
 *         based on the EM bit in the configuration register.
 * @param  dev       Pointer to initialized driver context.
 * @param  temp_out  Pointer to float where temperature will be stored.
 * @return TMP102_OK on success, or an error status code.
 * @pre    dev != NULL, dev->initialized == true, temp_out != NULL.
 */
tmp102_status_t tmp102_get_temperature_celsius(tmp102_driver_t *dev,
                                                float *temp_out);

/* ── Configuration Register Access (Phase 2) ── */

/**
 * @brief  Reads the 16-bit configuration register.
 * @param  dev         Pointer to initialized driver context.
 * @param  config_out  Pointer to uint16_t where config value will be stored.
 * @return TMP102_OK on success, or an error status code.
 */
tmp102_status_t tmp102_read_config(tmp102_driver_t *dev, uint16_t *config_out);

/**
 * @brief  Writes a 16-bit value to the configuration register.
 * @param  dev     Pointer to initialized driver context.
 * @param  config  The 16-bit configuration value to write.
 * @return TMP102_OK on success, or an error status code.
 */
tmp102_status_t tmp102_write_config(tmp102_driver_t *dev, uint16_t config);

/* ── Alert Threshold Registers (Phase 2) ── */

/**
 * @brief  Sets the T_LOW alert threshold in Celsius.
 * @param  dev           Pointer to initialized driver context.
 * @param  temp_celsius  Threshold temperature in °C.
 * @return TMP102_OK on success, or an error status code.
 */
tmp102_status_t tmp102_set_t_low(tmp102_driver_t *dev, float temp_celsius);

/**
 * @brief  Sets the T_HIGH alert threshold in Celsius.
 * @param  dev           Pointer to initialized driver context.
 * @param  temp_celsius  Threshold temperature in °C.
 * @return TMP102_OK on success, or an error status code.
 */
tmp102_status_t tmp102_set_t_high(tmp102_driver_t *dev, float temp_celsius);

/**
 * @brief  Reads the current T_LOW threshold in Celsius.
 * @param  dev       Pointer to initialized driver context.
 * @param  temp_out  Pointer to float where threshold will be stored.
 * @return TMP102_OK on success, or an error status code.
 */
tmp102_status_t tmp102_get_t_low(tmp102_driver_t *dev, float *temp_out);

/**
 * @brief  Reads the current T_HIGH threshold in Celsius.
 * @param  dev       Pointer to initialized driver context.
 * @param  temp_out  Pointer to float where threshold will be stored.
 * @return TMP102_OK on success, or an error status code.
 */
tmp102_status_t tmp102_get_t_high(tmp102_driver_t *dev, float *temp_out);

#ifdef __cplusplus
}
#endif

#endif /* TMP102_DRIVER_H */
