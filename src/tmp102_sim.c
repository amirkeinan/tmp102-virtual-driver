/**
 * @file tmp102_sim.c
 * @brief Software emulator (mock) implementation for TMP102 temperature sensor.
 *
 * Implements register storage, power-up defaults, pointer mechanics,
 * physical temperature conversion injection, and virtual I2C bus transactions.
 */

#include "tmp102_sim.h"

/* ========================================================================= */
/* Emulator Lifecycle & Configuration API                                   */
/* ========================================================================= */

/**
 * @brief Initialize the TMP102 virtual hardware to power-up defaults.
 *
 * Sets every register and internal state field to the values
 * specified in the TMP102 datasheet for initial power-up:
 *   - temp_register   = 0x0000 (0 °C)
 *   - config_register = 0x60A0
 *   - t_low_register  = 0x4B00 (75 °C)
 *   - t_high_register = 0x5000 (80 °C)
 *   - pointer_register= 0x00   (Temperature)
 *   - i2c_address     = 0x48   (ADD0 → GND)
 *   - fault_count     = 0
 *   - alert_state     = false
 *
 * @param[out] hw  Caller-allocated emulator context. Ignored if NULL.
 */
void sim_init(tmp102_virtual_hw_t *hw) {
  if (hw == NULL) {
    return;
  }

  /* Initialize registers to power-up defaults specified in datasheet */
  hw->temp_register = TMP102_DEFAULT_TEMP;       /* 0x0000 (0°C) */
  hw->config_register = TMP102_DEFAULT_CONFIG;   /* 0x60A0 */
  hw->t_low_register = TMP102_DEFAULT_T_LOW;     /* 0x4B00 (75°C) */
  hw->t_high_register = TMP102_DEFAULT_T_HIGH;   /* 0x5000 (80°C) */
  hw->pointer_register = TMP102_DEFAULT_POINTER; /* 0x00 (Temp register) */

  /* Initialize emulator internal state */
  hw->i2c_address = TMP102_DEFAULT_ADDRESS; /* 0x48 (ADD0 to GND) */
  hw->fault_count = 0;
  hw->alert_state = false;
}

/**
 * @brief Reset the emulator to power-up defaults.
 *
 * Equivalent to cycling power on a physical TMP102. Delegates to
 * sim_init() so the reset values are always kept in one place.
 *
 * @param[in,out] hw  Emulator context to reset. Ignored if NULL.
 */
void sim_reset(tmp102_virtual_hw_t *hw) {
  /* Reset all registers and internal state back to power-up defaults */
  sim_init(hw);
}

/* ========================================================================= */
/* Internal Helpers — Thermostat Alert Logic                                  */
/* ========================================================================= */

/**
 * @brief Decode a 12-bit left-justified threshold register to °C.
 *
 * Threshold registers (T_LOW / T_HIGH) use the same 12-bit two's
 * complement left-justified layout as the Temperature Register in
 * Normal Mode: bits [15:4] hold the signed count, bits [3:0] are zero.
 *
 * @param[in] reg_val  Raw 16-bit register value.
 *
 * @return Temperature in degrees Celsius (0.0625 °C resolution).
 */
static float sim_decode_threshold_celsius(uint16_t reg_val) {
  int16_t count = (int16_t)(reg_val >> 4);

  /* Sign-extend from bit 11 */
  if ((count & 0x0800) != 0) {
    count = (int16_t)(count | (int16_t)0xF000);
  }

  return (float)count * TMP102_TEMP_RESOLUTION;
}

/**
 * @brief Refresh the AL (Alert) bit in the config register.
 *
 * The AL bit reflects the current alert output level.  Its
 * relationship to @c alert_state depends on the Polarity (POL) bit:
 *   - POL = 0 (active-low):  AL = !alert_state
 *   - POL = 1 (active-high): AL =  alert_state
 *
 * @param[in,out] hw  Emulator context whose config register is updated.
 */
