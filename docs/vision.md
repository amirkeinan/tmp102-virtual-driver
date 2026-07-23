# Project Vision

## 🎯 Project Goal
The primary objective of this project is to gain practical, hands-on experience in **real-time embedded systems programming** — specifically in developing a professional-grade device driver in C.

This project is designed to simulate a realistic embedded development workflow: writing a driver against a hardware datasheet, communicating over I2C, handling register-level operations, and validating everything through a comprehensive test harness — all without requiring physical hardware.

Beyond the learning aspect, this project will serve as a **portfolio-quality showcase on GitHub**, demonstrating to potential employers that I can:
- Read and implement against a hardware datasheet (TMP102).
- Design clean, modular, and portable embedded software.
- Apply professional software engineering practices (CI/CD, testing, documentation).

---

## 👤 About Me
I am a Computer Science student aspiring to build a career in real-time embedded systems, programming primarily in C/C++.

* **Skill Level:** The project is designed to match my current abilities while pushing me to learn new concepts. Every design decision should be understandable — no "black-box" patterns. If something is complex (e.g., two's complement conversion, I2C protocol), it must be well-documented and explained.
* **Learning Outcomes:**
  - **Low-level register manipulation** — reading/writing 16-bit registers, bitwise operations, bit shifting and masking.
  - **I2C protocol fundamentals** — understanding address-based communication, write-then-read sequences, and pointer registers.
  - **Dependency Injection in C** — using function pointers and context structs to decouple the driver from specific hardware, enabling testability and portability.
  - **Embedded error handling** — designing robust status codes and defensive programming (null-pointer checks, initialization guards).
  - **Test-Driven Development (TDD)** — writing tests before or alongside implementation, covering positive cases, negative cases, edge cases, and boundary values.
  - **Build systems & CI/CD** — using Makefiles and GitHub Actions to automate compilation, testing, and quality assurance.

---

## 📝 Project Description
The project entails developing a full-featured device driver for the **TMP102 digital temperature sensor** (by Texas Instruments), designed to run entirely on a **virtual platform** — no physical hardware required.

### Why the TMP102?
The TMP102 is an ideal sensor for a learning project because:
- It uses the **I2C protocol**, one of the most common communication interfaces in embedded systems.
- It has a **small but non-trivial register map** (4 registers), providing enough complexity to learn real driver development without being overwhelming.
- It involves **interesting bitwise challenges**: 12-bit and 13-bit temperature values, two's complement encoding, and configurable alert thresholds.
- It has a **well-documented datasheet**, making it possible to implement a faithful emulator.

### The Virtual Platform Approach
Instead of requiring a physical TMP102 sensor and an I2C-capable microcontroller, this project uses a **software-based hardware emulator** (the "Emulator" or "Simulator"). This emulator:
- Maintains an internal state that mirrors the TMP102's real register map.
- Responds to I2C read/write operations exactly as the real hardware would according to the datasheet.
- Provides "backdoor" functions (e.g., `sim_set_ambient_temperature()`) to inject physical conditions for testing.

This approach means the **driver code itself is identical** to what would run on real hardware — only the HAL implementation changes. This is the core value of the Dependency Injection architecture.

---

## 📐 Architecture Overview
The project follows a strict **4-layer architecture** to ensure separation of concerns:

| Layer | Component | Role |
|-------|-----------|------|
| **Layer 1** | Test Harness (`tests/`) | Drives scenarios, injects data, asserts results |
| **Layer 2** | TMP102 Driver (`src/tmp102_driver.c`) | Core logic — register parsing, API, error handling |
| **Layer 3** | HAL (Function Pointers) | Communication bridge — abstracts I2C operations |
| **Layer 4** | TMP102 Emulator (`src/tmp102_sim.c`) | Virtual hardware — simulates the sensor's behavior |

> For detailed architecture diagrams and data flow, see [diagram.md](file:///home/amirk/tmp102-virtual-driver/docs/diagram.md).
> For full technical specification, see [kickoff.md](file:///home/amirk/tmp102-virtual-driver/docs/kickoff.md).

---

## ✅ Key Requirements

### Testing
- A comprehensive test harness that covers **all sensor features** described in the TMP102 datasheet.
- Tests for edge cases: maximum/minimum temperatures, boundary values (0°C, -0.0625°C), extended mode thresholds, and error conditions.
- Clear pass/fail output with descriptive test names.

### Documentation
- Thorough and clear documentation for every public API function, struct, and enum.
- A high-quality `README.md` at the repository root with build instructions, usage examples, and architecture overview.
- In-code comments explaining **why**, not just what — especially for bitwise operations and protocol details.

### Architecture & Portability
- The driver must be **modular and portable** — swapping the HAL implementation is all that's needed to run on a different platform (e.g., STM32, Raspberry Pi, or this virtual emulator).
- **Zero global variables** in the driver — all state is held in context structs passed by the caller.
- Dependency Injection via function pointers for all hardware-dependent operations.

### Language & API Design
- Written in **C (C99 or later)**, with a clean, intuitive, and well-encapsulated API.
- The API should feel natural to an embedded developer: `tmp102_init()`, `tmp102_get_temperature_celsius()`, `tmp102_set_config()`, etc.
- Internal implementation details must be hidden from the API consumer.

### Code Quality
- Clean code principles: meaningful names, small focused functions, proper separation of concerns.
- Consistent coding style throughout the project.
- Defensive programming: null-pointer checks, initialization guards, and meaningful error codes on every API call.

### Security & Encapsulation
- Robust API encapsulation — internal structs and helper functions are not exposed in public headers.
- Prevent unintended access to internal state; the driver context should be treated as opaque by the caller.

### CI/CD & Automation
- **GitHub Actions** pipeline that automatically compiles and runs all tests on every push and pull request.
- The CI pipeline should catch regressions immediately, ensuring the driver remains stable throughout development.

---

## 🎯 Success Criteria
The project is considered successfully completed when:

1. **Full Feature Coverage** — The driver supports all TMP102 features: temperature reading (normal + extended mode), configuration register access, alert thresholds (T_LOW / T_HIGH), conversion rate, shutdown mode, and thermostat mode.
2. **All Tests Pass** — The test harness covers every feature and edge case, and all tests pass consistently in CI.
3. **Professional Documentation** — A reader unfamiliar with the project can understand the architecture, build the project, and use the API solely from the documentation.
4. **Clean CI Pipeline** — GitHub Actions runs automatically on every push, compiling and testing the project with zero failures.
5. **Portfolio-Ready** — The repository is polished enough to be shared with potential employers as a demonstration of embedded development skills.

---

## 🚧 Scope Boundaries

### In Scope
- TMP102 driver implementation covering all datasheet features.
- Software-based TMP102 emulator for virtual I2C communication.
- Comprehensive test harness with pass/fail reporting.
- Makefile-based build system for Linux/WSL.
- GitHub Actions CI/CD pipeline.
- Full documentation (README, API docs, architecture docs).

### Out of Scope (for now)
- Physical hardware deployment (though the driver is designed to be portable to real hardware).
- Integration with a specific RTOS or microcontroller SDK.
- GUI or graphical temperature monitoring interface.
- Multi-sensor bus arbitration or advanced I2C features (clock stretching, bus recovery).
