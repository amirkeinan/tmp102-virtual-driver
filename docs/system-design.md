# TMP102 Virtual Driver - System Design & Architecture

## References
- [TMP102 Datasheet](tmp102.pdf)
- [Project Vision](vision.md)
- [Architecture Diagram](diagram.md)



## 1. Project Overview
A lightweight, portable, and dependency-free C driver for the TMP102 temperature sensor. The project includes a software-based hardware emulator (Mock) for comprehensive testing and validation over a virtual I2C bus without requiring physical hardware.
### 1.1 Design Principles
* **Security:** The driver should be designed to be secure against unexpected inputs and outputs.
* **Portability:** The driver should be designed to be portable across different platforms.
* **Testability:** The driver should be designed to be testable.
* **Documentation:** The driver should be well-documented.
* **Performance:** The driver should be designed to be efficient and well-optimized.

## 2. System Architecture (The 4 Layers)
The project is strictly divided into four layers to ensure modularity, portability, and separation of concerns.

### Layer 1: Application & Test Harness (`tests/test_main.c`)
* **Role:** The end-user application and testing environment.
* **Responsibilities:**
  * Initializes the virtual hardware and the driver.
  * Injects simulated environmental data (e.g., ambient temperature) into the Emulator.
  * Asserts that the Driver returns the expected values.
  * Creates a scenario for each use case of the sensor, and edge cases of the sensor.
  * The test code should be well-documented and easy to understand. 
  * the tests should make sure that the driver implements all the features of the TMP102 sensor as they described in TMP102 datasheet.

