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
            printf("  ASSERTION FAILED: expected %d, got %d at %s:%d\n",       \
                   _exp, _act, __FILE__, __LINE__);                            \
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
/* Test Helper Utilities                                                     */
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
/* Main Test Runner                                                          */
/* ========================================================================= */

int main(void)
{
    printf("====================================================\n");
    printf("  TMP102 Virtual Driver Test Suite — Phase 1 MVP\n");
    printf("====================================================\n\n");

    printf("--- Initialization & Lifecycle Tests (T31 - T36) ---\n");
    RUN_TEST(test_t31_init_default_address_0x48);
    RUN_TEST(test_t32_init_address_0x49);
    RUN_TEST(test_t33_init_address_0x4a);
    RUN_TEST(test_t34_init_address_0x4b);
    RUN_TEST(test_t35_double_initialization);
    RUN_TEST(test_t36_read_after_successful_init);

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