static void sim_update_al_bit(tmp102_virtual_hw_t *hw) {
  bool pol = (hw->config_register & TMP102_CONFIG_POL_MASK) != 0U;
  bool al_value;

  if (pol) {
    /* Active high: alert_state directly maps to AL bit */
    al_value = hw->alert_state;
  } else {
    /* Active low: AL is inverted from alert_state */
    al_value = !hw->alert_state;
  }

  if (al_value) {
    hw->config_register |= TMP102_CONFIG_AL_MASK;
  } else {
    hw->config_register &= (uint16_t)(~TMP102_CONFIG_AL_MASK);
  }
}

/**
 * @brief Evaluate thermostat alert conditions after a temperature change.
 *
 * Implements the full TMP102 thermostat state-machine, including both
 * Comparator and Interrupt modes and the Fault Queue mechanism.
 *
 * **Comparator Mode** (TM = 0, default):
 *   - Alert asserts when temp ≥ T_HIGH (after fault queue threshold).
 *   - Alert de-asserts when temp < T_LOW (immediately).
 *   - In the hysteresis band [T_LOW, T_HIGH): previous state is held.
 *
 * **Interrupt Mode** (TM = 1):
 *   - Alert asserts on temp ≥ T_HIGH (after fault queue threshold).
 *   - Alert toggles (de-asserts) on temp < T_LOW (after fault queue
 *     threshold).
 *   - Reading the Configuration Register acknowledges (clears) the
 *     alert (handled inside sim_i2c_write_read()).
 *
 * **Fault Queue**: @c fault_count increments on each consecutive
 * reading that satisfies the trigger condition.  The alert state
 * changes only when @c fault_count reaches the F1:F0 threshold.
 *
 * @param[in,out] hw            Emulator context to evaluate.
 * @param[in]     temp_celsius  Current ambient temperature in °C.
 */
static void sim_evaluate_alert(tmp102_virtual_hw_t *hw, float temp_celsius) {
  /* Decode threshold values from registers */
  float t_high = sim_decode_threshold_celsius(hw->t_high_register);
  float t_low = sim_decode_threshold_celsius(hw->t_low_register);

  /* Extract Fault Queue count: F1:F0 → 1, 2, 4, 6 */
  uint8_t fq_raw =
      (uint8_t)((hw->config_register & TMP102_CONFIG_F1_F0_MASK) >>
                TMP102_CONFIG_F1_F0_SHIFT);
  static const uint8_t fq_lookup[4] = {1, 2, 4, 6};
  uint8_t fq_threshold = fq_lookup[fq_raw & 0x03U];

  /* Detect thermostat mode: 0=Comparator, 1=Interrupt */
  bool interrupt_mode =
      (hw->config_register & TMP102_CONFIG_TM_MASK) != 0U;

  if (!interrupt_mode) {
    /*
     * Comparator Mode:
     * - temp >= T_HIGH → increment fault_count, assert alert when threshold met
     * - temp < T_LOW   → reset fault_count, de-assert alert immediately
     * - T_LOW <= temp < T_HIGH → hysteresis, hold previous state, reset counter
     */
    if (temp_celsius >= t_high) {
      hw->fault_count++;
      if (hw->fault_count >= fq_threshold) {
        hw->alert_state = true;
      }
    } else if (temp_celsius < t_low) {
      hw->fault_count = 0;
      hw->alert_state = false;
    } else {
      /* In hysteresis band: hold alert_state, reset fault counter */
      hw->fault_count = 0;
    }
  } else {
    /*
     * Interrupt Mode:
     * - When alert_state is false: temp >= T_HIGH triggers alert
     * - When alert_state is true:  temp < T_LOW triggers de-assert
     * - Fault queue applies to both transitions
     * - Alert is acknowledged by reading the config register (handled in
     *   sim_i2c_write_read when reading config register)
     */
    if (!hw->alert_state) {
      /* Watching for high threshold crossing */
      if (temp_celsius >= t_high) {
        hw->fault_count++;
        if (hw->fault_count >= fq_threshold) {
          hw->alert_state = true;
          hw->fault_count = 0;
        }
      } else {
        hw->fault_count = 0;
      }
    } else {
      /* Alert is active; watching for low threshold crossing */
      if (temp_celsius < t_low) {
        hw->fault_count++;
        if (hw->fault_count >= fq_threshold) {
          hw->alert_state = false;
          hw->fault_count = 0;
        }
      } else {
        hw->fault_count = 0;
      }
    }
  }

  /* Update AL bit in config register based on alert_state and POL */
  sim_update_al_bit(hw);
}

