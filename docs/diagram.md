# TMP102 Virtual Driver Architecture

This document provides a visual representation of the project architecture, dependencies, and execution flow as defined in the Kickoff document, using an ASCII art diagram style.

## 1. System Architecture Diagram

```text
+-----------------------------------------------------------------------+
|                               LAYER 1                                 |
|                      Application & Test Harness                       |
|                                                                       |
| +-------------------------------------------------------------------+ |
| |                        test_main.c                                | |
| | +-------------------+ +-------------------+ +-------------------+ | |
| | | Hardware Setup    | | Scenario Builder  | | Result Assertions | | |
| | +-------------------+ +-------------------+ +-------------------+ | |
| +-------------------------------------------------------------------+ |
+-----------------------------------------------------------------------+
         |                                                 |
         | (Backdoor physical injection)                   | (Public API calls)
         | sim_set_ambient_temperature(25.5)               | tmp102_get_temperature_celsius()
         |                                                 v
         |     +--------------------------------------------------------+
         |     |                        LAYER 2                         |
         |     |                  TMP102 Core Driver                    |
         |     |                                                        |
         |     | +----------------------------------------------------+ |
         |     | |                  tmp102_driver_t                   | |
         |     | | +---------------+ +-------------+ +--------------+ | |
         |     | | | Register Map  | | Bitwise Ops | | Status Codes | | |
         |     | | +---------------+ +-------------+ +--------------+ | |
         |     | +----------------------------------------------------+ |
         |     +--------------------------------------------------------+
         |                                                 |
         |                                                 | (Dependency Injection)
         |                                                 | my_sensor.hal.i2c_write_read(user_data, ...)
         |                                                 v
         |     +--------------------------------------------------------+
         |     |                        LAYER 3                         |
         |     |           Hardware Abstraction Layer (HAL)             |
         |     |                                                        |
         |     | +----------------------------------------------------+ |
         |     | |                 tmp102_hal_funcs_t                 | |
         |     | | +-------------------------+ +--------------------+ | |
         |     | | | int (*i2c_write_read)() | | int (*i2c_write)() | | |
         |     | | +-------------------------+ +--------------------+ | |
         |     | | +----------------------------------------------+ | |
         |     | | | void *user_data  (→ emulator state context)  | | |
         |     | | +----------------------------------------------+ | |
         |     | +----------------------------------------------------+ |
         |     +--------------------------------------------------------+
         |                                                 |
         |                                                 | (Routed call)
         v                                                 v
+-----------------------------------------------------------------------+
|                               LAYER 4                                 |
|                       TMP102 Virtual Emulator                         |
|                                                                       |
| +-------------------------------------------------------------------+ |
| |                         tmp102_sim.c                              | |
| | +-------------------------+ +-----------------------------------+ | |
| | | tmp102_virtual_hw_t     | | sim_i2c_write_read()              | | |
| | | (Registers State)       | | (Mock I2C Receiver)               | | |
| | +-------------------------+ +-----------------------------------+ | |
| +-------------------------------------------------------------------+ |
+-----------------------------------------------------------------------+
```

## 2. Interaction Flow Sequence (Reading Temperature)

```text
 [Test Harness]                  [TMP102 Driver]                   [HAL]                    [Emulator]
       |                               |                             |                           |
       |-- sim_set_ambient_temp() -------------------------------------------------------------->|
       |                               |                             |                           | (Updates internal 
       |                               |                             |                           |  register 0x00)
       |                               |                             |                           |
       |-- tmp102_get_temperature() -->|                             |                           |
       |                               |                             |                           |
       |                               |-- hal.i2c_write_read(ud) -->|                           |
       |                               |                             |                           |
       |                               |                             |-- sim_i2c_write_read(ud) ->|
       |                               |                             |                           | (Intercepts I2C, copies
       |                               |                             |                           |  2 bytes from 0x00)
       |                               |                             |<-------- [raw bytes] -----|
       |                               |                             |                           |
       |                               |<------ [raw bytes] ---------|                           |
       |                               |                             |                           |
       | (Shifts/masks bits, applies   |                             |                           |
       |  0.0625*C multiplier)         |                             |                           |
       |                               |                             |                           |
       |<------ TMP102_OK (25.5) ------|                             |                           |
       |                               |                             |                           |
    (Assert!)                          |                             |                           |
```
