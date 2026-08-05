# TMP102 Virtual Driver — Technical Specification & System Architecture Blueprint

> **Document Version:** 1.0  
> **Date:** 2026-08-02  
> **Project:** TMP102 Virtual Driver  
> **Author:** Amir Keinan  
> **Dependencies:** [tmp102.pdf](tmp102.pdf) · [system-design.md](system-design.md) · [diagram.md](diagram.md) · [RegistersMap.md](RegistersMap.md) · [vision.md](vision.md)

---

## 1. Project Overview & Scope Boundaries

### 1.1 Core Objective

A lightweight, portable, and **dependency-free** C driver for the **TMP102 digital temperature sensor** (Texas Instruments). The project includes a software-based hardware emulator (Mock) that fully simulates the TMP102 register map and I2C protocol, enabling comprehensive testing and validation over a virtual I2C bus — **without requiring physical hardware**.

This project serves as a **portfolio-quality showcase** demonstrating professional embedded development practices: datasheet-driven implementation, clean modular architecture (Dependency Injection), comprehensive testing, and CI/CD automation.

**Key Quality Requirements:**
| Requirement | Description |
|:---|:---|
| **Portability** | Zero platform-specific code in the driver. Swapping the HAL implementation is the only change needed to run on real hardware (STM32, Raspberry Pi, etc.). |
| **Zero-Dependency** | No external libraries. Pure C99 implementation. |
| **Security** | Defensive programming throughout — null-pointer guards, initialization checks, meaningful error codes on every API call. |
| **Testability** | Architecture designed for full testability via Dependency Injection and a virtual emulator. |
| **Performance** | Efficient bitwise operations, minimal memory footprint, no dynamic allocation. |
| **Documentation** | Every public API, struct, and enum is fully documented. In-code comments explain *why*, not just *what*. |

### 1.2 In-Scope vs. Out-of-Scope

**In-Scope:**
- TMP102 driver implementation covering **all datasheet features** (temperature reading, configuration register, alert thresholds, conversion rate, shutdown mode, thermostat mode).
- Software-based TMP102 emulator for virtual I2C communication (complete register map simulation).
- 12-bit Normal Mode and 13-bit Extended Mode temperature conversion.
- Comprehensive test harness with pass/fail reporting (77 test cases).
- Makefile-based build system for Linux/WSL.
- GitHub Actions CI/CD pipeline (compile → test on every push/PR).
- Full documentation suite (README, API docs, architecture docs).

**Out-of-Scope:**
- Physical hardware deployment (driver is designed for portability but tested virtually only).
- Integration with a specific RTOS or microcontroller SDK.
- GUI or graphical temperature monitoring interface.
- Multi-sensor bus arbitration or advanced I2C features (clock stretching, bus recovery).
- DMA-based I2C transfers.

---

## 2. System Architecture & Dependency Management

### 2.1 Multi-Layer Architecture

The system is strictly divided into **four layers** to enforce modularity, portability, and separation of concerns. Each layer has a single, exclusive responsibility.

```text
+-----------------------------------------------------------------------+
|                               LAYER 1                                 |
|                      Application & Test Harness                       |
|                         tests/test_main.c                             |
| +-------------------+ +-------------------+ +-------------------+     |
| | Hardware Setup    | | Scenario Builder  | | Result Assertions |     |
| +-------------------+ +-------------------+ +-------------------+     |
+-----------------------------------------------------------------------+
         |                                                 |
         | (Backdoor physical injection)                   | (Public API calls)
         | sim_set_ambient_temperature(25.5)               | tmp102_get_temperature_celsius()
         v                                                 v
+-----------------------------------------------------------------------+
|                               LAYER 2                                 |
|                         TMP102 Core Driver                            |
|                 src/tmp102_driver.c | include/tmp102_driver.h         |
| +---------------+ +-------------+ +--------------+                    |
| | Register Map  | | Bitwise Ops | | Status Codes |                    |
| +---------------+ +-------------+ +--------------+                    |
+-----------------------------------------------------------------------+
                                                           |
                                                           | (Dependency Injection)
                                                           | hal.i2c_write_read(user_data, ...)
                                                           v
+-----------------------------------------------------------------------+
|                               LAYER 3                                 |
|                  Hardware Abstraction Layer (HAL)                      |
|                      tmp102_hal_funcs_t struct                        |
| +-------------------------+ +--------------------+                    |
| | int (*i2c_write_read)() | | int (*i2c_write)() |                    |
| +-------------------------+ +--------------------+                    |
| +----------------------------------------------+                      |
| | void *user_data  (→ emulator state context)  |                      |
| +----------------------------------------------+                      |
+-----------------------------------------------------------------------+
                                                           |
                                                           | (Routed call)
         +-------------------------------------------------+
         v                                                 v
+-----------------------------------------------------------------------+
|                               LAYER 4                                 |
|                       TMP102 Virtual Emulator                         |
|                 src/tmp102_sim.c | include/tmp102_sim.h               |
| +-------------------------+ +-----------------------------------+     |
| | tmp102_virtual_hw_t     | | sim_i2c_write_read()              |     |
| | (Registers State)       | | (Mock I2C Receiver)               |     |
| +-------------------------+ +-----------------------------------+     |
+-----------------------------------------------------------------------+
```

