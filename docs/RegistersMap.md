# TMP102 Register Map & Technical Reference

This document serves as a machine-readable technical reference extracted from the Texas Instruments TMP102 Datasheet for use by developers and AI development agents.

---

## 1. I2C Bus & Slave Addressing
The TMP102 address is determined by the physical connection of the **ADD0** pin.

| ADD0 Connection | 7-Bit Address (Hex) | Binary Address |
| :--- | :--- | :--- |
| **GND** | `0x48` (Default) | `1001000` |
| **V+** | `0x49` | `1001001` |
| **SDA** | `0x4A` | `1001010` |
| **SCL** | `0x4B` | `1001011` |

* **Standard Bus Speed:** Up to 400 kHz (Fast Mode) or up to 2.85 MHz (High-Speed Mode).
* **Communication Requirement:** Uses standard I2C Write-then-Read (Repeated Start) transactions.

---

## 2. Pointer Register & Memory Map
The Pointer Register (8-bit) determines which internal register is targeted for read/write operations.

| Pointer Value (Hex) | Register Name | Read / Write | Size | Default Value | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `0x00` | **Temperature Register** | Read Only | 16-bit | `0x0000` | Stores ambient temperature data |
| `0x01` | **Configuration Register** | Read / Write | 16-bit | `0x60A0` | Controls device operating modes |
| `0x02` | **T_LOW Register** | Read / Write | 16-bit | `0x4B00` | Lower threshold for thermostat alert (75°C) |
| `0x03` | **T_HIGH Register** | Read / Write | 16-bit | `0x5000` | Upper threshold for thermostat alert (80°C) |

---

## 3. Register Bit Definitions

### 3.1 Temperature Register (`0x00`) - Read-Only
The temperature register stores 12-bit or 13-bit data in two's complement format.
* **Resolution:** $0.0625^\circ\text{C}$ per LSB.

#### Normal Mode (12-bit) - `EM = 0`
* **Byte 1 (MSB):** `[D11 | D10 | D9 | D8 | D7 | D6 | D5 | D4]` (Bits 15–8)
* **Byte 2 (LSB):** `[D3  | D2  | D1 | D0 |  0 |  0 |  0 |  0]` (Bits 7–0)
* *Note:* Shift the 16-bit value right by 4 bits (`>> 4`) to extract the 12-bit signed integer.

#### Extended Mode (13-bit) - `EM = 1`
* **Byte 1 (MSB):** `[D12 | D11 | D10 | D9 | D8 | D7 | D6 | D5]` (Bits 15–8)
* **Byte 2 (LSB):** `[D4  | D3  | D2  | D1 | D0 |  0 |  0 |  0]` (Bits 7–0)
* *Note:* Shift the 16-bit value right by 3 bits (`>> 3`) to extract the 13-bit signed integer.

---

### 3.2 Configuration Register (`0x01`) - Read/Write
Default power-up value: `0x60A0` (Binary: `0110 0000 1010 0000`).

#### High Byte (Bits 15–8)
| Bit | Name | Description & Values |
| :--- | :--- | :--- |
| **15** | **OS** | **One-Shot / Extended Start:** Read: `1` when conversion complete. Write: `1` starts single conversion in Shutdown Mode. |
| **14:13** | **R1:R0** | **Converter Resolution:** Read-Only. Always `11` (12-bit / 13-bit resolution). |
| **12:11** | **F1:F0** | **Fault Queue:** Number of consecutive faults to trigger Alert. `00`=1, `01`=2, `10`=4, `11`=6. |
| **10** | **POL** | **Polarity:** `0` = Alert pin Active Low (default), `1` = Alert pin Active High. |
| **9** | **TM** | **Thermostat Mode:** `0` = Comparator mode (default), `1` = Interrupt mode. |
| **8** | **SD** | **Shutdown Mode:** `0` = Continuous conversion (default), `1` = Shutdown mode enabled. |

#### Low Byte (Bits 7–0)
| Bit | Name | Description & Values |
| :--- | :--- | :--- |
| **7:6** | **CR1:CR0** | **Conversion Rate:** `00`=0.25 Hz, `01`=1 Hz, `10`=4 Hz (default), `11`=8 Hz. |
| **5** | **AL** | **Alert Bit:** Read-Only. `1` = Default power-up state. Mirrors physical Alert pin status. |
| **4** | **EM** | **Extended Mode:** `0` = Normal Mode (12-bit, max 128°C), `1` = Extended Mode (13-bit, max 150°C). |
| **3:0** | **RESERVED**| Always `0`. |

---

## 4. Temperature Conversion Formulas

### Converting Raw Binary to Celsius (Normal Mode - 12-bit)
1. Read 2 bytes from register `0x00`.
2. Combine bytes into 16-bit value: `raw = (byte1 << 8) | byte2`.
3. Shift right by 4: `val = raw >> 4`.
4. Check sign bit (bit 11):
   * If bit 11 is `0`: `temperature_celsius = val * 0.0625`
   * If bit 11 is `1`: Sign-extend 12-bit to 16-bit negative integer: `val = val | 0xF000`, then `temperature_celsius = val * 0.0625`

### Converting Raw Binary to Celsius (Extended Mode - 13-bit)
1. Read 2 bytes from register `0x00`.
2. Combine bytes into 16-bit value: `raw = (byte1 << 8) | byte2`.
3. Shift right by 3: `val = raw >> 3`.
4. Check sign bit (bit 12):
   * If bit 12 is `0`: `temperature_celsius = val * 0.0625`
   * If bit 12 is `1`: Sign-extend 13-bit to 16-bit negative integer: `val = val | 0xE000`, then `temperature_celsius = val * 0.0625`

### Normal Mode (12-bit) Conversion Example Table
| Real Temperature | Digital Output (Hex) | Binary (12-bit) |
| :--- | :--- | :--- |
| **+127.9375°C** | `0x7FF0` | `0111 1111 1111` |
| **+100°C** | `0x6400` | `0110 0100 0000` |
| **+25°C** | `0x1900` | `0001 1001 0000` |
| **+0.25°C** | `0x0040` | `0000 0000 0100` |
| **0°C** | `0x0000` | `0000 0000 0000` |
| **-0.25°C** | `0xFFC0` | `1111 1111 1100` |
| **-25°C** | `0xE700` | `1110 0111 0000` |
| **-55°C** | `0xC900` | `1100 1001 0000` |

### Extended Mode (13-bit) Conversion Example Table
| Real Temperature | Digital Output (Hex) | Binary (13-bit) |
| :--- | :--- | :--- |
| **+150°C** | `0x4B00` | `0 1001 0110 0000` |
| **+128°C** | `0x4000` | `0 1000 0000 0000` |
| **+127.9375°C** | `0x3FF8` | `0 0111 1111 1111` |
| **+25°C** | `0x0C80` | `0 0001 1001 0000` |
| **0°C** | `0x0000` | `0 0000 0000 0000` |
| **-55°C** | `0xE480` | `1 1100 1001 0000` |
| **-128°C** | `0xC000` | `1 0000 0000 0000` |