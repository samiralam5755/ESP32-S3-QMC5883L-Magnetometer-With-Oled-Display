# 🧭 ESP32-S3 Animated Digital Compass (HP5883 / QMC5883P + SH1106 OLED)

[![Platform: ESP32-S3](https://img.shields.io/badge/Platform-ESP32--S3-blue.svg)](https://www.espressif.com/)
[![Sensor: HP5883 / QMC5883P](https://img.shields.io/badge/Sensor-HP5883%20%2F%20QMC5883P%20(0x2C)-orange.svg)]()
[![Display: SH1106 1.3" OLED](https://img.shields.io/badge/Display-SH1106%20OLED%20(0x3C)-green.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

An ultra-smooth, high-precision digital compass system built with **ESP32-S3**, **HP5883 / QMC5883P Magnetometer (0x2C)**, and an **SH1106 1.3" I2C OLED Display**. 

Features real-time **Hard-Iron offset & Soft-Iron scale calibration**, **Exponential Moving Average (EMA) jitter smoothing**, a **360° animated rotating dial**, an **ASCII Serial Monitor Dashboard**, and **live axis presets**.

---

## 🌟 Key Highlights

- ⚡ **Native HP5883 / QMC5883P Driver (`0x2C`)**: Resolves the infamous "zero data / standby mode" bug present when using standard HMC/QMC libraries on newer `0x2C` magnetometer breakout modules.
- 🔄 **360° Animated Compass Dial**: 50 FPS rotating dial with 16 sub-division tick marks and a sharp fixed North pointer.
- 📊 **Dual Visualization**:
  - **OLED UI**: Digital degree readout, 16-point cardinal rose (`N`, `NNE`, `NE`, etc.), raw ADC values, and a 360° circular progress bar.
  - **Serial Visualizer**: ASCII tape needle gauge, magnetic field breakdown, and full Arduino IDE Serial Plotter CSV streaming.
- 🎯 **Hard-Iron & Soft-Iron Calibration**: Interactive on-boot Figure-8 calibration routine with on-screen graphical progress bar.
- 🕹️ **8 Live Orientation Presets**: Instantly switch axis mapping (`invertX`, `invertY`, `swapXY`) from the Serial Monitor without re-uploading code.

---

## 📌 Hardware Pin Connections (I2C Bus)

Both the OLED display and the Magnetometer sensor share the same hardware I2C bus on **GPIO 4 (SDA)** and **GPIO 5 (SCL)**:

| Component | Pin | ESP32-S3 Pin | Function |
| :--- | :--- | :--- | :--- |
| **SH1106 1.3" OLED** | `SDA` | **GPIO 4** | I2C Data Line |
| **SH1106 1.3" OLED** | `SCL` | **GPIO 5** | I2C Clock Line |
| **SH1106 1.3" OLED** | `VCC` | **3.3V / 5V** | Power Supply |
| **SH1106 1.3" OLED** | `GND` | **GND** | Ground |
| **HP5883 / QMC5883P** | `SDA` | **GPIO 4** | I2C Data Line |
| **HP5883 / QMC5883P** | `SCL` | **GPIO 5** | I2C Clock Line |
| **HP5883 / QMC5883P** | `VCC` | **3.3V** | Power Supply (3.3V Recommended) |
| **HP5883 / QMC5883P** | `GND` | **GND** | Ground |

> [!NOTE]
> Ensure both devices have pull-up resistors on the I2C lines (most breakout boards already contain onboard 4.7kΩ or 10kΩ pull-up resistors).

---

## 🧠 The HP5883 / QMC5883P (`0x2C`) Problem & Solution

Many newer GY-271 / GY-273 boards labeled as "HMC5883L" or "QMC5883L" actually contain the **QMC5883P / HP5883** chip at I2C address **`0x2C`**.

### Why standard libraries fail:
1. **Incompatible Register Maps**: Standard libraries attempt to write to `0x09` or expect data starting at `0x00`/`0x03`.
2. **Standby Mode Trap**: Register `0x0A` controls operating modes. Writing `0x00` forces the chip into Standby/Suspend mode, returning `X=0, Y=0, Z=0`.
3. **Missing Set/Reset Pulse**: Register `0x0B` must be set to `0x01` to excite the magnetic core.

### How this firmware fixes it:
- Sets register `0x0B = 0x01` (Set/Reset Period = 1).
- Configures register `0x0A = 0x0D` (Continuous Active Mode at 200 Hz ODR, 512 Oversampling).
- Reads 6-byte data sequence starting at register `0x01` (`XOUT_LSB`).
- Auto-detects `0x2C` (HP5883), `0x0D` (QMC5883L), and `0x1E` (HMC5883L).

---



## 🛠️ Required Arduino Libraries

Open **Arduino IDE** -> **Sketch** -> **Include Library** -> **Manage Libraries...** and install:

1. **`Adafruit GFX Library`** (by Adafruit)
2. **`Adafruit SH110X`** (by Adafruit)
3. **`Wire`** (Built-in ESP32 Arduino Core)

---

## 🚀 Getting Started

1. Connect the hardware according to the [Wiring Table](#-hardware-pin-connections-i2c-bus).
2. Open `ESP32_SH1106_Compass/ESP32_SH1106_Compass.ino` in Arduino IDE.
3. Select your board: **Tools** -> **Board** -> **ESP32 Arduino** -> **ESP32S3 Dev Module**.
4. Select your serial port: **Tools** -> **Port**.
5. Click **Upload**.
6. Open **Serial Monitor** at **115200 baud**.

---

## 🔄 Calibration Procedure

Magnetometers are sensitive to nearby metals, PCB traces, and components (Hard-Iron & Soft-Iron distortions).

1. Upon booting, the OLED and Serial Monitor will show **`CALIBRATION MODE`** with an 8-second countdown.
2. Slowly rotate the ESP32 and sensor assembly in a **Figure-8 motion** in all 3 axes.
3. The firmware calculates the center offset (`(max + min) / 2`) and scale factor (`avgSpan / span`) for both X and Y axes.
4. If you ever move the sensor or change location, simply type `c` into the Serial Monitor to trigger a re-calibration anytime.

---

## 🕹️ Live Serial Commands (115200 Baud)

You can send commands directly through the Arduino Serial Monitor without recompiling:

| Key | Action | Description |
| :---: | :--- | :--- |
| **`2`** | **Preset 2 (Default)** | `atan2(-Y, X)` — Corrects East/West inversion |
| **`1`** | **Preset 1** | `atan2(Y, X)` — Standard mathematical frame |
| **`3`** | **Preset 3** | `atan2(Y, -X)` — Invert X (North-South flip) |
| **`4`** | **Preset 4** | `atan2(-Y, -X)` — Invert both X and Y (180° rotation) |
| **`5`** | **Preset 5** | `atan2(X, Y)` — Swap X and Y axes (90° turn) |
| **`6`** | **Preset 6** | `atan2(X, -Y)` — Swapped X/Y + Invert X |
| **`7`** | **Preset 7** | `atan2(-X, Y)` — Swapped X/Y + Invert Y |
| **`8`** | **Preset 8** | `atan2(-X, -Y)` — Swapped X/Y + Invert Both |
| **`c`** | **Calibrate** | Starts the 8-second Figure-8 calibration routine |
| **`i`** | **Toggle N/S** | Toggles `invertX` on/off |
| **`j`** | **Toggle E/W** | Toggles `invertY` on/off |
| **`s`** | **Toggle Swap** | Toggles `swapXY` on/off |

---

## 📊 Serial Dashboard Preview

```text
==================================================================
      ESP32-S3 QMC5883L / HP5883 MAGNETOMETER VISUALIZER          
==================================================================
I2C Pins: SDA = GPIO 4, SCL = GPIO 5
Initializing I2C Wire bus...
-> Detected HP5883 / QMC5883P at address 0x2C (Chip ID: 0x80)
Sensor successfully initialized!

--------------------------------------------------------------------------
  * HEADING:   0.0 deg  |  DIRECTION: N    (North (N))
  Tape Gauge : [V . . . E . . . S . . . W . . . N]
  Raw Data   : X =   -285   | Y =    945   | Z =  -2914
  Calibrated : X =  270.0   | Y =    0.0   | Invert:[X:NO, Y:YES Swap:NO]
--------------------------------------------------------------------------
  * HEADING:  90.0 deg  |  DIRECTION: E    (East (E))
  Tape Gauge : [N . . . V . . . S . . . W . . . N]
  Raw Data   : X =   -304   | Y =   1434   | Z =  -2950
  Calibrated : X =    0.0   | Y =  347.0   | Invert:[X:NO, Y:YES Swap:NO]
--------------------------------------------------------------------------
```

---

## 📜 License

This project is open-source and licensed under the [MIT License](LICENSE).
Feel free to use, modify, and integrate it into your robotics, navigation, and IoT projects!
