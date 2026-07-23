# TMP102 Virtual Driver - Project Kickoff & Architecture

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
Every driver API function returns a standard status code.
```c
typedef enum {
    TMP102_OK = 0,
    TMP102_ERR_I2C,
    TMP102_ERR_NULL_PTR,
    TMP102_ERR_NOT_INITIALIZED,
    TMP102_ERR_INVALID_PARAM,
    TMP102_ERR_TIMEOUT
} tmp102_status_t;
```

### 3.2 HAL Function Pointers (Dependency Injection)
Defines the I2C operations required by the driver. HAL functions return `1` on success, or a negative value on failure. The driver maps any non-`1` return to `TMP102_ERR_I2C`. A `void *user_data` pointer is passed through every HAL call, allowing the implementation (e.g., the Emulator) to access its own state without relying on global variables.
```c
typedef struct {
    // Standard Write-then-Read sequence (Repeated Start)
    int (*i2c_write_read)(void *user_data, uint8_t addr, const uint8_t *tx_buf, size_t tx_len, uint8_t *rx_buf, size_t rx_len);
    // Standard Write sequence
    int (*i2c_write)(void *user_data, uint8_t addr, const uint8_t *tx_buf, size_t tx_len);
    // Opaque user context passed to every HAL call (e.g., pointer to emulator state)
    void *user_data;
} tmp102_hal_funcs_t;
```

### 3.3 Driver Context
Represents a single sensor instance, allowing multiple sensors on the same bus.
```c
typedef struct {
    uint8_t i2c_address;          // Default: 0x48
    bool initialized;
    tmp102_hal_funcs_t hal;       // Injected I2C hardware abstraction functions
} tmp102_driver_t;
```

### 3.4 Emulator State
Represents the physical registers of the TMP102 sensor.
```c
typedef struct {
    uint16_t temp_register;       // 0x00 - Temperature (Read-Only)
    uint16_t config_register;     // 0x01 - Configuration (R/W)
                                  //   Power-up default: 0x60A0
                                  //   MSB [OS=0 R1=1 R0=1 F1=0 F0=0 POL=0 TM=0 SD=0]
                                  //   LSB [CR1=1 CR0=0 AL=1 EM=0 0 0 0 0]
                                  //   → 12-bit resolution, 4Hz conversion, comparator mode
    uint16_t t_low_register;      // 0x02 - T_LOW threshold (R/W, default: 0x4B00 → 75°C)
    uint16_t t_high_register;     // 0x03 - T_HIGH threshold (R/W, default: 0x5000 → 80°C)
    uint8_t  pointer_register;    // Active register pointer (2-bit, values 0x00–0x03)
} tmp102_virtual_hw_t;
```

---

## 4. API Overview

> **Note:** Full API specifications will be detailed in per-layer implementation documents. The following signatures define the high-level contract.

### 4.1 Driver API (`include/tmp102_driver.h`)
```c
// Initialization & Lifecycle
tmp102_status_t tmp102_init(tmp102_driver_t *dev, uint8_t i2c_address, tmp102_hal_funcs_t *hal);

// Temperature Reading
tmp102_status_t tmp102_get_temperature_celsius(tmp102_driver_t *dev, float *temp_out);

// Configuration Register (Phase 2)
tmp102_status_t tmp102_read_config(tmp102_driver_t *dev, uint16_t *config_out);
tmp102_status_t tmp102_write_config(tmp102_driver_t *dev, uint16_t config);

// Alert Threshold Registers (Phase 2)
tmp102_status_t tmp102_set_t_low(tmp102_driver_t *dev, float temp_celsius);
tmp102_status_t tmp102_set_t_high(tmp102_driver_t *dev, float temp_celsius);
tmp102_status_t tmp102_get_t_low(tmp102_driver_t *dev, float *temp_out);
tmp102_status_t tmp102_get_t_high(tmp102_driver_t *dev, float *temp_out);
```

### 4.2 Emulator API (`include/tmp102_sim.h`)
```c
// Lifecycle
void sim_init(tmp102_virtual_hw_t *hw);
void sim_reset(tmp102_virtual_hw_t *hw);

// Backdoor — Physical condition injection
void sim_set_ambient_temperature(tmp102_virtual_hw_t *hw, float temp_celsius);

// I2C handlers (injected into HAL via function pointers)
int sim_i2c_write_read(void *user_data, uint8_t addr, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len);
int sim_i2c_write(void *user_data, uint8_t addr, const uint8_t *tx, size_t tx_len);
```

