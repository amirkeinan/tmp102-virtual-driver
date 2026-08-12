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

See [vision.md §Scope Boundaries](vision.md#-scope-boundaries) for the complete in-scope and out-of-scope definition.

---

## 2. System Architecture & Dependency Management

### 2.1 Multi-Layer Architecture

The system is strictly divided into **four layers** to enforce modularity, portability, and separation of concerns. Each layer has a single, exclusive responsibility.

See [system-design.md §2](system-design.md#2-system-architecture-the-4-layers) for the full layer descriptions and responsibilities.
See [diagram.md §1](diagram.md#1-system-architecture-diagram) for the architecture diagram.

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

All type definitions (`tmp102_status_t`, `tmp102_hal_funcs_t`, `tmp102_driver_t`, `tmp102_virtual_hw_t`) and public API signatures (Driver API & Emulator API) are defined in [system-design.md §3–§4](system-design.md#3-data-structures--state).

The following implementation-level details supplement the canonical definitions:

### 3.1 Memory Layout Summary

| Struct | Field | Type | Size | Role |
|:---|:---|:---|:---|:---|
| `tmp102_hal_funcs_t` | `i2c_write_read` | Function pointer | 8 bytes (64-bit) | Write-then-Read I2C transaction |
| | `i2c_write` | Function pointer | 8 bytes (64-bit) | Write-only I2C transaction |
| | `user_data` | `void *` | 8 bytes (64-bit) | Opaque context for HAL implementation |
| `tmp102_driver_t` | `i2c_address` | `uint8_t` | 1 byte | Sensor I2C address (valid: `0x48`, `0x49`, `0x4A`, `0x4B`) |
| | `initialized` | `bool` | 1 byte | Guards against using the driver before `tmp102_init()` |
| | `hal` | `tmp102_hal_funcs_t` | 24 bytes (64-bit) | Injected HAL function pointers and context |

### 3.2 Emulator State — Field Reference

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

See [system-design.md §9](system-design.md#9-test-case-specification) for the complete test case specification (T01–T77), covering all 8 test categories: Normal Mode temperature (T01–T17), Extended Mode temperature (T18–T22), Error Handling (T23–T30), Initialization & Lifecycle (T31–T36), Configuration Register (T37–T54), Alert Thresholds (T55–T62), Thermostat & Alert Logic (T63–T71), and Emulator Validation (T72–T77).

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

See [diagram.md §2](diagram.md#2-interaction-flow-sequence-reading-temperature) for the detailed 7-step ASCII sequence diagram showing a complete temperature read operation through all 4 layers.

---

## 7. Temperature Conversion Reference

See [RegistersMap.md §4](RegistersMap.md#4-temperature-conversion-formulas) for conversion formulas, bit layouts, and example tables covering both Normal Mode (12-bit) and Extended Mode (13-bit) register↔Celsius conversions.

---

## 8. Project Directory Structure

See [system-design.md §6](system-design.md#6-project-directory-structure) for the complete project directory tree.