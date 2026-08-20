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

void sim_reset(tmp102_virtual_hw_t *hw) {
  /* Reset all registers and internal state back to power-up defaults */
  sim_init(hw);
}

/* ========================================================================= */
/* Backdoor API — Physical Condition Injection                               */
/* ========================================================================= */

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
}

/* ========================================================================= */
/* I2C Protocol Handlers                                                     */
/* ========================================================================= */

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