### Layer 2: The TMP102 Driver (`src/tmp102_driver.c` | `include/tmp102_driver.h`)
* **Role:** The core logic.
* **Responsibilities:**
  * Exposes a clean API (e.g., `tmp102_init()`, `tmp102_get_temperature_celsius()`).
  * Manages the specific TMP102 Register Map.
  * Performs bitwise operations (parsing 12-bit/13-bit temperatures, handling two's complement).
  * **Design Rule:** Zero global variables. Operates exclusively via context structs and injected HAL function pointers.
  * **Documentation:** Ensure all API functions, structs, and the core driver logic are comprehensively documented.

### Layer 3: Hardware Abstraction Layer (HAL)
* **Role:** The communication bridge.
* **Responsibilities:**
  * Defines standard I2C operation signatures using function pointers.
  * Routes calls to either the real hardware (in a physical deployment) or the Emulator (in our test setup) via Dependency Injection.

### Layer 4: The TMP102 Emulator (`src/tmp102_sim.c` | `include/tmp102_sim.h`)
* **Role:** The Virtual Hardware.
* **Responsibilities:**
  * Maintains an internal state representing the sensor's memory array.
  * Implements the receiving end of the I2C protocol.
  * Exposes "Backdoor" functions for the Test Harness to manipulate physical conditions (`sim_set_ambient_temperature(float temp)`).
  

---

## 3. Data Structures & State

### 3.1 Status & Error Handling
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

### 3.2 HAL Function Pointers (Dependency Injection)
The Dependency Injection mechanism. Defines the I2C operations required by the driver. A `void *user_data` pointer is passed through every HAL call, allowing the implementation (e.g., the Emulator) to access its own state without relying on global variables.
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

### 3.3 Driver Context
Represents a single sensor instance. Multiple instances can coexist on the same I2C bus (different addresses).
```c
typedef struct {
    uint8_t i2c_address;          // 7-bit I2C address (0x48–0x4B)
    bool initialized;             // Initialization guard flag
    tmp102_hal_funcs_t hal;       // Injected I2C hardware abstraction functions
} tmp102_driver_t;
```

### 3.4 Emulator State
Represents the physical registers and internal state of the TMP102 sensor.
```c
typedef struct {
    // ── Hardware Registers ──
    uint16_t temp_register;       // 0x00 — Temperature (Read-Only)
    uint16_t config_register;     // 0x01 — Configuration (R/W)
                                  //   Power-up default: 0x60A0
                                  //   See RegistersMap.md §3.2 for full bit definitions
    uint16_t t_low_register;      // 0x02 — T_LOW threshold (R/W, default: 0x4B00 → 75°C)
    uint16_t t_high_register;     // 0x03 — T_HIGH threshold (R/W, default: 0x5000 → 80°C)
    uint8_t  pointer_register;    // Active register pointer (2-bit, values 0x00–0x03)

    // ── Emulator Internal State ──
    uint8_t  i2c_address;         // Emulator's own I2C slave address (0x48–0x4B)
    uint8_t  fault_count;         // Current consecutive fault counter (for Fault Queue logic)
    bool     alert_state;         // Current alert output state (for comparator/interrupt mode)
} tmp102_virtual_hw_t;
```

---

## 4. API Specification

### 4.1 Driver API (`include/tmp102_driver.h`)
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

### 4.2 Emulator API (`include/tmp102_sim.h`)
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

## 5. Temperature Conversion

See [RegistersMap.md §4](RegistersMap.md#4-temperature-conversion-formulas) for conversion formulas, bit layouts, and example tables.

---

## 6. Project Directory Structure
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
│   ├── system-design.md              # This document
│   ├── Implementation-Plan.md        # Technical specification & WBS
│   ├── vision.md                    # Project vision & goals
│   ├── diagram.md                   # Architecture diagrams (ASCII art)
│   ├── RegistersMap.md              # TMP102 register map & technical reference
│   └── tmp102.pdf                   # TMP102 datasheet (Texas Instruments)
├── .github/
│   └── workflows/
│       └── ci.yml                   # GitHub Actions CI pipeline
├── Makefile                         # Build system (all, test, clean)
└── README.md                        # Project README
```

---

## 7. Implementation Phases

The project is delivered in two phases: **Phase 1 (MVP)** covers the build system, emulator core, driver initialization, 12-bit temperature reading, error handling, unit tests, and CI/CD. **Phase 2 (Advanced Features)** adds configuration register access, 13-bit extended mode, conversion rate/shutdown control, fault queue, and thermostat logic.

See [Implementation-Plan.md §4](Implementation-Plan.md#4-implementation-work-breakdown-structure-wbs) for the detailed task breakdown, dependencies, and verification criteria.

---

## 8. Build System
The project uses a standard `Makefile` optimized for WSL/Linux environments.
* `make` / `make all`: Compiles the Driver and Emulator into object files.
* `make test`: Builds the test harness and executes the binary to validate the architecture.
* `make clean`: Removes compilation artifacts.

---

## 9. Test Case Specification

### 9.1 Temperature Reading — Normal Mode (12-bit) [Phase 1]

| # | Test Case | Input (°C) | Register Bytes [B0, B1] | Notes |
|---|-----------|-----------|------------------------|-------|
| T01 | Zero | 0.0 | `0x00, 0x00` | Zero boundary |
| T02 | Smallest positive step | 0.0625 | `0x00, 0x10` | LSB resolution verification |
| T03 | Quarter degree | 0.25 | `0x00, 0x40` | Common fractional |
| T04 | Half degree | 0.5 | `0x00, 0x80` | Common fractional |
| T05 | One degree | 1.0 | `0x01, 0x00` | Smallest positive integer |
| T06 | Typical room temp | 25.0 | `0x19, 0x00` | Standard positive integer |
| T07 | Room temp fractional | 25.5 | `0x19, 0x80` | Positive integer + fraction |
| T08 | Body temperature | 37.0 | `0x25, 0x00` | Common application value |
| T09 | Boiling point | 100.0 | `0x64, 0x00` | High positive integer |
| T10 | Max normal range | 127.9375 | `0x7F, 0xF0` | Upper boundary (12-bit max) |
| T11 | Smallest negative step | −0.0625 | `0xFF, 0xF0` | Two's complement boundary |
| T12 | Minus one | −1.0 | `0xFF, 0x00` | Simple negative integer |
| T13 | Negative integer | −25.0 | `0xE7, 0x00` | Standard negative value |
| T14 | Negative fractional | −25.5 | `0xE6, 0x80` | Negative integer + fraction |
| T15 | Small negative fraction | −0.5 | `0xFF, 0x80` | Small negative fractional |
| T16 | Typical sensor min | −55.0 | `0xC9, 0x00` | Datasheet min operating temp |
| T17 | Min normal range | −128.0 | `0x80, 0x00` | Lower boundary (12-bit min) |

### 9.2 Temperature Reading — Extended Mode (13-bit) [Phase 2]

| # | Test Case | Input (°C) | Register Bytes [B0, B1] | Notes |
|---|-----------|-----------|------------------------|-------|
| T18 | Just above normal max | 128.0 | `0x40, 0x00` | First value requiring 13-bit |
| T19 | Extended range mid | 140.0 | `0x46, 0x00` | Mid extended range |
| T20 | Max extended range | 150.0 | `0x4B, 0x00` | Upper boundary (13-bit max) |
| T21 | Extended zero | 0.0 | `0x00, 0x00` | Zero in 13-bit mode |
| T22 | Extended negative | −128.0 | `0xC0, 0x00` | Min in 13-bit encoding |

### 9.3 Error Handling [Phase 1]

| # | Test Case | Expected Result | Notes |
|---|-----------|----------------|-------|
| T23 | NULL driver context to init | `TMP102_ERR_NULL_PTR` | Defensive null check |
| T24 | NULL HAL pointer to init | `TMP102_ERR_NULL_PTR` | Defensive null check |
| T25 | NULL temp output pointer | `TMP102_ERR_NULL_PTR` | Null output parameter |
| T26 | Read temp on uninitialized driver | `TMP102_ERR_NOT_INITIALIZED` | Init guard |
| T27 | HAL i2c_write_read returns failure | `TMP102_ERR_I2C` | HAL error propagation |
| T28 | HAL i2c_write returns failure | `TMP102_ERR_I2C` | HAL error propagation |
| T29 | NULL i2c_write_read function pointer | `TMP102_ERR_NULL_PTR` | Missing HAL function |
| T30 | NULL i2c_write function pointer | `TMP102_ERR_NULL_PTR` | Missing HAL function |

### 9.4 Initialization & Lifecycle [Phase 1]

| # | Test Case | Expected Result | Notes |
|---|-----------|----------------|-------|
| T31 | Init with default address (0x48) | `TMP102_OK`, driver initialized | ADD0 = GND |
| T32 | Init with alternate address (0x49) | `TMP102_OK` | ADD0 = V+ |
| T33 | Init with address 0x4A | `TMP102_OK` | ADD0 = SDA |
| T34 | Init with address 0x4B | `TMP102_OK` | ADD0 = SCL |
| T35 | Double initialization | `TMP102_OK` (idempotent) | Re-init should succeed |
| T36 | Read after successful init | `TMP102_OK` + correct value | Full init→read flow |

### 9.5 Configuration Register [Phase 2]

| # | Test Case | Expected Result | Notes |
|---|-----------|----------------|-------|
| T37 | Read default configuration | `0x60A0` | Power-up default |
| T38 | Enable Shutdown Mode (SD=1) | Config bit 0 set | Low-power mode |
| T39 | Disable Shutdown Mode (SD=0) | Config bit 0 cleared | Resume continuous |
| T40 | Set Thermostat: Interrupt (TM=1) | Config bit 1 set | Interrupt mode |
| T41 | Set Thermostat: Comparator (TM=0) | Config bit 1 cleared | Default mode |
| T42 | Set Polarity high (POL=1) | Config bit 2 set | ALERT active high |
| T43 | Set Fault Queue = 2 (F1:F0=01) | Bits updated | 2 consecutive faults |
| T44 | Set Fault Queue = 4 (F1:F0=10) | Bits updated | 4 consecutive faults |
| T45 | Set Fault Queue = 6 (F1:F0=11) | Bits updated | 6 consecutive faults |
| T46 | Set Conv. Rate 0.25Hz (CR=00) | Bits updated | Slowest rate |
| T47 | Set Conv. Rate 1Hz (CR=01) | Bits updated | 1 Hz |
| T48 | Set Conv. Rate 4Hz (CR=10) | Bits updated | Default rate |
| T49 | Set Conv. Rate 8Hz (CR=11) | Bits updated | Fastest rate |
| T50 | Enable Extended Mode (EM=1) | 13-bit reads enabled | Switch to 13-bit |
| T51 | Disable Extended Mode (EM=0) | 12-bit reads restored | Back to 12-bit |
| T52 | One-Shot in Shutdown mode | Single conversion triggered | OS bit behavior |
| T53 | Read Alert bit (AL) | Reflects alert state | Read-only status |
| T54 | Write & read-back full config | Values match | Round-trip consistency |

### 9.6 Alert Threshold Registers (T_LOW / T_HIGH) [Phase 2]

| # | Test Case | Input (°C) | Expected Result | Notes |
|---|-----------|-----------|----------------|-------|
| T55 | Read default T_LOW | — | 75.0°C (`0x4B00`) | Power-up default |
| T56 | Read default T_HIGH | — | 80.0°C (`0x5000`) | Power-up default |
| T57 | Set T_LOW = 50.0°C | 50.0 | Read-back matches | Custom threshold |
| T58 | Set T_HIGH = 100.0°C | 100.0 | Read-back matches | Custom threshold |
| T59 | Set T_LOW negative (−10.0°C) | −10.0 | Read-back matches | Negative threshold |
| T60 | Set T_LOW = 0.0°C | 0.0 | Read-back matches | Zero boundary |
| T61 | Set T_HIGH = max (127.9375°C) | 127.9375 | Read-back matches | Upper boundary |
| T62 | Set T_LOW fractional (25.5°C) | 25.5 | Read-back matches | Fractional threshold |

### 9.7 Thermostat & Alert Logic [Phase 2]

| # | Test Case | Scenario | Expected Result | Notes |
|---|-----------|---------|----------------|-------|
| T63 | Comparator: temp > T_HIGH | Inject temp above T_HIGH | Alert asserted | Basic trigger |
| T64 | Comparator: temp < T_LOW | Inject temp below T_LOW | Alert de-asserted | Alert clear |
| T65 | Comparator: temp in range | T_LOW < temp < T_HIGH | Previous state held | Hysteresis |
| T66 | Interrupt: temp > T_HIGH | Inject temp above T_HIGH | Alert triggered | Interrupt trigger |
| T67 | Interrupt: read clears alert | Read after alert | Alert cleared | Interrupt ack |
| T68 | Fault Queue=2: single fault | One reading > T_HIGH | Alert NOT triggered | Fault filtering |
| T69 | Fault Queue=2: two faults | Two readings > T_HIGH | Alert triggered | Threshold met |
| T70 | Polarity: active high | POL=1, temp > T_HIGH | Alert = 1 | Inverted polarity |
| T71 | Polarity: active low | POL=0, temp > T_HIGH | Alert = 0 | Default polarity |

### 9.8 Emulator Validation [Phase 1]

| # | Test Case | Expected Result | Notes |
|---|-----------|----------------|-------|
| T72 | Emulator init sets defaults | Config=0x60A0, T_LOW=0x4B00, T_HIGH=0x5000 | Power-up state |
| T73 | Emulator reset restores defaults | All registers reset | Clean state |
| T74 | Pointer register selects register | Write ptr=0x01, read returns config | Register addressing |
| T75 | Invalid pointer value | Handled gracefully | Robustness |
| T76 | Write to read-only temp register | Value unchanged | Register protection |
| T77 | Wrong I2C address | No response | Address filtering |

---

## 10. Interaction Flow (Sequence Example: Reading Temperature)

See [diagram.md §2](diagram.md#2-interaction-flow-sequence-reading-temperature) for the detailed 7-step ASCII sequence diagram showing a complete temperature read operation through all 4 layers.