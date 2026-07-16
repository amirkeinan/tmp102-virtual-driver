# TMP102 Virtual Driver - Project Kickoff & Architecture

## References
- [TMP102 Datasheet](docs/tmp102.pdf)




## 1. Project Overview
A lightweight, portable, and dependency-free C driver for the TMP102 temperature sensor. The project includes a software-based hardware emulator (Mock) for comprehensive testing and validation over a virtual I2C bus without requiring physical hardware.

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
    TMP102_ERR_NOT_INITIALIZED
} tmp102_status_t;
```

### 3.2 HAL Function Pointers (Dependency Injection)
Defines the I2C operations required by the driver.
```c
typedef struct {
    // Standard Write-then-Read sequence (Repeated Start)
    int (*i2c_write_read)(uint8_t addr, uint8_t *tx_buf, size_t tx_len, uint8_t *rx_buf, size_t rx_len);
    // Standard Write sequence
    int (*i2c_write)(uint8_t addr, uint8_t *tx_buf, size_t tx_len);
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
    uint16_t temp_register;       // 0x00
    uint16_t config_register;     // 0x01 (Default power-up: 0x60A0)
    uint16_t t_low_register;      // 0x02
    uint16_t t_high_register;     // 0x03
    uint8_t  pointer_register;    // Active register pointer
} tmp102_virtual_hw_t;
```

---

## 4. Implementation Phases

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

## 5. Build System
The project uses a standard `Makefile` optimized for WSL/Linux environments.
* `make` / `make all`: Compiles the Driver and Emulator into object files.
* `make test`: Builds the test harness and executes the binary to validate the architecture.
* `make clean`: Removes compilation artifacts.

---

## 6. Interaction Flow (Sequence Example: Reading Temperature)
1. **Test Harness** calls `sim_set_ambient_temperature(25.5)`. The Emulator updates its internal `temp_register` (converting float to two's complement binary).
2. **Test Harness** calls `tmp102_get_temperature_celsius(&my_sensor, &temp_out)`.
3. **Driver** accesses the injected HAL: `my_sensor.hal.i2c_write_read(0x48, pointer_buf, 1, rx_buf, 2)`.
4. **Emulator** intercepts the HAL call, updates its `pointer_register` to `0x00`, and copies the 2 bytes from `temp_register` into `rx_buf`.
5. **Driver** receives the 2 raw bytes, shifts and masks the bits, applies the 0.0625°C resolution multiplier, and stores the result in `temp_out`.
6. **Driver** returns `TMP102_OK`.
7. **Test Harness** asserts that `temp_out == 25.5` and logs a pass/fail message.