| Layer | Component | Responsibility |
|:---:|:---|:---|
| **Layer 1** | Test Harness (`tests/test_main.c`) | Initializes virtual hardware and driver; injects simulated data; asserts expected behavior; drives pass/fail reporting. |
| **Layer 2** | TMP102 Driver (`src/tmp102_driver.c`) | Core logic — exposes clean API, manages register map, performs bitwise parsing (12-bit/13-bit, two's complement), error handling. **Zero global variables.** |
| **Layer 3** | HAL (Function Pointers in `tmp102_hal_funcs_t`) | Communication bridge — defines standard I2C operation signatures; routes calls via Dependency Injection. |
| **Layer 4** | TMP102 Emulator (`src/tmp102_sim.c`) | Virtual hardware — maintains internal register state, implements I2C protocol receiving end, exposes backdoor functions for test injection. |

### 2.2 Dependency Rules & Interface Contracts

**Information Flow Rules:**
- Communication is strictly **top-down unidirectional**: Layer 1 → Layer 2 → Layer 3 → Layer 4.
- **No layer may call upward.** The emulator never calls the driver; the driver never calls the test harness.
- Layer 1 (Test Harness) has a special **bypass path** to Layer 4 (Emulator) via backdoor functions for injecting physical conditions (e.g., `sim_set_ambient_temperature()`). This is the only cross-layer exception and is used exclusively in the test context.

**Dependency Injection Mechanism:**
- The driver is fully decoupled from the hardware via the `tmp102_hal_funcs_t` struct, which holds **function pointers** for I2C operations and a `void *user_data` opaque context pointer.
- At initialization (`tmp102_init()`), the caller injects the HAL struct. In test configuration, the function pointers are set to the emulator's `sim_i2c_write_read()` and `sim_i2c_write()` functions, and `user_data` points to the emulator's state struct.
- This design allows the **exact same driver code** to run on real hardware — only the HAL function pointer assignments change.

**Context & Memory Management:**
- ❌ **Global variables are strictly prohibited** in the driver and emulator.
- All module state resides within dedicated **Context Structs** (`tmp102_driver_t` for the driver, `tmp102_virtual_hw_t` for the emulator) that are passed as parameters to every function.
- No dynamic memory allocation (`malloc`/`free`). All structs are caller-allocated (typically stack or static).

---

## 3. Data Structures & API Specifications

### 3.1 Status & Error Handling Protocol

Every driver API function returns a standard status code of type `tmp102_status_t`. This enum provides deterministic, machine-readable error reporting.

```c
typedef enum {
    TMP102_OK = 0,                  // Operation completed successfully
    TMP102_ERR_I2C,                 // I2C communication failure (HAL returned non-1)
    TMP102_ERR_NULL_PTR,            // NULL pointer passed as argument
    TMP102_ERR_NOT_INITIALIZED,     // Driver not initialized (tmp102_init() not called)
    TMP102_ERR_INVALID_PARAM,       // Invalid parameter value
    TMP102_ERR_TIMEOUT              // Operation timed out
} tmp102_status_t;
```

**HAL Return Value Convention:**
- HAL functions return `1` on **success**.
- HAL functions return a **negative value** on failure.
- The driver maps **any non-`1` return** to `TMP102_ERR_I2C`.

### 3.2 Memory Layout & Struct Definitions

#### HAL Function Pointers (`tmp102_hal_funcs_t`)
The Dependency Injection mechanism. Defines the I2C operations required by the driver.

```c
typedef struct {
    /**
     * Standard Write-then-Read sequence (Repeated Start).
     * Writes tx_len bytes from tx_buf, then reads rx_len bytes into rx_buf.
     * Returns 1 on success, negative on failure.
     */
    int (*i2c_write_read)(void *user_data, uint8_t addr,
                          const uint8_t *tx_buf, size_t tx_len,
                          uint8_t *rx_buf, size_t rx_len);

    /**
     * Standard Write sequence.
     * Writes tx_len bytes from tx_buf to the device at addr.
     * Returns 1 on success, negative on failure.
     */
    int (*i2c_write)(void *user_data, uint8_t addr,
                     const uint8_t *tx_buf, size_t tx_len);

    /**
     * Opaque user context passed to every HAL call.
     * In test configuration, points to tmp102_virtual_hw_t (emulator state).
     */
    void *user_data;
} tmp102_hal_funcs_t;
```

| Field | Type | Size | Role |
|:---|:---|:---|:---|
| `i2c_write_read` | Function pointer | 8 bytes (64-bit) | Write-then-Read I2C transaction |
| `i2c_write` | Function pointer | 8 bytes (64-bit) | Write-only I2C transaction |
| `user_data` | `void *` | 8 bytes (64-bit) | Opaque context for HAL implementation |

#### Driver Context (`tmp102_driver_t`)
Represents a single sensor instance. Multiple instances can coexist on the same I2C bus (different addresses).

```c
typedef struct {
    uint8_t i2c_address;          // 7-bit I2C address (0x48–0x4B)
    bool initialized;             // Initialization guard flag
    tmp102_hal_funcs_t hal;       // Injected I2C hardware abstraction functions
} tmp102_driver_t;
```

| Field | Type | Size | Role |
|:---|:---|:---|:---|
| `i2c_address` | `uint8_t` | 1 byte | Sensor I2C address (valid: `0x48`, `0x49`, `0x4A`, `0x4B`) |
| `initialized` | `bool` | 1 byte | Guards against using the driver before `tmp102_init()` |
| `hal` | `tmp102_hal_funcs_t` | 24 bytes (64-bit) | Injected HAL function pointers and context |

#### Emulator State (`tmp102_virtual_hw_t`)
Represents the physical registers of the TMP102 sensor.

```c
typedef struct {
    // ── Hardware Registers ──
    uint16_t temp_register;       // 0x00 — Temperature (Read-Only)
    uint16_t config_register;     // 0x01 — Configuration (R/W)
                                  //   Power-up default: 0x60A0
                                  //   MSB [OS=0 R1=1 R0=1 F1=0 F0=0 POL=0 TM=0 SD=0]
                                  //   LSB [CR1=1 CR0=0 AL=1 EM=0 0 0 0 0]
                                  //   → 12-bit resolution, 4Hz conversion, comparator mode
    uint16_t t_low_register;      // 0x02 — T_LOW threshold (R/W, default: 0x4B00 → 75°C)
    uint16_t t_high_register;     // 0x03 — T_HIGH threshold (R/W, default: 0x5000 → 80°C)
    uint8_t  pointer_register;    // Active register pointer (2-bit, values 0x00–0x03)

    // ── Emulator Internal State ──
    uint8_t  i2c_address;         // Emulator's own I2C slave address (0x48–0x4B)
    uint8_t  fault_count;         // Current consecutive fault counter (for Fault Queue logic)
    bool     alert_state;         // Current alert output state (for comparator/interrupt mode)
} tmp102_virtual_hw_t;
```

| Field | Type | Size | Pointer Addr | Default | Description |
|:---|:---|:---|:---|:---|:---|
| `temp_register` | `uint16_t` | 2 bytes | `0x00` | `0x0000` | Ambient temperature (Read-Only, 12/13-bit two's complement) |
| `config_register` | `uint16_t` | 2 bytes | `0x01` | `0x60A0` | Device operating modes and status |
| `t_low_register` | `uint16_t` | 2 bytes | `0x02` | `0x4B00` | Lower alert threshold (75°C) |
| `t_high_register` | `uint16_t` | 2 bytes | `0x03` | `0x5000` | Upper alert threshold (80°C) |
| `pointer_register` | `uint8_t` | 1 byte | — | `0x00` | Active register selector (2-bit) |
| `i2c_address` | `uint8_t` | 1 byte | — | `0x48` | Emulator's own I2C slave address. Used for address filtering — the emulator only responds to transactions targeting this address. Supports all four valid TMP102 addresses (0x48–0x4B). |
| `fault_count` | `uint8_t` | 1 byte | — | `0` | Tracks consecutive faults for Fault Queue logic (F1:F0 bits). Incremented when temperature exceeds T_HIGH or drops below T_LOW; reset when condition clears. Alert is asserted only when `fault_count` reaches the configured queue depth (1/2/4/6). |
| `alert_state` | `bool` | 1 byte | — | `false` | Tracks the current alert output state. In Comparator mode: set when faults reach threshold, cleared when temp drops below T_LOW. In Interrupt mode: set on threshold crossing, cleared on any register read. Reflected in the AL bit of the config register. |

### 3.3 Public API Signatures

#### Driver API (`include/tmp102_driver.h`)

```c
/* ── Initialization & Lifecycle ── */

/**
 * @brief  Initializes a TMP102 driver instance with Dependency Injection.
 * @param  dev          Pointer to driver context struct (caller-allocated).
 * @param  i2c_address  7-bit I2C address (0x48–0x4B).
 * @param  hal          Pointer to populated HAL function struct.
 * @return TMP102_OK on success.
 * @pre    dev != NULL, hal != NULL, hal->i2c_write_read != NULL, hal->i2c_write != NULL.
 * @post   dev->initialized == true.
 */
tmp102_status_t tmp102_init(tmp102_driver_t *dev, uint8_t i2c_address,
                            tmp102_hal_funcs_t *hal);

/* ── Temperature Reading ── */

/**
 * @brief  Reads the current temperature in Celsius from the sensor.
 *         Automatically handles 12-bit (Normal) or 13-bit (Extended) mode
 *         based on the EM bit in the configuration register.
 * @param  dev       Pointer to initialized driver context.
 * @param  temp_out  Pointer to float where temperature will be stored.
 * @return TMP102_OK on success.
 * @pre    dev->initialized == true, temp_out != NULL.
 */
tmp102_status_t tmp102_get_temperature_celsius(tmp102_driver_t *dev,
                                                float *temp_out);

/* ── Configuration Register (Phase 2) ── */

/**
 * @brief  Reads the 16-bit configuration register.
 * @param  dev         Pointer to initialized driver context.
 * @param  config_out  Pointer to uint16_t where config value will be stored.
 * @return TMP102_OK on success.
 */
tmp102_status_t tmp102_read_config(tmp102_driver_t *dev, uint16_t *config_out);

/**
 * @brief  Writes a 16-bit value to the configuration register.
 * @param  dev     Pointer to initialized driver context.
 * @param  config  The 16-bit configuration value to write.
 * @return TMP102_OK on success.
 */
tmp102_status_t tmp102_write_config(tmp102_driver_t *dev, uint16_t config);

/* ── Alert Threshold Registers (Phase 2) ── */

/**
 * @brief  Sets the T_LOW alert threshold in Celsius.
 * @param  dev           Pointer to initialized driver context.
 * @param  temp_celsius  Threshold temperature in °C.
 * @return TMP102_OK on success.
 */
tmp102_status_t tmp102_set_t_low(tmp102_driver_t *dev, float temp_celsius);

/**
 * @brief  Sets the T_HIGH alert threshold in Celsius.
 */
tmp102_status_t tmp102_set_t_high(tmp102_driver_t *dev, float temp_celsius);

/**
 * @brief  Reads the current T_LOW threshold in Celsius.
 */
tmp102_status_t tmp102_get_t_low(tmp102_driver_t *dev, float *temp_out);

/**
 * @brief  Reads the current T_HIGH threshold in Celsius.
 */
tmp102_status_t tmp102_get_t_high(tmp102_driver_t *dev, float *temp_out);
```

#### Emulator API (`include/tmp102_sim.h`)

```c
/* ── Lifecycle ── */

/**
 * @brief  Initializes the emulator to TMP102 power-up defaults.
 *         config=0x60A0, t_low=0x4B00, t_high=0x5000, temp=0x0000, ptr=0x00.
 * @param  hw  Pointer to emulator state struct (caller-allocated).
 */
void sim_init(tmp102_virtual_hw_t *hw);

/**
 * @brief  Resets the emulator to power-up defaults (same as sim_init).
 */
void sim_reset(tmp102_virtual_hw_t *hw);

/* ── Backdoor — Physical Condition Injection ── */

/**
 * @brief  Injects an ambient temperature into the emulator.
 *         Converts the float to the appropriate register encoding
 *         (12-bit or 13-bit based on EM bit) and stores in temp_register.
 * @param  hw            Pointer to emulator state.
 * @param  temp_celsius  Temperature to inject (°C).
 */
void sim_set_ambient_temperature(tmp102_virtual_hw_t *hw, float temp_celsius);

/* ── I2C Protocol Handlers (injected into HAL) ── */

/**
 * @brief  Handles I2C Write-then-Read transactions.
 *         Updates pointer_register from tx_buf, then copies the selected
 *         register's 2 bytes into rx_buf.
 * @return 1 on success, negative on failure (e.g., wrong address).
 */
int sim_i2c_write_read(void *user_data, uint8_t addr,
                       const uint8_t *tx, size_t tx_len,
                       uint8_t *rx, size_t rx_len);

/**
 * @brief  Handles I2C Write transactions.
 *         Updates pointer_register and optionally writes data to the
 *         selected register (if writable).
 * @return 1 on success, negative on failure.
 */
int sim_i2c_write(void *user_data, uint8_t addr,
                  const uint8_t *tx, size_t tx_len);
```

---

## 4. Implementation Work Breakdown Structure (WBS)

### 4.1 Task Qualification Rules

Every task defined in this document satisfies the three fundamental criteria:

| Criterion | Definition |
|:---|:---|
| **Scoped** | Clear start and end points. Explicitly defines targeted files and functions. |
| **Buildable** | Small enough to be completed in a single focused work session (15–45 minutes). |
| **Verifiable** | Includes a strict, binary completion criterion (Pass/Fail) leaving no room for guesswork. |

> **Golden Rule:** If a task does not satisfy all 3 conditions, it is not ready for execution and must be further decomposed.

---

### 4.2 Phase 1: MVP (Minimum Viable Product)

#### Task 1.1 — Build System Setup

| | |
|:---|:---|
| **Dependencies** | None (first task). |
| **Scope** | Create `Makefile` at project root. |
| **Deliverable** | A `Makefile` with targets: `all` (compile driver + emulator objects), `test` (build + run test binary), `clean` (remove artifacts). Compiler: `gcc`, flags: `-Wall -Wextra -Werror -std=c99`. Include paths: `-Iinclude`. |
| **Verification** | `make clean && make all` completes with **zero warnings and zero errors**. |

#### Task 1.2 — Status Enum & Shared Types

| | |
|:---|:---|
| **Dependencies** | Task 1.1 (Makefile must exist for compilation checks). |
| **Scope** | Create `include/tmp102_driver.h` — define `tmp102_status_t` enum, `tmp102_hal_funcs_t` struct, `tmp102_driver_t` struct, and necessary includes (`<stdint.h>`, `<stdbool.h>`, `<stddef.h>`). |
| **Deliverable** | Complete header file with all type definitions, include guards, and comprehensive documentation (doxygen-style). |
| **Verification** | `gcc -fsyntax-only -std=c99 -Iinclude include/tmp102_driver.h` returns 0. |

#### Task 1.3 — Emulator Types & Header

| | |
|:---|:---|
| **Dependencies** | Task 1.1 (Makefile), Task 1.2 (driver header — `tmp102_sim.h` includes types from `tmp102_driver.h`). |
| **Scope** | Create `include/tmp102_sim.h` — define `tmp102_virtual_hw_t` struct (including `i2c_address`, `fault_count`, and `alert_state` fields) and all emulator API function prototypes. |
| **Deliverable** | Complete header file with struct definition (all register fields + internal emulator state fields), function prototypes, power-up default macros (e.g., `#define TMP102_DEFAULT_CONFIG 0x60A0`, `#define TMP102_DEFAULT_ADDRESS 0x48`), and documentation. |
| **Verification** | `gcc -fsyntax-only -std=c99 -Iinclude include/tmp102_sim.h` returns 0. |

#### Task 1.4 — Emulator Core Implementation

| | |
|:---|:---|
| **Dependencies** | Task 1.3 (emulator header with struct and function prototypes). |
| **Scope** | Create `src/tmp102_sim.c` — implement `sim_init()`, `sim_reset()`, `sim_set_ambient_temperature()` (12-bit Normal Mode only), `sim_i2c_write_read()`, `sim_i2c_write()`. |
| **Deliverable** | Full emulator implementing: register initialization to power-up defaults, `i2c_address` initialization (default `0x48`), `fault_count` and `alert_state` initialization to zero/false, pointer register logic, float-to-12-bit-register temperature conversion, I2C address filtering via `hw->i2c_address` (respond only to matching address), read-only protection on temp register (0x00), and register read via pointer. |
| **Verification** | `make all` compiles cleanly. |

#### Task 1.5 — Driver Core Implementation

| | |
|:---|:---|
| **Dependencies** | Task 1.2 (driver header with types), Task 1.4 (emulator — needed to test the driver end-to-end, though not a compile dependency). |
| **Scope** | Create `src/tmp102_driver.c` — implement `tmp102_init()` and `tmp102_get_temperature_celsius()` (12-bit Normal Mode). |
| **Deliverable** | `tmp102_init()`: validates all parameters (null-pointer checks for `dev`, `hal`, `hal->i2c_write_read`, `hal->i2c_write`), copies HAL struct, sets address and `initialized` flag. `tmp102_get_temperature_celsius()`: validates init guard and output pointer, sends pointer register write (0x00), reads 2 bytes, extracts 12-bit two's complement value, applies 0.0625°C multiplier, stores result. |
| **Verification** | `make all` compiles cleanly. |

#### Task 1.6 — Test Infrastructure & Initialization Tests

| | |
|:---|:---|
| **Dependencies** | Task 1.4 (emulator implementation), Task 1.5 (driver implementation). |
| **Scope** | Create `tests/test_main.c` — implement test infrastructure (test counter, pass/fail macros, `main()` runner, descriptive output) and initialization/lifecycle tests (T31–T36). |
| **Deliverable** | Test binary with reusable test macros and 6 passing initialization tests covering all four I2C addresses, double-init idempotency, and a full init→read flow. |
| **Verification** | `make test` builds and runs. Tests T31–T36 **all pass** (exit code 0). |

#### Task 1.7 — Error Handling Tests

| | |
|:---|:---|
| **Dependencies** | Task 1.6 (test infrastructure and `test_main.c` exist). |
| **Scope** | `tests/test_main.c` — add error handling test cases T23–T30. |
| **Deliverable** | 8 test cases covering: NULL pointer checks on init parameters (T23–T24), NULL output pointer (T25), uninitialized driver guard (T26), HAL failure propagation (T27–T28), and missing HAL function pointers (T29–T30). |
| **Verification** | `make test` runs. Tests T23–T30 **all pass**. |

#### Task 1.8 — Temperature Reading Tests (12-bit Normal Mode)

| | |
|:---|:---|
| **Dependencies** | Task 1.6 (test infrastructure). |
| **Scope** | `tests/test_main.c` — add 12-bit temperature reading test cases T01–T17. |
| **Deliverable** | 17 test cases covering: zero boundary, LSB resolution (0.0625°C), positive integers and fractions, negative integers and fractions (two's complement), upper boundary (127.9375°C), and lower boundary (−128°C). |
| **Verification** | `make test` runs. Tests T01–T17 **all pass**. |

#### Task 1.9 — Emulator Validation Tests

| | |
|:---|:---|
| **Dependencies** | Task 1.6 (test infrastructure). |
| **Scope** | `tests/test_main.c` — add emulator validation test cases T72–T77. |
| **Deliverable** | 6 test cases covering: power-up defaults (T72), reset (T73), pointer register selection (T74), invalid pointer handling (T75), read-only temp register protection (T76), and wrong I2C address rejection (T77). |
| **Verification** | `make test` runs. Tests T72–T77 **all pass**. |

#### Task 1.10 — CI/CD Pipeline

| | |
|:---|:---|
| **Dependencies** | Tasks 1.6–1.9 (all Phase 1 tests must exist and pass locally before CI is meaningful). |
| **Scope** | Create `.github/workflows/ci.yml`. |
| **Deliverable** | GitHub Actions workflow triggered on `push` and `pull_request` to `main`. Steps: checkout → install gcc → `make clean` → `make test`. Uses `ubuntu-latest` runner. |
| **Verification** | Push to repository triggers CI. Pipeline completes with **green status**. |

---

### 4.3 Phase 2: Advanced Features & Edge Cases

#### Task 2.1 — Configuration Register Read/Write + Tests

| | |
|:---|:---|
| **Dependencies** | Phase 1 complete (Tasks 1.1–1.10). Specifically requires: driver header (Task 1.2), emulator (Task 1.4), driver core (Task 1.5), test infrastructure (Task 1.6). |
| **Scope** | `src/tmp102_driver.c` — implement `tmp102_read_config()` and `tmp102_write_config()`. `tests/test_main.c` — add tests T37 and T54. |
| **Deliverable** | Read: sends pointer register write (0x01), reads 2 bytes, returns as `uint16_t`. Write: sends pointer register + 2 data bytes. Full null-pointer and init guards. Tests verify default config read (0x60A0) and write-then-read-back round-trip. |
| **Verification** | `make test` passes. Tests T37, T54 **pass** — read default config returns `0x60A0`; write-then-read-back returns matching value. |

#### Task 2.2 — Extended Mode (13-bit) Temperature Support + Tests

| | |
|:---|:---|
| **Dependencies** | Task 2.1 (config register read is required to detect the EM bit). |
| **Scope** | `src/tmp102_driver.c` — update `tmp102_get_temperature_celsius()` to detect EM bit and handle 13-bit mode. `src/tmp102_sim.c` — update `sim_set_ambient_temperature()` to handle 13-bit encoding when EM=1. `tests/test_main.c` — add tests T18–T22. |
| **Deliverable** | Driver reads config register EM bit before parsing temperature. If EM=1, shifts by 3 bits and sign-extends 13-bit value. Emulator encodes temperature as 13-bit when EM=1 in config_register. Tests verify extended mode readings for 128°C, 140°C, 150°C, 0°C, and −128°C. |
| **Verification** | `make test` passes. Tests T18–T22 **all pass**. |

#### Task 2.3 — Alert Threshold Registers (T_LOW / T_HIGH) + Tests

| | |
|:---|:---|
| **Dependencies** | Task 2.1 (config register read/write pattern is reused for threshold register access). |
| **Scope** | `src/tmp102_driver.c` — implement `tmp102_set_t_low()`, `tmp102_set_t_high()`, `tmp102_get_t_low()`, `tmp102_get_t_high()`. `tests/test_main.c` — add tests T55–T62. |
| **Deliverable** | Set functions convert float °C to 12-bit register format and write to registers 0x02/0x03. Get functions read registers 0x02/0x03 and convert back to float °C. Full validation. Tests verify default threshold reads, custom value round-trips, negative and boundary values. |
| **Verification** | `make test` passes. Tests T55–T62 **all pass**. |

#### Task 2.4 — Configuration Bit-Field Operations + Tests

| | |
|:---|:---|
| **Dependencies** | Task 2.1 (requires `tmp102_read_config()` and `tmp102_write_config()` for the read-modify-write pattern). |
| **Scope** | `src/tmp102_driver.c` — add helper functions or convenience wrappers for individual config fields (SD, TM, POL, F1:F0, CR1:CR0, EM, OS). `tests/test_main.c` — add tests T38–T53. |
| **Deliverable** | Functions to set/clear individual configuration bits using read-modify-write pattern. Bit masks defined as `#define` constants in the header. Tests verify each configuration field can be independently set and read back. |
| **Verification** | `make test` passes. Tests T38–T53 **all pass**. |

#### Task 2.5 — Thermostat & Alert Logic in Emulator + Tests

| | |
|:---|:---|
| **Dependencies** | Task 2.3 (threshold registers must be writable), Task 2.4 (config bit-field operations — TM, POL, F1:F0 must be configurable). |
| **Scope** | `src/tmp102_sim.c` — implement comparator mode alert logic, interrupt mode alert logic, fault queue filtering (using `fault_count` field), and polarity configuration (using `alert_state` field). `tests/test_main.c` — add tests T63–T71. |
| **Deliverable** | Emulator evaluates alert conditions on each `sim_set_ambient_temperature()` call: compares temp against T_LOW/T_HIGH, applies fault queue counter (`hw->fault_count`), sets/clears AL bit in config register and updates `hw->alert_state` per comparator/interrupt mode semantics, respects POL bit for output polarity. Tests verify comparator triggers, interrupt mode with read-clear, fault queue filtering, and polarity inversion. |
| **Verification** | `make test` runs **all 77 test cases** and **ALL pass**. |

---

### 4.4 Dependency Overview & Critical Path

The following table summarizes all task dependencies and identifies the critical path to each phase milestone.

#### Phase 1 — MVP Dependency Matrix

| Task | Name | Dependencies | Parallelizable With |
|:---|:---|:---|:---|
| **1.1** | Build System Setup | — | — |
| **1.2** | Status Enum & Shared Types | 1.1 | — |
| **1.3** | Emulator Types & Header | 1.1, 1.2 | — |
| **1.4** | Emulator Core Implementation | 1.3 | 1.5 (after 1.2) |
| **1.5** | Driver Core Implementation | 1.2 | 1.4 (after 1.3) |
| **1.6** | Test Infra & Init Tests | 1.4, 1.5 | — |
| **1.7** | Error Handling Tests | 1.6 | 1.8, 1.9 |
| **1.8** | Temperature Reading Tests | 1.6 | 1.7, 1.9 |
| **1.9** | Emulator Validation Tests | 1.6 | 1.7, 1.8 |
| **1.10** | CI/CD Pipeline | 1.6–1.9 | — |

**Critical Path to MVP:** `1.1 → 1.2 → 1.3 → 1.4 → 1.6 → 1.10`

> **Note:** Tasks 1.4 and 1.5 can be developed in parallel once their respective headers (1.3 and 1.2) are complete. Tasks 1.7, 1.8, and 1.9 are fully parallelizable once the test infrastructure (1.6) is in place.

#### Phase 2 — Advanced Features Dependency Matrix

| Task | Name | Dependencies | Parallelizable With |
|:---|:---|:---|:---|
| **2.1** | Config Register Read/Write + Tests | Phase 1 complete | — |
| **2.2** | Extended Mode (13-bit) + Tests | 2.1 | 2.3 |
| **2.3** | Alert Thresholds (T_LOW/T_HIGH) + Tests | 2.1 | 2.2 |
| **2.4** | Config Bit-Field Operations + Tests | 2.1 | 2.2, 2.3 |
| **2.5** | Thermostat & Alert Logic + Tests | 2.3, 2.4 | — |

**Critical Path to Phase 2 Complete:** `2.1 → 2.4 → 2.5`

> **Note:** Tasks 2.2, 2.3, and 2.4 can all be developed in parallel once Task 2.1 is complete. Task 2.5 (thermostat logic) is the final integration task and requires both thresholds (2.3) and config bit-fields (2.4) to be in place.

```text
Phase 1 Critical Path:
  1.1 ──▶ 1.2 ──┬──▶ 1.3 ──▶ 1.4 ──┐
                │                    ├──▶ 1.6 ──┬──▶ 1.7 ──┐
                └──────────▶ 1.5 ──┘           ├──▶ 1.8 ──┼──▶ 1.10
                                                └──▶ 1.9 ──┘

Phase 2 Critical Path:
  Phase 1 ──▶ 2.1 ──┬──▶ 2.2
                     ├──▶ 2.3 ──┐
                     └──▶ 2.4 ──┴──▶ 2.5
```

---

## 5. Verification & Test Plan

### 5.1 Unit & Integration Test Matrix

#### 5.1.1 Temperature Reading — Normal Mode (12-bit) · Phase 1

| # | Test Case | Input (°C) | Register Bytes [B0, B1] | Category |
|:---|:---|:---|:---|:---|
| T01 | Zero | 0.0 | `0x00, 0x00` | Boundary |
| T02 | Smallest positive step | 0.0625 | `0x00, 0x10` | LSB resolution |
| T03 | Quarter degree | 0.25 | `0x00, 0x40` | Fractional |
| T04 | Half degree | 0.5 | `0x00, 0x80` | Fractional |
| T05 | One degree | 1.0 | `0x01, 0x00` | Positive integer |
| T06 | Typical room temp | 25.0 | `0x19, 0x00` | Positive integer |
| T07 | Room temp fractional | 25.5 | `0x19, 0x80` | Positive + fraction |
| T08 | Body temperature | 37.0 | `0x25, 0x00` | Application value |
| T09 | Boiling point | 100.0 | `0x64, 0x00` | High positive |
| T10 | Max normal range | 127.9375 | `0x7F, 0xF0` | Upper boundary |
| T11 | Smallest negative step | −0.0625 | `0xFF, 0xF0` | Two's complement boundary |
| T12 | Minus one | −1.0 | `0xFF, 0x00` | Negative integer |
| T13 | Negative integer | −25.0 | `0xE7, 0x00` | Standard negative |
| T14 | Negative fractional | −25.5 | `0xE6, 0x80` | Negative + fraction |
| T15 | Small negative fraction | −0.5 | `0xFF, 0x80` | Small negative |
| T16 | Typical sensor min | −55.0 | `0xC9, 0x00` | Datasheet min operating |
| T17 | Min normal range | −128.0 | `0x80, 0x00` | Lower boundary |

#### 5.1.2 Temperature Reading — Extended Mode (13-bit) · Phase 2

| # | Test Case | Input (°C) | Register Bytes [B0, B1] | Category |
|:---|:---|:---|:---|:---|
| T18 | Just above normal max | 128.0 | `0x40, 0x00` | 13-bit threshold |
| T19 | Extended range mid | 140.0 | `0x46, 0x00` | Mid extended |
| T20 | Max extended range | 150.0 | `0x4B, 0x00` | Upper boundary |
| T21 | Extended zero | 0.0 | `0x00, 0x00` | Zero in 13-bit |
| T22 | Extended negative | −128.0 | `0xC0, 0x00` | Min 13-bit encoding |

#### 5.1.3 Error Handling · Phase 1

| # | Test Case | Expected Result | Category |
|:---|:---|:---|:---|
| T23 | NULL driver context to init | `TMP102_ERR_NULL_PTR` | Null check |
| T24 | NULL HAL pointer to init | `TMP102_ERR_NULL_PTR` | Null check |
| T25 | NULL temp output pointer | `TMP102_ERR_NULL_PTR` | Null check |
| T26 | Read temp on uninitialized driver | `TMP102_ERR_NOT_INITIALIZED` | Init guard |
| T27 | HAL i2c_write_read returns failure | `TMP102_ERR_I2C` | Error propagation |
| T28 | HAL i2c_write returns failure | `TMP102_ERR_I2C` | Error propagation |
| T29 | NULL i2c_write_read function pointer | `TMP102_ERR_NULL_PTR` | Missing HAL |
| T30 | NULL i2c_write function pointer | `TMP102_ERR_NULL_PTR` | Missing HAL |

#### 5.1.4 Initialization & Lifecycle · Phase 1

| # | Test Case | Expected Result | Category |
|:---|:---|:---|:---|
| T31 | Init with default address (0x48) | `TMP102_OK`, initialized | ADD0 = GND |
| T32 | Init with alternate address (0x49) | `TMP102_OK` | ADD0 = V+ |
| T33 | Init with address 0x4A | `TMP102_OK` | ADD0 = SDA |
| T34 | Init with address 0x4B | `TMP102_OK` | ADD0 = SCL |
| T35 | Double initialization | `TMP102_OK` (idempotent) | Re-init |
| T36 | Read after successful init | `TMP102_OK` + correct value | Full flow |

#### 5.1.5 Configuration Register · Phase 2

| # | Test Case | Expected Result | Category |
|:---|:---|:---|:---|
| T37 | Read default configuration | `0x60A0` | Power-up default |
| T38 | Enable Shutdown Mode (SD=1) | Config bit 8 set | Low-power |
| T39 | Disable Shutdown Mode (SD=0) | Config bit 8 cleared | Resume |
| T40 | Set Thermostat: Interrupt (TM=1) | Config bit 9 set | Interrupt mode |
| T41 | Set Thermostat: Comparator (TM=0) | Config bit 9 cleared | Default mode |
| T42 | Set Polarity high (POL=1) | Config bit 10 set | Active high |
| T43 | Set Fault Queue = 2 (F1:F0=01) | Bits updated | 2 faults |
| T44 | Set Fault Queue = 4 (F1:F0=10) | Bits updated | 4 faults |
| T45 | Set Fault Queue = 6 (F1:F0=11) | Bits updated | 6 faults |
| T46 | Set Conv. Rate 0.25Hz (CR=00) | Bits updated | Slowest |
| T47 | Set Conv. Rate 1Hz (CR=01) | Bits updated | 1 Hz |
| T48 | Set Conv. Rate 4Hz (CR=10) | Bits updated | Default |
| T49 | Set Conv. Rate 8Hz (CR=11) | Bits updated | Fastest |
| T50 | Enable Extended Mode (EM=1) | 13-bit enabled | Switch to 13-bit |
| T51 | Disable Extended Mode (EM=0) | 12-bit restored | Back to 12-bit |
| T52 | One-Shot in Shutdown mode | Single conversion triggered | OS bit |
| T53 | Read Alert bit (AL) | Reflects alert state | Read-only status |
| T54 | Write & read-back full config | Values match | Round-trip |

#### 5.1.6 Alert Threshold Registers (T_LOW / T_HIGH) · Phase 2

| # | Test Case | Input (°C) | Expected Result | Category |
|:---|:---|:---|:---|:---|
| T55 | Read default T_LOW | — | 75.0°C | Power-up default |
| T56 | Read default T_HIGH | — | 80.0°C | Power-up default |
| T57 | Set T_LOW = 50.0°C | 50.0 | Read-back matches | Custom |
| T58 | Set T_HIGH = 100.0°C | 100.0 | Read-back matches | Custom |
| T59 | Set T_LOW negative (−10.0°C) | −10.0 | Read-back matches | Negative |
| T60 | Set T_LOW = 0.0°C | 0.0 | Read-back matches | Boundary |
| T61 | Set T_HIGH = max (127.9375°C) | 127.9375 | Read-back matches | Upper boundary |
| T62 | Set T_LOW fractional (25.5°C) | 25.5 | Read-back matches | Fractional |

#### 5.1.7 Thermostat & Alert Logic · Phase 2

| # | Test Case | Scenario | Expected Result | Category |
|:---|:---|:---|:---|:---|
| T63 | Comparator: temp > T_HIGH | Inject above T_HIGH | Alert asserted | Basic trigger |
| T64 | Comparator: temp < T_LOW | Inject below T_LOW | Alert de-asserted | Alert clear |
| T65 | Comparator: temp in range | T_LOW < temp < T_HIGH | Previous state held | Hysteresis |
| T66 | Interrupt: temp > T_HIGH | Inject above T_HIGH | Alert triggered | Interrupt trigger |
| T67 | Interrupt: read clears alert | Read after alert | Alert cleared | Interrupt ack |
| T68 | Fault Queue=2: single fault | One reading > T_HIGH | Alert NOT triggered | Fault filtering |
| T69 | Fault Queue=2: two faults | Two readings > T_HIGH | Alert triggered | Threshold met |
| T70 | Polarity: active high | POL=1, temp > T_HIGH | Alert = 1 | Inverted polarity |
| T71 | Polarity: active low | POL=0, temp > T_HIGH | Alert = 0 | Default polarity |

#### 5.1.8 Emulator Validation · Phase 1

| # | Test Case | Expected Result | Category |
|:---|:---|:---|:---|
| T72 | Emulator init sets defaults | Config=0x60A0, T_LOW=0x4B00, T_HIGH=0x5000 | Power-up state |
| T73 | Emulator reset restores defaults | All registers reset | Clean state |
| T74 | Pointer register selects register | Write ptr=0x01, read returns config | Register addressing |
| T75 | Invalid pointer value | Handled gracefully | Robustness |
| T76 | Write to read-only temp register | Value unchanged | Register protection |
| T77 | Wrong I2C address | No response (failure) | Address filtering |

---

### 5.2 CI/CD Automation Requirements

**Platform:** GitHub Actions  
**Runner:** `ubuntu-latest`  
**Triggers:** `push` to `main`, `pull_request` targeting `main`

**Pipeline Steps:**
```
┌──────────────┐    ┌──────────────────┐    ┌──────────────────────┐
│  1. Checkout  │───▶│ 2. Install GCC   │───▶│ 3. make clean && make│
│   (actions/   │    │   (apt-get)      │    │     test             │
│   checkout)   │    │                  │    │  (Compile + Run All  │
│               │    │                  │    │   Tests)             │
└──────────────┘    └──────────────────┘    └──────────────────────┘
```

**CI Configuration (`.github/workflows/ci.yml`):**
```yaml
name: CI — Build & Test

on:
  push:
    branches: [ main ]
  pull_request:
    branches: [ main ]

jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
      - name: Checkout repository
        uses: actions/checkout@v4

      - name: Install build dependencies
        run: sudo apt-get update && sudo apt-get install -y gcc make

      - name: Build and Run Tests
        run: make clean && make test
```

**Success Criteria:** Pipeline exit code `0` — all tests pass, zero compiler warnings.

---

## 6. Sequence & Interaction Flow

### Primary Scenario: Reading Temperature

Step-by-step runtime execution of a temperature read operation:

```text
 [Test Harness]                  [TMP102 Driver]                   [HAL]                    [Emulator]
       |                               |                             |                           |
  ┌────┴────────────────────────────────┴─────────────────────────────┴───────────────────────────┴──┐
  │ STEP 1: Test injects physical condition                                                         │
  └─────────────────────────────────────────────────────────────────────────────────────────────────-─┘
       |                               |                             |                           |
       |── sim_set_ambient_temp(25.5) ──────────────────────────────────────────────────────────>|
       |                               |                             |          Converts 25.5°C  |
       |                               |                             |          to 12-bit:       |
       |                               |                             |          raw = 25.5/0.0625|
       |                               |                             |              = 408        |
       |                               |                             |          reg = 408 << 4   |
       |                               |                             |              = 0x1980     |
       |                               |                             |          Stores in        |
       |                               |                             |          temp_register    |
       |                               |                             |                           |
  ┌────┴────────────────────────────────┴─────────────────────────────┴───────────────────────────┴──┐
  │ STEP 2: Test requests temperature via Driver API                                                │
  └─────────────────────────────────────────────────────────────────────────────────────────────────-─┘
       |                               |                             |                           |
       |── tmp102_get_temperature() -->|                             |                           |
       |                               |                             |                           |
       |                               | Validates: initialized?     |                           |
       |                               |           temp_out != NULL? |                           |
       |                               |                             |                           |
  ┌────┴────────────────────────────────┴─────────────────────────────┴───────────────────────────┴──┐
  │ STEP 3: Driver invokes HAL with I2C Write-then-Read                                             │
  └─────────────────────────────────────────────────────────────────────────────────────────────────-─┘
       |                               |                             |                           |
       |                               |── hal.i2c_write_read(      |                           |
       |                               |     user_data,             |                           |
       |                               |     0x48,                  |                           |
       |                               |     [0x00], 1,    // ptr   |                           |
       |                               |     rx_buf, 2)   --------->|                           |
       |                               |                             |                           |
  ┌────┴────────────────────────────────┴─────────────────────────────┴───────────────────────────┴──┐
  │ STEP 4: HAL routes to Emulator (via function pointer)                                           │
  └─────────────────────────────────────────────────────────────────────────────────────────────────-─┘
       |                               |                             |                           |
       |                               |                             |── sim_i2c_write_read() -->|
       |                               |                             |                           |
       |                               |                             |        Checks address     |
       |                               |                             |        Sets ptr = 0x00    |
       |                               |                             |        Copies 2 bytes:    |
       |                               |                             |        rx[0] = 0x19       |
       |                               |                             |        rx[1] = 0x80       |
       |                               |                             |                           |
       |                               |                             |<──── return 1 (success) ──|
       |                               |                             |                           |
  ┌────┴────────────────────────────────┴─────────────────────────────┴───────────────────────────┴──┐
  │ STEP 5: Driver parses raw bytes                                                                 │
  └─────────────────────────────────────────────────────────────────────────────────────────────────-─┘
       |                               |                             |                           |
       |                               | raw = (0x19 << 4) |        |                           |
       |                               |       (0x80 >> 4)          |                           |
       |                               |     = 0x198 = 408          |                           |
       |                               |                             |                           |
       |                               | 408 > 0x7FF? No → positive |                           |
       |                               |                             |                           |
       |                               | temp = 408 * 0.0625        |                           |
       |                               |      = 25.5°C              |                           |
       |                               |                             |                           |
  ┌────┴────────────────────────────────┴─────────────────────────────┴───────────────────────────┴──┐
  │ STEP 6: Driver returns result                                                                   │
  └─────────────────────────────────────────────────────────────────────────────────────────────────-─┘
       |                               |                             |                           |
       |<── TMP102_OK, *temp = 25.5 ──|                             |                           |
       |                               |                             |                           |
  ┌────┴────────────────────────────────┴─────────────────────────────┴───────────────────────────┴──┐
  │ STEP 7: Test asserts result                                                                     │
  └─────────────────────────────────────────────────────────────────────────────────────────────────-─┘
       |                               |                             |                           |
    ASSERT(temp_out == 25.5)           |                             |                           |
    ASSERT(status == TMP102_OK)        |                             |                           |
       |                               |                             |                           |
    ✅ PASS                             |                             |                           |
```

---

## 7. Temperature Conversion Reference

### 7.1 Normal Mode (12-bit) — Register ↔ Celsius

**Register → Celsius (Driver direction):**
```c
// rx_buf[0] = MSB, rx_buf[1] = LSB
int16_t raw = (rx_buf[0] << 4) | (rx_buf[1] >> 4);
if (raw > 0x7FF) raw |= 0xF000;   // Sign-extend 12-bit to 16-bit
float temperature = raw * 0.0625f;
```
**Range:** −128°C to +127.9375°C

**Celsius → Register (Emulator direction):**
```c
int16_t raw = (int16_t)(temp_celsius / 0.0625f);
uint16_t reg = ((uint16_t)raw) << 4;
```

### 7.2 Extended Mode (13-bit) — Register ↔ Celsius

**Register → Celsius (Driver direction):**
```c
int16_t raw = (rx_buf[0] << 5) | (rx_buf[1] >> 3);
if (raw > 0xFFF) raw |= 0xE000;   // Sign-extend 13-bit to 16-bit
float temperature = raw * 0.0625f;
```
**Range:** −128°C to +150°C

**Celsius → Register (Emulator direction):**
```c
int16_t raw = (int16_t)(temp_celsius / 0.0625f);
uint16_t reg = ((uint16_t)raw) << 3;
```

---

## 8. Project Directory Structure

```
tmp102-virtual-driver/
├── include/
│   ├── tmp102_driver.h              # Driver public API & type definitions
│   └── tmp102_sim.h                 # Emulator public API & virtual HW struct
├── src/
│   ├── tmp102_driver.c              # Driver implementation (core logic)
│   └── tmp102_sim.c                 # Emulator implementation (virtual hardware)
├── tests/
│   └── test_main.c                  # Test harness (all test scenarios)
├── docs/
│   ├── system-design.md              # System design & architecture specification
│   ├── vision.md                    # Project vision & goals
│   ├── diagram.md                   # Architecture diagrams (ASCII art)
│   ├── RegistersMap.md              # TMP102 register map & technical reference
│   ├── implamantion_plan.md         # This document
│   └── tmp102.pdf                   # TMP102 datasheet (Texas Instruments)
├── .github/
│   └── workflows/
│       └── ci.yml                   # GitHub Actions CI pipeline
├── Makefile                         # Build system (all, test, clean)
└── README.md                        # Project README
```