---

## 5. Temperature Conversion

The TMP102 stores temperature values in two's complement binary format with a resolution of **0.0625°C per LSB**.

### 5.1 Normal Mode (12-bit)
The temperature register (0x00) returns 2 bytes. The temperature is encoded in the upper 12 bits (bits [15:4]):
```
Byte 0:  [D11  D10  D9  D8  D7  D6  D5  D4]
Byte 1:  [D3   D2   D1  D0   0   0   0   0]
```
**Conversion (Register → Celsius):**
```c
int16_t raw = (rx_buf[0] << 4) | (rx_buf[1] >> 4);
if (raw > 0x7FF) raw |= 0xF000;   // Sign-extend 12-bit to 16-bit
float temperature = raw * 0.0625f;
```
**Range:** −128°C to +127.9375°C

### 5.2 Extended Mode (13-bit)
When the EM bit in the Configuration Register is set, the temperature is encoded in the upper 13 bits (bits [15:3]):
```
Byte 0:  [D12  D11  D10  D9  D8  D7  D6  D5]
Byte 1:  [D4   D3   D2   D1  D0   0   0   0]
```
**Conversion (Register → Celsius):**
```c
int16_t raw = (rx_buf[0] << 5) | (rx_buf[1] >> 3);
if (raw > 0xFFF) raw |= 0xE000;   // Sign-extend 13-bit to 16-bit
float temperature = raw * 0.0625f;
```
**Range:** −128°C to +150°C

### 5.3 Temperature-to-Register (Emulator Direction)
When the Emulator converts a float temperature to register format:
```c
// Normal Mode (12-bit)
int16_t raw = (int16_t)(temp_celsius / 0.0625f);
uint16_t reg = ((uint16_t)raw) << 4;

// Extended Mode (13-bit)
int16_t raw = (int16_t)(temp_celsius / 0.0625f);
uint16_t reg = ((uint16_t)raw) << 3;
```

---

## 6. Project Directory Structure
```
tmp102-virtual-driver/
├── include/
│   ├── tmp102_driver.h          # Driver public API
│   └── tmp102_sim.h             # Emulator public API
├── src/
│   ├── tmp102_driver.c          # Driver implementation
│   └── tmp102_sim.c             # Emulator implementation
├── tests/
│   └── test_main.c              # Test harness
├── docs/
│   ├── kickoff.md               # This document
│   ├── vision.md                # Project vision & goals
│   ├── diagram.md               # Architecture diagrams
│   └── tmp102.pdf               # TMP102 datasheet
├── .github/
│   └── workflows/
│       └── ci.yml               # GitHub Actions CI pipeline
├── Makefile                     # Build system
└── README.md                    # Project README
```

---

## 7. Implementation Phases

### Phase 1: Core Functionality (MVP)
* Setup the Build System (`Makefile`).
* Implement the Emulator state and I2C response logic.
* Implement Driver context initialization with Dependency Injection.
* Support 12-bit Normal Mode temperature reading.
* Implement error handling (`tmp102_status_t`).
* Write unit tests for positive, negative, and fractional temperature values.
* Setup a CI/CD pipeline (e.g., GitHub Actions) to automate testing and build processes.

### Phase 2: Advanced Features
* Configuration Register (0x01) read/write operations.
* Support for Extended Mode (13-bit) for temperatures > 128°C.
* Conversion Rate (CR0, CR1) and Shutdown Mode (SD) configuration.
* Fault Queue (F0, F1) and Thermostat logic simulation.

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
1. **Test Harness** calls `sim_set_ambient_temperature(25.5)`. The Emulator updates its internal `temp_register` (converting float to two's complement binary).
2. **Test Harness** calls `tmp102_get_temperature_celsius(&my_sensor, &temp_out)`.
3. **Driver** accesses the injected HAL: `my_sensor.hal.i2c_write_read(my_sensor.hal.user_data, 0x48, pointer_buf, 1, rx_buf, 2)`.
4. **Emulator** intercepts the HAL call, updates its `pointer_register` to `0x00`, and copies the 2 bytes from `temp_register` into `rx_buf`.
5. **Driver** receives the 2 raw bytes, shifts and masks the bits, applies the 0.0625°C resolution multiplier, and stores the result in `temp_out`.
6. **Driver** returns `TMP102_OK`.
7. **Test Harness** asserts that `temp_out == 25.5` and logs a pass/fail message.