/* ========================================================================= */
/* Backdoor API — Physical Condition Injection                               */
/* ========================================================================= */

/**
 * @brief Inject a simulated ambient temperature into the emulator.
 *
 * Converts @p temp_celsius to the internal register encoding (12-bit
 * or 13-bit two's complement, depending on the EM bit), stores it
 * in @c temp_register, and then re-evaluates the thermostat alert
 * logic via sim_evaluate_alert().
 *
 * @param[in,out] hw            Emulator context to update.
 *                              Ignored if NULL.
 * @param[in]     temp_celsius  Ambient temperature in °C.
 */
void sim_set_ambient_temperature(tmp102_virtual_hw_t *hw, float temp_celsius) {
  if (hw == NULL) {
    return;
  }

  /*
   * Calculate 12-bit / 13-bit digital count based on 0.0625°C resolution per
   * LSB. Round symmetrically to nearest integer to avoid precision truncation
   * issues.
   */
  float counts_f = temp_celsius / TMP102_TEMP_RESOLUTION;
  int16_t counts = (counts_f >= 0.0f) ? (int16_t)(counts_f + 0.5f)
                                      : (int16_t)(counts_f - 0.5f);

  /*
   * Encode count into 16-bit register format according to Extended Mode (EM)
   * bit:
   * - TMP102 hardware aligns ADC data left-justified so the sign bit resides at
   * bit 15, allowing direct two's complement interpretation and optional
   * single-byte (MSB) reads.
   * - Normal Mode (EM = 0): 12-bit two's complement, left-shifted into
   * bits 15..4, bits 3..0 = 0.
   * - Extended Mode (EM = 1): 13-bit two's complement, left-shifted into
   * bits 15..3, bits 2..0 = 0.
   */
  if ((hw->config_register & TMP102_CONFIG_EM_MASK) != 0U) {
    hw->temp_register = (uint16_t)(((uint16_t)counts << 3) & 0xFFF8U);
  } else {
    hw->temp_register = (uint16_t)(((uint16_t)counts << 4) & 0xFFF0U);
  }

  /* Evaluate thermostat alert logic after temperature update */
  sim_evaluate_alert(hw, temp_celsius);
}

/* ========================================================================= */
/* I2C Protocol Handlers                                                     */
/* ========================================================================= */

/**
 * @brief Handle a simulated I2C Write-then-Read (Repeated Start) transaction.
 *
 * Emulates the TMP102 response to an I2C combined write-read
 * sequence:
 *   1. Validates the target address against @c hw->i2c_address.
 *   2. If @p tx_len > 0, updates the Pointer Register from @c tx[0].
 *   3. Reads the 16-bit value from the currently selected register
 *      and stores the Big-Endian result in @p rx.
 *
 * In **Interrupt Mode** (TM = 1), reading the Configuration Register
 * acknowledges the alert by clearing @c alert_state and refreshing
 * the AL bit.
 *
 * @param[in]  user_data  Opaque pointer; must point to a valid
 *                        tmp102_virtual_hw_t instance.
 * @param[in]  addr       7-bit I2C target address.
 * @param[in]  tx         Pointer byte buffer (may be NULL if
 *                        @p tx_len is 0).
 * @param[in]  tx_len     Number of bytes in @p tx.
 * @param[out] rx         Buffer to receive the 2-byte register value.
 * @param[in]  rx_len     Size of @p rx in bytes (must be ≥ 2).
 *
 * @return  1 on success.
 * @return -1 on error (NULL pointers, address mismatch, invalid
 *         pointer register, or insufficient @p rx_len).
 */
