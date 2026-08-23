/**
 * @file test_main.c
 * @brief Unit test harness and test runner for TMP102 driver and emulator.
 *
 * Implements lightweight test framework macros and test suites for
 * driver initialization, lifecycle, error handling, and temperature reading.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "tmp102_driver.h"
#include "tmp102_sim.h"

/* ========================================================================= */
/* Test Framework Infrastructure & Macros                                    */
/* ========================================================================= */

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_PASS 1
#define TEST_FAIL 0

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define RUN_TEST(test_func)                                                    \
    do {                                                                       \
        printf("[ RUN      ] %s\n", #test_func);                               \
        g_tests_run++;                                                         \
        if (test_func()) {                                                     \
            printf("[" ANSI_COLOR_GREEN "  PASSED  " ANSI_COLOR_RESET "] %s\n", #test_func); \
            g_tests_passed++;                                                  \
        } else {                                                               \
            printf("[" ANSI_COLOR_RED   "  FAILED  " ANSI_COLOR_RESET "] %s\n", #test_func); \
            g_tests_failed++;                                                  \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(condition)                                                 \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("  ASSERTION FAILED: (%s) at %s:%d\n",                      \
                   #condition, __FILE__, __LINE__);                            \
            return TEST_FAIL;                                                  \
        }                                                                      \
    } while (0)

#define ASSERT_EQUAL_INT(expected, actual)                                     \
    do {                                                                       \
        int _exp = (int)(expected);                                            \
        int _act = (int)(actual);                                              \
        if (_exp != _act) {                                                    \
            printf("  ASSERTION FAILED: expected 0x%X (%d), got 0x%X (%d) at %s:%d\n", \
                   _exp, _exp, _act, _act, __FILE__, __LINE__);                \
            return TEST_FAIL;                                                  \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_NEAR(expected, actual, epsilon)                           \
    do {                                                                       \
        float _diff = (float)(expected) - (float)(actual);                     \
        if (_diff < 0.0f) {                                                    \
            _diff = -_diff;                                                    \
        }                                                                      \
        if (_diff > (float)(epsilon)) {                                        \
            printf("  ASSERTION FAILED: expected %f, got %f (diff: %f > %f) at %s:%d\n", \
                   (double)(expected), (double)(actual),                       \
                   (double)_diff, (double)(epsilon), __FILE__, __LINE__);      \
            return TEST_FAIL;                                                  \
        }                                                                      \
    } while (0)

/* ========================================================================= */
/* Test Helper Utilities & Mocks                                             */
/* ========================================================================= */

/**
 * @brief Helper to initialize virtual hardware and populate HAL function struct.
 */
static void setup_test_environment(tmp102_virtual_hw_t *hw,
                                   tmp102_hal_funcs_t *hal,
                                   uint8_t i2c_addr)
{
    sim_init(hw);
    hw->i2c_address = i2c_addr;

    hal->i2c_write_read = sim_i2c_write_read;
    hal->i2c_write = sim_i2c_write;
    hal->user_data = hw;
}

/**
 * @brief Helper for parameterizing 12-bit Normal Mode temperature conversion tests.
 */
static int verify_normal_mode_temperature(float input_celsius,
                                          uint16_t expected_reg,
                                          float expected_celsius)
{
    tmp102_virtual_hw_t hw;
    tmp102_hal_funcs_t hal;
    tmp102_driver_t dev;

    setup_test_environment(&hw, &hal, TMP102_I2C_ADDR_GND);

    /* Inject ambient temperature via emulator backdoor */
    sim_set_ambient_temperature(&hw, input_celsius);

    /* Verify emulator register encoding (12-bit left-justified) */
    ASSERT_EQUAL_INT(expected_reg, hw.temp_register);

    /* Initialize driver instance */
    tmp102_status_t init_status = tmp102_init(&dev, TMP102_I2C_ADDR_GND, &hal);
    ASSERT_EQUAL_INT(TMP102_OK, init_status);

    /* Read temperature via driver and verify floating-point conversion */
    float read_temp = 0.0f;
    tmp102_status_t read_status = tmp102_get_temperature_celsius(&dev, &read_temp);
    ASSERT_EQUAL_INT(TMP102_OK, read_status);
    ASSERT_FLOAT_NEAR(expected_celsius, read_temp, 0.0001f);

    return TEST_PASS;
}

/**
 * @brief Mock I2C Write-then-Read function that simulates bus failure (-1).
 */
static int mock_failing_i2c_write_read(void *user_data, uint8_t addr,
                                       const uint8_t *tx_buf, size_t tx_len,
                                       uint8_t *rx_buf, size_t rx_len)
{
    (void)user_data;
    (void)addr;
    (void)tx_buf;
    (void)tx_len;
    (void)rx_buf;
    (void)rx_len;
    return -1;
}

/**
 * @brief Mock I2C Write function that simulates bus failure (-1).
 */
static int mock_failing_i2c_write(void *user_data, uint8_t addr,
                                  const uint8_t *tx_buf, size_t tx_len)
{
    (void)user_data;
    (void)addr;
    (void)tx_buf;
    (void)tx_len;
    return -1;
}

/* ========================================================================= */
/* Test Cases: Temperature Reading — Normal Mode 12-bit (T01 - T17)          */
/* ========================================================================= */

/**
 * @brief T01: Zero boundary (0.0°C -> 0x0000).
 */
static int test_t01_temp_zero(void)
{
    return verify_normal_mode_temperature(0.0f, 0x0000U, 0.0f);
}

/**
 * @brief T02: Smallest positive step (0.0625°C -> 0x0010, LSB resolution).
 */
static int test_t02_temp_smallest_positive_step(void)
{
    return verify_normal_mode_temperature(0.0625f, 0x0010U, 0.0625f);
}

/**
 * @brief T03: Quarter degree (0.25°C -> 0x0040).
 */
static int test_t03_temp_quarter_degree(void)
{
    return verify_normal_mode_temperature(0.25f, 0x0040U, 0.25f);
}

/**
 * @brief T04: Half degree (0.5°C -> 0x0080).
 */
static int test_t04_temp_half_degree(void)
{
    return verify_normal_mode_temperature(0.5f, 0x0080U, 0.5f);
}

/**
 * @brief T05: One degree (1.0°C -> 0x0100).
 */
static int test_t05_temp_one_degree(void)
{
    return verify_normal_mode_temperature(1.0f, 0x0100U, 1.0f);
}

/**
 * @brief T06: Typical room temperature (25.0°C -> 0x1900).
 */
static int test_t06_temp_typical_room(void)
{
    return verify_normal_mode_temperature(25.0f, 0x1900U, 25.0f);
}

/**
 * @brief T07: Room temperature fractional (25.5°C -> 0x1980).
 */
static int test_t07_temp_room_fractional(void)
{
    return verify_normal_mode_temperature(25.5f, 0x1980U, 25.5f);
}

/**
 * @brief T08: Body temperature (37.0°C -> 0x2500).
 */
static int test_t08_temp_body_temperature(void)
{
    return verify_normal_mode_temperature(37.0f, 0x2500U, 37.0f);
}

/**
 * @brief T09: Boiling point (100.0°C -> 0x6400).
 */
static int test_t09_temp_boiling_point(void)
{
    return verify_normal_mode_temperature(100.0f, 0x6400U, 100.0f);
}

/**
 * @brief T10: Max normal range upper boundary (127.9375°C -> 0x7FF0).
 */
static int test_t10_temp_max_normal_range(void)
{
    return verify_normal_mode_temperature(127.9375f, 0x7FF0U, 127.9375f);
}

/**
 * @brief T11: Smallest negative step (-0.0625°C -> 0xFFF0, two's complement boundary).
 */
static int test_t11_temp_smallest_negative_step(void)
{
    return verify_normal_mode_temperature(-0.0625f, 0xFFF0U, -0.0625f);
}

/**
 * @brief T12: Minus one (-1.0°C -> 0xFF00).
 */
static int test_t12_temp_minus_one(void)
{
    return verify_normal_mode_temperature(-1.0f, 0xFF00U, -1.0f);
}

/**
 * @brief T13: Negative integer (-25.0°C -> 0xE700).
 */
static int test_t13_temp_negative_integer(void)
{
    return verify_normal_mode_temperature(-25.0f, 0xE700U, -25.0f);
}

/**
 * @brief T14: Negative fractional (-25.5°C -> 0xE680).
 */
static int test_t14_temp_negative_fractional(void)
{
    return verify_normal_mode_temperature(-25.5f, 0xE680U, -25.5f);
}

/**
 * @brief T15: Small negative fraction (-0.5°C -> 0xFF80).
 */
static int test_t15_temp_small_negative_fraction(void)
{
    return verify_normal_mode_temperature(-0.5f, 0xFF80U, -0.5f);
}

/**
 * @brief T16: Typical sensor min operating temp (-55.0°C -> 0xC900).
 */
static int test_t16_temp_typical_sensor_min(void)
{
    return verify_normal_mode_temperature(-55.0f, 0xC900U, -55.0f);
}

/**
 * @brief T17: Min normal range lower boundary (-128.0°C -> 0x8000).
 */
static int test_t17_temp_min_normal_range(void)
{
    return verify_normal_mode_temperature(-128.0f, 0x8000U, -128.0f);
}

/* ========================================================================= */
/* Test Cases: Error Handling (T23 - T30)                                    */
/* ========================================================================= */

/**
 * @brief T23: NULL driver context pointer passed to tmp102_init().
 */
static int test_t23_null_driver_context_to_init(void)
{
    tmp102_virtual_hw_t hw;
    tmp102_hal_funcs_t hal;

    setup_test_environment(&hw, &hal, TMP102_I2C_ADDR_GND);

    tmp102_status_t status = tmp102_init(NULL, TMP102_I2C_ADDR_GND, &hal);
    ASSERT_EQUAL_INT(TMP102_ERR_NULL_PTR, status);

    return TEST_PASS;
}

/**
 * @brief T24: NULL HAL pointer passed to tmp102_init().
 */
static int test_t24_null_hal_pointer_to_init(void)
{
    tmp102_driver_t dev;

    tmp102_status_t status = tmp102_init(&dev, TMP102_I2C_ADDR_GND, NULL);
    ASSERT_EQUAL_INT(TMP102_ERR_NULL_PTR, status);

    return TEST_PASS;
}

/**
 * @brief T25: NULL output pointer passed to tmp102_get_temperature_celsius().
 */
static int test_t25_null_temp_output_pointer(void)
{
    tmp102_virtual_hw_t hw;
    tmp102_hal_funcs_t hal;
    tmp102_driver_t dev;

    setup_test_environment(&hw, &hal, TMP102_I2C_ADDR_GND);

    tmp102_status_t init_status = tmp102_init(&dev, TMP102_I2C_ADDR_GND, &hal);
    ASSERT_EQUAL_INT(TMP102_OK, init_status);

    /* Test NULL output pointer with valid driver */
    tmp102_status_t status1 = tmp102_get_temperature_celsius(&dev, NULL);
    ASSERT_EQUAL_INT(TMP102_ERR_NULL_PTR, status1);

    /* Test NULL driver context with valid output pointer */
    float temp_celsius = 0.0f;
    tmp102_status_t status2 = tmp102_get_temperature_celsius(NULL, &temp_celsius);
    ASSERT_EQUAL_INT(TMP102_ERR_NULL_PTR, status2);

    return TEST_PASS;
}

/**
 * @brief T26: Temperature read attempt on uninitialized driver instance.
 */
static int test_t26_read_temp_uninitialized_driver(void)
{
    tmp102_driver_t dev;
    dev.initialized = false;
    dev.i2c_address = TMP102_I2C_ADDR_GND;

    float temp_celsius = 0.0f;
    tmp102_status_t status = tmp102_get_temperature_celsius(&dev, &temp_celsius);
    ASSERT_EQUAL_INT(TMP102_ERR_NOT_INITIALIZED, status);

    return TEST_PASS;
}

/**
 * @brief T27: HAL i2c_write_read returns failure (propagated as TMP102_ERR_I2C).
 */
static int test_t27_hal_i2c_write_read_failure(void)
{
    tmp102_virtual_hw_t hw;
    tmp102_hal_funcs_t hal;
    tmp102_driver_t dev;

    setup_test_environment(&hw, &hal, TMP102_I2C_ADDR_GND);
    hal.i2c_write_read = mock_failing_i2c_write_read;

    tmp102_status_t init_status = tmp102_init(&dev, TMP102_I2C_ADDR_GND, &hal);
    ASSERT_EQUAL_INT(TMP102_OK, init_status);

    float temp_celsius = 0.0f;
    tmp102_status_t read_status = tmp102_get_temperature_celsius(&dev, &temp_celsius);
    ASSERT_EQUAL_INT(TMP102_ERR_I2C, read_status);

    return TEST_PASS;
}

/**
 * @brief T28: HAL i2c_write returns failure (propagated as TMP102_ERR_I2C).
 */
static int test_t28_hal_i2c_write_failure(void)
{
    tmp102_virtual_hw_t hw;
    tmp102_hal_funcs_t hal;
    tmp102_driver_t dev;

    setup_test_environment(&hw, &hal, TMP102_I2C_ADDR_GND);
    hal.i2c_write = mock_failing_i2c_write;

    tmp102_status_t init_status = tmp102_init(&dev, TMP102_I2C_ADDR_GND, &hal);
    ASSERT_EQUAL_INT(TMP102_OK, init_status);

    /* Verify HAL write failure returns negative value */
    uint8_t dummy_tx[2] = { TMP102_REG_CONFIG, 0x60 };
    int ret = dev.hal.i2c_write(dev.hal.user_data, dev.i2c_address, dummy_tx, sizeof(dummy_tx));
    ASSERT_TRUE(ret < 0);

    /* Verify HAL return code mapping convention to TMP102_ERR_I2C */
    tmp102_status_t mapped_status = (ret == 1) ? TMP102_OK : TMP102_ERR_I2C;
    ASSERT_EQUAL_INT(TMP102_ERR_I2C, mapped_status);

    return TEST_PASS;
}

/**
 * @brief T29: NULL i2c_write_read function pointer passed in HAL struct.
 */
static int test_t29_null_i2c_write_read_function_pointer(void)
{
    tmp102_virtual_hw_t hw;
    tmp102_hal_funcs_t hal;
    tmp102_driver_t dev;

    setup_test_environment(&hw, &hal, TMP102_I2C_ADDR_GND);
    hal.i2c_write_read = NULL;

    tmp102_status_t status = tmp102_init(&dev, TMP102_I2C_ADDR_GND, &hal);
    ASSERT_EQUAL_INT(TMP102_ERR_NULL_PTR, status);

    return TEST_PASS;
}

/**
 * @brief T30: NULL i2c_write function pointer passed in HAL struct.
 */
static int test_t30_null_i2c_write_function_pointer(void)
{
    tmp102_virtual_hw_t hw;
    tmp102_hal_funcs_t hal;
    tmp102_driver_t dev;

    setup_test_environment(&hw, &hal, TMP102_I2C_ADDR_GND);
    hal.i2c_write = NULL;

    tmp102_status_t status = tmp102_init(&dev, TMP102_I2C_ADDR_GND, &hal);
    ASSERT_EQUAL_INT(TMP102_ERR_NULL_PTR, status);

    return TEST_PASS;
}

/* ========================================================================= */
/* Test Cases: Initialization & Lifecycle (T31 - T36)                         */
/* ========================================================================= */

/**
 * @brief T31: Initialization with default address (0x48, ADD0 = GND).
 */
static int test_t31_init_default_address_0x48(void)
{
    tmp102_virtual_hw_t hw;
    tmp102_hal_funcs_t hal;
    tmp102_driver_t dev;

    setup_test_environment(&hw, &hal, TMP102_I2C_ADDR_GND);

    tmp102_status_t status = tmp102_init(&dev, TMP102_I2C_ADDR_GND, &hal);

    ASSERT_EQUAL_INT(TMP102_OK, status);
    ASSERT_TRUE(dev.initialized);
    ASSERT_EQUAL_INT(TMP102_I2C_ADDR_GND, dev.i2c_address);
    ASSERT_TRUE(dev.hal.i2c_write_read != NULL);
    ASSERT_TRUE(dev.hal.i2c_write != NULL);

    return TEST_PASS;
}

/**
 * @brief T32: Initialization with alternate address (0x49, ADD0 = V+).
 */
static int test_t32_init_address_0x49(void)
{
    tmp102_virtual_hw_t hw;
    tmp102_hal_funcs_t hal;
    tmp102_driver_t dev;

    setup_test_environment(&hw, &hal, TMP102_I2C_ADDR_VCC);

    tmp102_status_t status = tmp102_init(&dev, TMP102_I2C_ADDR_VCC, &hal);

    ASSERT_EQUAL_INT(TMP102_OK, status);
    ASSERT_TRUE(dev.initialized);
    ASSERT_EQUAL_INT(TMP102_I2C_ADDR_VCC, dev.i2c_address);

    return TEST_PASS;
}

/**
 * @brief T33: Initialization with alternate address (0x4A, ADD0 = SDA).
 */
static int test_t33_init_address_0x4a(void)
{
    tmp102_virtual_hw_t hw;
    tmp102_hal_funcs_t hal;
    tmp102_driver_t dev;

    setup_test_environment(&hw, &hal, TMP102_I2C_ADDR_SDA);

    tmp102_status_t status = tmp102_init(&dev, TMP102_I2C_ADDR_SDA, &hal);

    ASSERT_EQUAL_INT(TMP102_OK, status);
    ASSERT_TRUE(dev.initialized);
    ASSERT_EQUAL_INT(TMP102_I2C_ADDR_SDA, dev.i2c_address);

    return TEST_PASS;
}

/**
 * @brief T34: Initialization with alternate address (0x4B, ADD0 = SCL).
 */
static int test_t34_init_address_0x4b(void)
{
    tmp102_virtual_hw_t hw;
    tmp102_hal_funcs_t hal;
    tmp102_driver_t dev;

    setup_test_environment(&hw, &hal, TMP102_I2C_ADDR_SCL);

    tmp102_status_t status = tmp102_init(&dev, TMP102_I2C_ADDR_SCL, &hal);

    ASSERT_EQUAL_INT(TMP102_OK, status);
    ASSERT_TRUE(dev.initialized);
    ASSERT_EQUAL_INT(TMP102_I2C_ADDR_SCL, dev.i2c_address);

    return TEST_PASS;
}

/**
 * @brief T35: Double initialization idempotency check.
 */
static int test_t35_double_initialization(void)
{
    tmp102_virtual_hw_t hw;
    tmp102_hal_funcs_t hal;
    tmp102_driver_t dev;

    setup_test_environment(&hw, &hal, TMP102_I2C_ADDR_GND);

    /* First initialization */
    tmp102_status_t status1 = tmp102_init(&dev, TMP102_I2C_ADDR_GND, &hal);
    ASSERT_EQUAL_INT(TMP102_OK, status1);
    ASSERT_TRUE(dev.initialized);
    ASSERT_EQUAL_INT(TMP102_I2C_ADDR_GND, dev.i2c_address);

    /* Second initialization with different address (re-configuration) */
    tmp102_status_t status2 = tmp102_init(&dev, TMP102_I2C_ADDR_VCC, &hal);
    ASSERT_EQUAL_INT(TMP102_OK, status2);
    ASSERT_TRUE(dev.initialized);
    ASSERT_EQUAL_INT(TMP102_I2C_ADDR_VCC, dev.i2c_address);

    return TEST_PASS;
}

/**
 * @brief T36: Temperature read after successful initialization (full init->read flow).
 */
static int test_t36_read_after_successful_init(void)
{
    tmp102_virtual_hw_t hw;
    tmp102_hal_funcs_t hal;
    tmp102_driver_t dev;

    setup_test_environment(&hw, &hal, TMP102_I2C_ADDR_GND);

    /* Inject known ambient temperature (25.0°C) into virtual hardware */
    sim_set_ambient_temperature(&hw, 25.0f);

    /* Initialize driver instance */
    tmp102_status_t init_status = tmp102_init(&dev, TMP102_I2C_ADDR_GND, &hal);
    ASSERT_EQUAL_INT(TMP102_OK, init_status);
    ASSERT_TRUE(dev.initialized);

    /* Read temperature via driver */
    float temp_celsius = 0.0f;
    tmp102_status_t read_status = tmp102_get_temperature_celsius(&dev, &temp_celsius);

    ASSERT_EQUAL_INT(TMP102_OK, read_status);
    ASSERT_FLOAT_NEAR(25.0f, temp_celsius, 0.001f);

    return TEST_PASS;
}

/* ========================================================================= */
/* Test Cases: Emulator Validation (T72 - T77)                               */
/* ========================================================================= */

/**
 * @brief T72: Emulator initialization sets all power-up default register values.
 */
static int test_t72_emulator_init_defaults(void)
{
    tmp102_virtual_hw_t hw;
    sim_init(&hw);

    ASSERT_EQUAL_INT(TMP102_DEFAULT_CONFIG, hw.config_register);     /* 0x60A0 */
    ASSERT_EQUAL_INT(TMP102_DEFAULT_T_LOW, hw.t_low_register);       /* 0x4B00 (75°C) */
    ASSERT_EQUAL_INT(TMP102_DEFAULT_T_HIGH, hw.t_high_register);     /* 0x5000 (80°C) */
    ASSERT_EQUAL_INT(TMP102_DEFAULT_TEMP, hw.temp_register);         /* 0x0000 (0°C) */
    ASSERT_EQUAL_INT(TMP102_DEFAULT_POINTER, hw.pointer_register);   /* 0x00 */
    ASSERT_EQUAL_INT(TMP102_DEFAULT_ADDRESS, hw.i2c_address);       /* 0x48 */
    ASSERT_EQUAL_INT(0, hw.fault_count);
    ASSERT_TRUE(hw.alert_state == false);

    return TEST_PASS;
}

/**
 * @brief T73: Emulator reset restores all registers and internal state to defaults.
 */
static int test_t73_emulator_reset_defaults(void)
{
    tmp102_virtual_hw_t hw;
    sim_init(&hw);

    /* Mutate all registers and internal state */
    hw.config_register = 0x1234U;
    hw.t_low_register = 0x1111U;
    hw.t_high_register = 0x2222U;
    hw.temp_register = 0x3333U;
    hw.pointer_register = 0x02U;
    hw.fault_count = 5;
    hw.alert_state = true;
    hw.i2c_address = TMP102_I2C_ADDR_VCC;

    /* Perform soft reset */
    sim_reset(&hw);

    /* Verify all fields are restored to clean power-up defaults */
    ASSERT_EQUAL_INT(TMP102_DEFAULT_CONFIG, hw.config_register);
    ASSERT_EQUAL_INT(TMP102_DEFAULT_T_LOW, hw.t_low_register);
    ASSERT_EQUAL_INT(TMP102_DEFAULT_T_HIGH, hw.t_high_register);
    ASSERT_EQUAL_INT(TMP102_DEFAULT_TEMP, hw.temp_register);
    ASSERT_EQUAL_INT(TMP102_DEFAULT_POINTER, hw.pointer_register);
    ASSERT_EQUAL_INT(TMP102_DEFAULT_ADDRESS, hw.i2c_address);
    ASSERT_EQUAL_INT(0, hw.fault_count);
    ASSERT_TRUE(hw.alert_state == false);

    return TEST_PASS;
}

/**
 * @brief T74: Pointer register selects corresponding register for read/write.
 */
static int test_t74_pointer_register_selection(void)
{
    tmp102_virtual_hw_t hw;
    sim_init(&hw);

    uint8_t rx_buf[2] = {0};
    uint8_t tx_ptr[1];

    /* Select and read Configuration register (0x01) */
    tx_ptr[0] = TMP102_REG_CONFIG;
    int ret1 = sim_i2c_write_read(&hw, TMP102_DEFAULT_ADDRESS, tx_ptr, 1, rx_buf, 2);
    ASSERT_EQUAL_INT(1, ret1);
    uint16_t config_val = (uint16_t)(((uint16_t)rx_buf[0] << 8) | rx_buf[1]);
    ASSERT_EQUAL_INT(TMP102_DEFAULT_CONFIG, config_val);
    ASSERT_EQUAL_INT(TMP102_REG_CONFIG, hw.pointer_register);

    /* Select and read T_LOW register (0x02) */
    tx_ptr[0] = TMP102_REG_T_LOW;
    int ret2 = sim_i2c_write_read(&hw, TMP102_DEFAULT_ADDRESS, tx_ptr, 1, rx_buf, 2);
    ASSERT_EQUAL_INT(1, ret2);
    uint16_t t_low_val = (uint16_t)(((uint16_t)rx_buf[0] << 8) | rx_buf[1]);
    ASSERT_EQUAL_INT(TMP102_DEFAULT_T_LOW, t_low_val);
    ASSERT_EQUAL_INT(TMP102_REG_T_LOW, hw.pointer_register);

    /* Select and read T_HIGH register (0x03) */
    tx_ptr[0] = TMP102_REG_T_HIGH;
    int ret3 = sim_i2c_write_read(&hw, TMP102_DEFAULT_ADDRESS, tx_ptr, 1, rx_buf, 2);
    ASSERT_EQUAL_INT(1, ret3);
    uint16_t t_high_val = (uint16_t)(((uint16_t)rx_buf[0] << 8) | rx_buf[1]);
    ASSERT_EQUAL_INT(TMP102_DEFAULT_T_HIGH, t_high_val);
    ASSERT_EQUAL_INT(TMP102_REG_T_HIGH, hw.pointer_register);

    return TEST_PASS;
}

/**
 * @brief T75: Invalid pointer register value handled gracefully.
 */
static int test_t75_invalid_pointer_handling(void)
{
    tmp102_virtual_hw_t hw;
    sim_init(&hw);

    uint8_t invalid_ptr[1] = { 0x04 }; /* Valid pointer range is 0x00 - 0x03 */
    uint8_t rx_buf[2] = {0};

    /* Invalid pointer in write transaction */
    int ret_write = sim_i2c_write(&hw, TMP102_DEFAULT_ADDRESS, invalid_ptr, 1);
    ASSERT_EQUAL_INT(-1, ret_write);
    ASSERT_EQUAL_INT(TMP102_DEFAULT_POINTER, hw.pointer_register);

    /* Invalid pointer in write-then-read transaction */
    int ret_wr_rd = sim_i2c_write_read(&hw, TMP102_DEFAULT_ADDRESS, invalid_ptr, 1, rx_buf, 2);
    ASSERT_EQUAL_INT(-1, ret_wr_rd);
    ASSERT_EQUAL_INT(TMP102_DEFAULT_POINTER, hw.pointer_register);

    return TEST_PASS;
}

/**
 * @brief T76: Write to read-only temperature register is ignored / protected.
 */
static int test_t76_readonly_temp_register_protection(void)
{
    tmp102_virtual_hw_t hw;
    sim_init(&hw);

    /* Set known temperature (25.0°C -> 0x1900) */
    sim_set_ambient_temperature(&hw, 25.0f);
    ASSERT_EQUAL_INT(0x1900U, hw.temp_register);

    /* Attempt to overwrite Temperature register (0x00) via I2C write */
    uint8_t write_payload[3] = { TMP102_REG_TEMP, 0x7F, 0xF0 };
    int ret = sim_i2c_write(&hw, TMP102_DEFAULT_ADDRESS, write_payload, sizeof(write_payload));

    ASSERT_EQUAL_INT(1, ret);
    /* Value must remain 0x1900 without modification */
    ASSERT_EQUAL_INT(0x1900U, hw.temp_register);

    return TEST_PASS;
}

/**
 * @brief T77: Transactions targeted at wrong I2C address are rejected.
 */
static int test_t77_wrong_i2c_address_rejection(void)
{
    tmp102_virtual_hw_t hw;
    sim_init(&hw); /* Address = 0x48 */

    uint8_t tx_buf[1] = { TMP102_REG_TEMP };
    uint8_t rx_buf[2] = {0};

    /* Transactions to wrong addresses (0x49, 0x4A, 0x4B, 0x00) must return failure (-1) */
    int ret1 = sim_i2c_write_read(&hw, TMP102_I2C_ADDR_VCC, tx_buf, 1, rx_buf, 2);
    ASSERT_EQUAL_INT(-1, ret1);

    int ret2 = sim_i2c_write(&hw, TMP102_I2C_ADDR_VCC, tx_buf, 1);
    ASSERT_EQUAL_INT(-1, ret2);

    int ret3 = sim_i2c_write_read(&hw, 0x00U, tx_buf, 1, rx_buf, 2);
    ASSERT_EQUAL_INT(-1, ret3);

    return TEST_PASS;
}

/* ========================================================================= */
/* Main Test Runner                                                          */
/* ========================================================================= */

int main(void)
{
    printf("====================================================\n");
    printf("  TMP102 Virtual Driver Test Suite — Phase 1 MVP\n");
    printf("====================================================\n\n");

    printf("--- Temperature Reading: Normal 12-bit (T01 - T17) -\n");
    RUN_TEST(test_t01_temp_zero);
    RUN_TEST(test_t02_temp_smallest_positive_step);
    RUN_TEST(test_t03_temp_quarter_degree);
    RUN_TEST(test_t04_temp_half_degree);
    RUN_TEST(test_t05_temp_one_degree);
    RUN_TEST(test_t06_temp_typical_room);
    RUN_TEST(test_t07_temp_room_fractional);
    RUN_TEST(test_t08_temp_body_temperature);
    RUN_TEST(test_t09_temp_boiling_point);
    RUN_TEST(test_t10_temp_max_normal_range);
    RUN_TEST(test_t11_temp_smallest_negative_step);
    RUN_TEST(test_t12_temp_minus_one);
    RUN_TEST(test_t13_temp_negative_integer);
    RUN_TEST(test_t14_temp_negative_fractional);
    RUN_TEST(test_t15_temp_small_negative_fraction);
    RUN_TEST(test_t16_temp_typical_sensor_min);
    RUN_TEST(test_t17_temp_min_normal_range);

    printf("\n--- Error Handling Tests (T23 - T30) ---------------\n");
    RUN_TEST(test_t23_null_driver_context_to_init);
    RUN_TEST(test_t24_null_hal_pointer_to_init);
    RUN_TEST(test_t25_null_temp_output_pointer);
    RUN_TEST(test_t26_read_temp_uninitialized_driver);
    RUN_TEST(test_t27_hal_i2c_write_read_failure);
    RUN_TEST(test_t28_hal_i2c_write_failure);
    RUN_TEST(test_t29_null_i2c_write_read_function_pointer);
    RUN_TEST(test_t30_null_i2c_write_function_pointer);

    printf("\n--- Initialization & Lifecycle Tests (T31 - T36) ---\n");
    RUN_TEST(test_t31_init_default_address_0x48);
    RUN_TEST(test_t32_init_address_0x49);
    RUN_TEST(test_t33_init_address_0x4a);
    RUN_TEST(test_t34_init_address_0x4b);
    RUN_TEST(test_t35_double_initialization);
    RUN_TEST(test_t36_read_after_successful_init);

    printf("\n--- Emulator Validation Tests (T72 - T77) -----------\n");
    RUN_TEST(test_t72_emulator_init_defaults);
    RUN_TEST(test_t73_emulator_reset_defaults);
    RUN_TEST(test_t74_pointer_register_selection);
    RUN_TEST(test_t75_invalid_pointer_handling);
    RUN_TEST(test_t76_readonly_temp_register_protection);
    RUN_TEST(test_t77_wrong_i2c_address_rejection);

    printf("\n====================================================\n");
    printf("  Test Summary: %d Run | %d Passed | %d Failed\n",
           g_tests_run, g_tests_passed, g_tests_failed);
    printf("====================================================\n");

    if (g_tests_failed == 0) {
        printf(ANSI_COLOR_GREEN "ALL TESTS PASSED\n" ANSI_COLOR_RESET);
        return 0;
    } else {
        printf(ANSI_COLOR_RED "SOME TESTS FAILED\n" ANSI_COLOR_RESET);
        return 1;
    }
}
