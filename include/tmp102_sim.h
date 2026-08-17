/**
 * @file tmp102_sim.h
 * @brief Software emulator (mock) interface and state definitions for TMP102 sensor.
 *
 * Simulates TMP102 hardware registers, I2C bus transactions, address filtering,
 * pointer register mechanics, and backdoor physical condition injection.
 */

#ifndef TMP102_SIM_H
#define TMP102_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "tmp102_driver.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ========================================================================= */
/* Power-Up Default Values & Constants                                       */
/* ========================================================================= */

/** @brief Default power-up I2C slave address (ADD0 -> GND) */
#define TMP102_DEFAULT_ADDRESS      (0x48U)

/** @brief Default power-up Temperature Register value (0°C) */
#define TMP102_DEFAULT_TEMP         (0x0000U)

/** @brief Default power-up T_LOW threshold register value (75°C) */
#define TMP102_DEFAULT_T_LOW        (0x4B00U)

/** @brief Default power-up T_HIGH threshold register value (80°C) */
#define TMP102_DEFAULT_T_HIGH       (0x5000U)

/** @brief Default power-up Pointer Register value (points to Temperature) */
#define TMP102_DEFAULT_POINTER      (0x00U)

/* ========================================================================= */
/* Emulator Hardware State Struct                                            */
/* ========================================================================= */

/**
 * @brief Represents the internal physical registers and emulator runtime state.
 *
 * Fully encapsulates simulated hardware state to eliminate global variables.
 * Caller allocates instance (e.g. stack or static).
 */
typedef struct {
    /* ── Hardware Registers ── */
    uint16_t temp_register;       /**< 0x00: Temperature Register (Read-Only) */
    uint16_t config_register;     /**< 0x01: Configuration Register (R/W, default 0x60A0) */
    uint16_t t_low_register;      /**< 0x02: T_LOW Alert Threshold (R/W, default 0x4B00 -> 75°C) */
    uint16_t t_high_register;     /**< 0x03: T_HIGH Alert Threshold (R/W, default 0x5000 -> 80°C) */
    uint8_t  pointer_register;    /**< Active register selector (2-bit, values 0x00-0x03) */

    /* ── Emulator Internal State ── */
    uint8_t  i2c_address;         /**< Emulator's own I2C slave address (0x48-0x4B) */
    uint8_t  fault_count;         /**< Current consecutive fault counter for Fault Queue */
    bool     alert_state;         /**< Current alert output state (Comparator / Interrupt) */
} tmp102_virtual_hw_t;

/* ========================================================================= */
/* Emulator Lifecycle & Configuration API                                   */
/* ========================================================================= */

/**
 * @brief  Initializes the virtual hardware emulator to TMP102 power-up defaults.
 *
 * Sets:
 * - config_register = 0x60A0
 * - t_low_register = 0x4B00 (75°C)
 * - t_high_register = 0x5000 (80°C)
 * - temp_register = 0x0000 (0°C)
 * - pointer_register = 0x00
 * - i2c_address = 0x48 (TMP102_DEFAULT_ADDRESS)
 * - fault_count = 0
 * - alert_state = false
 *
 * @param  hw  Pointer to caller-allocated emulator context struct.
 */
void sim_init(tmp102_virtual_hw_t *hw);

/**
 * @brief  Resets the emulator registers to power-up defaults.
 * @param  hw  Pointer to emulator context struct.
 */
void sim_reset(tmp102_virtual_hw_t *hw);

/* ========================================================================= */
/* Backdoor API — Physical Condition Injection (Test Harness Bypass)         */
/* ========================================================================= */

/**
 * @brief  Injects a simulated physical ambient temperature into the emulator.
 *
 * Encodes the given Celsius temperature into two's complement format
 * (12-bit Normal Mode or 13-bit Extended Mode depending on the EM bit
 * in config_register) and stores it in temp_register.
 *
 * @param  hw            Pointer to emulator context struct.
 * @param  temp_celsius  Ambient temperature value in degrees Celsius.
 */
void sim_set_ambient_temperature(tmp102_virtual_hw_t *hw, float temp_celsius);

/* ========================================================================= */
/* I2C Protocol Handlers (Injected into Driver HAL)                          */
/* ========================================================================= */

/**
 * @brief  Handles simulated I2C Write-then-Read transactions (Repeated Start).
 *
 * 1. Checks device address against hw->i2c_address (returns -1 if mismatch).
 * 2. If tx_len > 0, updates pointer_register from tx[0].
 * 3. Reads 2 bytes from the selected internal register into rx.
 *
 * @param  user_data  Pointer to tmp102_virtual_hw_t instance.
 * @param  addr       7-bit I2C target slave address.
 * @param  tx         Pointer to transmission buffer (pointer byte).
 * @param  tx_len     Length of transmission buffer in bytes.
 * @param  rx         Pointer to reception buffer.
 * @param  rx_len     Length of reception buffer in bytes.
 * @return 1 on success, negative value (-1) on error / address mismatch.
 */
int sim_i2c_write_read(void *user_data, uint8_t addr,
                       const uint8_t *tx, size_t tx_len,
                       uint8_t *rx, size_t rx_len);

/**
 * @brief  Handles simulated I2C Write transactions.
 *
 * 1. Checks device address against hw->i2c_address (returns -1 if mismatch).
 * 2. If tx_len >= 1, sets pointer_register = tx[0].
 * 3. If tx_len == 3, writes the 16-bit payload (tx[1], tx[2]) into the selected
 *    register (if writable; temp_register is read-only).
 *
 * @param  user_data  Pointer to tmp102_virtual_hw_t instance.
 * @param  addr       7-bit I2C target slave address.
 * @param  tx         Pointer to transmission buffer.
 * @param  tx_len     Length of transmission buffer in bytes.
 * @return 1 on success, negative value (-1) on error / address mismatch.
 */
int sim_i2c_write(void *user_data, uint8_t addr,
                  const uint8_t *tx, size_t tx_len);

#ifdef __cplusplus
}
#endif

#endif /* TMP102_SIM_H */