int sim_i2c_write_read(void *user_data, uint8_t addr, const uint8_t *tx,
                       size_t tx_len, uint8_t *rx, size_t rx_len) {
  /* Validate input arguments defensively */
  if (user_data == NULL || rx == NULL || rx_len < 2U) {
    return -1;
  }

  tmp102_virtual_hw_t *hw = (tmp102_virtual_hw_t *)user_data;

  /* Address filtering: reject requests targeted at different I2C addresses */
  if (hw->i2c_address != addr) {
    return -1;
  }

  /* Update pointer register if pointer byte is supplied in tx buffer */
  if (tx_len > 0U) {
    if (tx == NULL) {
      return -1;
    }
    /* Validate pointer register range (0x00 to 0x03) */
    if (tx[0] > TMP102_REG_T_HIGH) {
      return -1;
    }
    hw->pointer_register = tx[0];
  }

  /* Retrieve 16-bit value from the currently selected register */
  uint16_t reg_val = 0;
  switch (hw->pointer_register) {
  case TMP102_REG_TEMP:
    reg_val = hw->temp_register;
    break;
  case TMP102_REG_CONFIG:
    reg_val = hw->config_register;
    /*
     * Interrupt Mode (TM=1): reading the config register acknowledges
     * the alert, clearing the alert_state and updating the AL bit.
     */
    if ((hw->config_register & TMP102_CONFIG_TM_MASK) != 0U) {
      hw->alert_state = false;
      hw->fault_count = 0;
      sim_update_al_bit(hw);
    }
    break;
  case TMP102_REG_T_LOW:
    reg_val = hw->t_low_register;
    break;
  case TMP102_REG_T_HIGH:
    reg_val = hw->t_high_register;
    break;
  default:
    return -1;
  }

  /* Output 2 bytes: MSB first, followed by LSB (Big-Endian I2C layout) */
  rx[0] = (uint8_t)((reg_val >> 8) & 0xFFU);
  rx[1] = (uint8_t)(reg_val & 0xFFU);

  return 1;
}

/**
 * @brief Handle a simulated I2C Write-only transaction.
 *
 * Emulates the TMP102 response to an I2C write:
 *   1. Validates the target address against @c hw->i2c_address.
 *   2. Updates the Pointer Register from @c tx[0].
 *   3. If @p tx_len == 3, writes the 16-bit payload (@c tx[1]:tx[2])
 *      to the selected register.  The Temperature Register (0x00)
 *      is read-only and writes to it are silently ignored.
 *
 * @param[in] user_data  Opaque pointer; must point to a valid
 *                       tmp102_virtual_hw_t instance.
 * @param[in] addr       7-bit I2C target address.
 * @param[in] tx         Transmission buffer (pointer byte +
 *                       optional 2-byte payload).
 * @param[in] tx_len     Number of bytes in @p tx (1 or 3).
 *
 * @return  1 on success.
 * @return -1 on error (NULL pointers, address mismatch, invalid
 *         pointer register, or unsupported @p tx_len).
 */
int sim_i2c_write(void *user_data, uint8_t addr, const uint8_t *tx,
                  size_t tx_len) {
  /* Validate input arguments defensively */
  if (user_data == NULL || tx == NULL || tx_len == 0U) {
    return -1;
  }

  tmp102_virtual_hw_t *hw = (tmp102_virtual_hw_t *)user_data;

  /* Address filtering: reject requests targeted at different I2C addresses */
  if (hw->i2c_address != addr) {
    return -1;
  }

  uint8_t ptr = tx[0];

  /* Validate pointer register range (0x00 to 0x03) */
  if (ptr > TMP102_REG_T_HIGH) {
    return -1;
  }

  /* Always update active pointer register */
  hw->pointer_register = ptr;

  if (tx_len == 1U) {
    /* Pointer register write only */
    return 1;
  } else if (tx_len == 3U) {
    /*
     * Register write: tx[1] is MSB, tx[2] is LSB.
     * Enforce read-only protection on Temperature register (0x00).
     */
    uint16_t reg_val = (uint16_t)(((uint16_t)tx[1] << 8) | (uint16_t)tx[2]);
    switch (ptr) {
    case TMP102_REG_TEMP:
      /* Temperature register is read-only; protect data by ignoring writes */
      break;
    case TMP102_REG_CONFIG:
      hw->config_register = reg_val;
      break;
    case TMP102_REG_T_LOW:
      hw->t_low_register = reg_val;
      break;
    case TMP102_REG_T_HIGH:
      hw->t_high_register = reg_val;
      break;
    default:
      return -1;
    }
    return 1;
  }

  /* Unsupported transaction length */
  return -1;
}
