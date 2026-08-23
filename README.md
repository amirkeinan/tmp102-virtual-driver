# TMP102 Virtual Driver

[![CI — Build & Test](https://github.com/amirkeinan/tmp102-virtual-driver/actions/workflows/ci.yml/badge.svg)](https://github.com/amirkeinan/tmp102-virtual-driver/actions/workflows/ci.yml)

A lightweight, portable, and **dependency-free** C driver for the [TMP102 digital temperature sensor](https://www.ti.com/product/TMP102) (Texas Instruments), paired with a full **software-based hardware emulator** for comprehensive testing without physical hardware.

---

## 🎯 Project Overview & Problem Statement

Developing and testing embedded sensor drivers typically requires physical hardware, logic analyzers, and complex debug setups. This project eliminates those barriers by implementing:

1. **A production-quality TMP102 driver** — fully datasheet-compliant, portable to any I2C-capable platform
2. **A virtual hardware emulator** — faithfully simulates the TMP102 register map, I2C protocol, thermostat logic, and alert behavior entirely in software

The driver and emulator are connected through **Dependency Injection (DI)** — the driver never directly calls the emulator. Instead, I2C operations are injected as function pointers at runtime, making the driver instantly portable: swap the HAL implementation to run on STM32, Raspberry Pi, or any embedded platform with zero driver code changes.

### Key Highlights

- **79 automated tests** covering all TMP102 features, edge cases, and error conditions
- **Zero compiler warnings** under strictest flags (`-Wall -Wextra -Werror -std=c99`)
- **Zero global variables** — fully re-entrant, thread-safe by design
- **Zero dynamic allocation** — deterministic, stack-based memory usage
- **CI/CD via GitHub Actions** — automated build & test on every push

---

## 🏗️ Layered Architecture

The system is strictly divided into **4 layers** with unidirectional top-down dependencies:

```
┌─────────────────────────────────────────────────────────────┐
│  Layer 1: Test Harness               tests/test_main.c     │
│  ─ Injects simulated conditions, asserts expected behavior  │
└────────────────┬──────────────────────────┬─────────────────┘
                 │ (Backdoor injection)     │ (Public API)
                 │                          ▼
                 │  ┌────────────────────────────────────────┐
                 │  │  Layer 2: TMP102 Driver                │
                 │  │  src/tmp102_driver.c                   │
                 │  │  include/tmp102_driver.h               │
                 │  │  ─ Register parsing, bitwise ops,      │
                 │  │    config management, error handling    │
                 │  └────────────────────┬───────────────────┘
                 │                       │ (Dependency Injection)
                 │                       ▼
                 │  ┌────────────────────────────────────────┐
                 │  │  Layer 3: HAL (Function Pointers)      │
                 │  │  tmp102_hal_funcs_t struct              │
                 │  │  ─ i2c_write_read(), i2c_write(),      │
                 │  │    void *user_data                     │
                 │  └────────────────────┬───────────────────┘
                 │                       │ (Routed call)
                 ▼                       ▼
┌─────────────────────────────────────────────────────────────┐
│  Layer 4: TMP102 Emulator             src/tmp102_sim.c     │
│  include/tmp102_sim.h                                       │
│  ─ Virtual registers, I2C protocol, thermostat logic,      │
│    backdoor API for test injection                          │
└─────────────────────────────────────────────────────────────┘
```

**Dependency Rule:** No layer may call upward. The emulator never calls the driver; the driver never calls the test harness.

> ### 💡 Architectural Decision: Why is there no `src/hal.c`?
> In this architecture, Layer 3 (HAL) is an **abstract interface (contract)** rather than concrete implementation code.
> - In testing, the HAL function pointers are wired to the software emulator (`src/tmp102_sim.c`).
> - In production, they are wired to the target MCU's physical I2C peripheral drivers (e.g., STM32 HAL, ESP-IDF, Linux `/dev/i2c`).
>
> This decoupling allows the exact same compiled driver source (`src/tmp102_driver.c`) to be distributed to real-world embedded projects without carrying test or mock code.

---

## ✅ Feature Coverage

| Feature | Status | Tests |
|:--------|:------:|:-----:|
| 12-bit Temperature Reading (Normal Mode) | ✅ | T01–T17 |
| 13-bit Temperature Reading (Extended Mode) | ✅ | T18–T22 |
| Error Handling (NULL, uninitialized, HAL failures) | ✅ | T23–T30 |
| I2C Address Configuration (0x48–0x4B) | ✅ | T31–T34 |
| Initialization & Lifecycle | ✅ | T35–T36 |
| Configuration Register (R/W) | ✅ | T37, T54 |
| Bit-Field Operations (SD, TM, POL, FQ, CR, EM, OS) | ✅ | T38–T53 |
| Alert Threshold Registers (T_LOW / T_HIGH) | ✅ | T55–T62 |
| Thermostat Logic (Comparator & Interrupt modes) | ✅ | T63–T67 |
| Fault Queue Filtering | ✅ | T68–T69 |
| Alert Polarity (POL) | ✅ | T70–T71 |
| Emulator Validation (defaults, reset, protection) | ✅ | T72–T77 |

---

## 🛠️ Technology Stack & Standards

| Component | Details |
|:----------|:--------|
| **Language** | C99 (strict: `-std=c99`) |
| **Compiler Flags** | `-Wall -Wextra -Werror` — zero warnings policy |
| **Build System** | GNU Make |
| **CI/CD** | GitHub Actions (build & test on push/PR to `main`) |
| **Testing** | Custom lightweight test harness (`RUN_TEST` macro) |
| **Dependencies** | None — pure C standard library only |
| **Design Pattern** | Dependency Injection via function pointer struct |

---

## 📁 Directory Structure

```
tmp102-virtual-driver/
├── include/
│   ├── tmp102_driver.h          # Driver public API, types, constants, bit masks
│   └── tmp102_sim.h             # Emulator public API, virtual hardware state struct
├── src/
│   ├── tmp102_driver.c          # Driver implementation (Layer 2)
│   └── tmp102_sim.c             # Emulator implementation (Layer 4)
├── tests/
│   └── test_main.c              # Test harness — 79 test cases (Layer 1)
├── docs/
│   ├── system-design.md         # System architecture & design specification
│   ├── Implementation-Plan.md   # Technical WBS & task breakdown
│   ├── RegistersMap.md          # TMP102 register map & bit definitions
│   ├── diagram.md               # Architecture diagrams (ASCII art)
│   ├── vision.md                # Project vision & goals
│   └── tmp102.pdf               # TMP102 datasheet (Texas Instruments)
├── .github/
│   └── workflows/
│       └── ci.yml               # GitHub Actions CI pipeline
├── Makefile                     # Build system (all, test, clean)
└── README.md                   # This file
```

---

## 🚀 Quickstart & Build Instructions

### Prerequisites

- GCC (or any C99-compatible compiler)
- GNU Make

### Build & Test

```bash
# Clone the repository
git clone https://github.com/amirkeinan/tmp102-virtual-driver.git
cd tmp102-virtual-driver

# Build driver & emulator object files
make all

# Build and run the full test suite (79 tests)
make test

# Clean build artifacts
make clean
```

### Expected Output

```
====================================================
  TMP102 Virtual Driver Test Suite
====================================================

--- Temperature Reading: Normal 12-bit (T01 - T17) -
[ RUN      ] test_t01_temp_zero
[  PASSED  ] test_t01_temp_zero
...

====================================================
  Test Summary: 79 Run | 79 Passed | 0 Failed
====================================================
ALL TESTS PASSED
```

---

## 📐 Driver API Overview

```c
/* Initialization */
tmp102_status_t tmp102_init(tmp102_driver_t *dev, uint8_t i2c_address,
                            const tmp102_hal_funcs_t *hal);

/* Temperature Reading (auto-detects 12-bit / 13-bit mode) */
tmp102_status_t tmp102_get_temperature_celsius(tmp102_driver_t *dev, float *temp_out);

/* Configuration Register */
tmp102_status_t tmp102_read_config(tmp102_driver_t *dev, uint16_t *config_out);
tmp102_status_t tmp102_write_config(tmp102_driver_t *dev, uint16_t config);

/* Bit-Field Convenience APIs */
tmp102_status_t tmp102_set_config_bits(tmp102_driver_t *dev, uint16_t mask);
tmp102_status_t tmp102_clear_config_bits(tmp102_driver_t *dev, uint16_t mask);
tmp102_status_t tmp102_set_fault_queue(tmp102_driver_t *dev, uint8_t value);
tmp102_status_t tmp102_set_conversion_rate(tmp102_driver_t *dev, uint8_t value);
tmp102_status_t tmp102_trigger_one_shot(tmp102_driver_t *dev);

/* Alert Threshold Registers */
tmp102_status_t tmp102_set_t_low(tmp102_driver_t *dev, float temp_celsius);
tmp102_status_t tmp102_set_t_high(tmp102_driver_t *dev, float temp_celsius);
tmp102_status_t tmp102_get_t_low(tmp102_driver_t *dev, float *temp_out);
tmp102_status_t tmp102_get_t_high(tmp102_driver_t *dev, float *temp_out);
```

Every function returns `tmp102_status_t` — a deterministic error code for robust error handling:

| Status | Meaning |
|:-------|:--------|
| `TMP102_OK` | Operation completed successfully |
| `TMP102_ERR_I2C` | I2C communication failure |
| `TMP102_ERR_NULL_PTR` | NULL pointer passed as argument |
| `TMP102_ERR_NOT_INITIALIZED` | Driver not initialized |

---

## 🔌 Porting to Real Hardware

The driver is designed for instant portability. To run on physical hardware, simply implement the two I2C functions and inject them:

```c
/* Your platform-specific I2C implementation */
int my_i2c_write_read(void *user_data, uint8_t addr,
                      const uint8_t *tx, size_t tx_len,
                      uint8_t *rx, size_t rx_len) {
    /* Call your platform's I2C API (e.g., HAL_I2C_Mem_Read on STM32) */
    return 1; /* 1 = success */
}

int my_i2c_write(void *user_data, uint8_t addr,
                 const uint8_t *tx, size_t tx_len) {
    /* Call your platform's I2C write API */
    return 1;
}

/* Wire it up */
tmp102_hal_funcs_t hal = {
    .i2c_write_read = my_i2c_write_read,
    .i2c_write      = my_i2c_write,
    .user_data      = NULL  /* or your platform context */
};

tmp102_driver_t sensor;
tmp102_init(&sensor, 0x48, &hal);

float temp;
tmp102_get_temperature_celsius(&sensor, &temp);
```

**Zero driver code changes required.** The same `tmp102_driver.c` runs on the emulator, STM32, Raspberry Pi, or any other platform.

---

## 📚 Documentation

| Document | Description |
|:---------|:------------|
| [System Design](docs/system-design.md) | Architecture, data structures, test spec |
| [Implementation Plan](docs/Implementation-Plan.md) | Technical WBS & task breakdown |
| [Register Map](docs/RegistersMap.md) | TMP102 register bit definitions |
| [Architecture Diagrams](docs/diagram.md) | Visual layer & flow diagrams |
| [Project Vision](docs/vision.md) | Goals & scope boundaries |

---

## 📄 License

This project is developed as a portfolio showcase for embedded systems development.

---

*Built with ❤️ and strict C99 compliance.*
