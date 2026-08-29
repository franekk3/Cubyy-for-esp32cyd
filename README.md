
# Cubyy for ESP32 CYD

Cubyy for ESP32 CYD is a free, open-source speedcubing timer created for the Cheap Yellow Display (CYD) board using the Arduino IDE. It uses Bluetooth Low Energy (BLE) to connect to a GAN Smart Timer, collects state and solve results in real time, displays them on screen, and saves solve history to a microSD card.

> **Note:** Cubyy requires an external BLE timer (such as the GAN Smart Timer) to measure solves and cannot be used as a standalone manual touch timer.

---

## Requirements

- **Hardware:**
  - ESP32 CYD board (tested on `ESP32-2432S028R`)
  - GAN Smart Timer (GAN Halo)
  - MicroSD card (formatted to **FAT32**)
- **Software:**
  - Desktop computer with Arduino IDE installed

---

## Setup Guide

### 1. Add ESP32 Board Support
1. In Arduino IDE, go to **File -> Preferences**.
2. Add the following URL into **Additional boards manager URLs**:

   [https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json](https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json)

3. Go to **Tools -> Board -> Boards Manager**, search for `esp32`, and install the package by **Espressif Systems**.

### 2. Install Required Libraries
Go to **Tools -> Manage Libraries** and install the following:
- **ArduinoJson** (by Benoit Blanchon)
- **TFT_eSPI** (by Bodmer)
- **XPT2046_Touchscreen** (by Paul Stoffregen)

### 3. Uploading Code
1. Download the `Cubyy-cyd-for-esp32.ino` file from this repository.
2. Open the file in Arduino IDE, select your ESP32 board and port, then click **Upload to board**.

---

## Screenshots

<table> <tr> <td align="center"> <img src="https://github.com/franekk3/Cubyy-for-cyd/blob/main/pictures/esp32-screen1.jpg?raw=true" alt="Image1" width="300"><br> <sub> Scanning device screen</sub> </td><td align="center"> <img src="https://github.com/franekk3/Cubyy-for-cyd/blob/main/pictures/esp32-screen2.jpg?raw=true" alt="Image2" width="300"><br> <sub> Esp32 finding Gan timer </sub> </td> </tr> </table>
<table> <tr> <td align="center"> <img src="https://github.com/franekk3/Cubyy-for-cyd/blob/main/pictures/esp32-screen3.jpg?raw=true" alt="Image3" width="300"><br> <sub> Timer Screen </sub> </td><td align="center"> <img src="https://github.com/franekk3/Cubyy-for-cyd/blob/main/pictures/esp32-screen4.jpg?raw=true" alt="Image4" width="300"><br> <sub>Sample Solve </sub> </td> </tr> </table>

---

## Features

- **Scramble Generator:** Built-in 3x3 scramble generator.
- **BLE Timer Integration:** Reads timer states and exact times directly from GAN Smart Timers.
- **MicroSD Session Logging:** Saves solves to `/cubyy_session.json` on the microSD card in standard csTimer format.
- **Seamless Export:** Easily import your microSD session file into [Cubyy Web](https://cubyy.vercel.app) or any other timer supporting csTimer formats.*

*\*Solves are saved only when a compatible microSD card is inserted.*

---

## Future Roadmap

- Local solve statistics (Ao5, Ao12, Personal Bests)
- Support for additional puzzle types
- Multi-session support directly on the device
- Cloud synchronization with Cubyy Web **

*\*\*Cubyy currently does not utilize cloud servers for automatic online backups.*

---

## License

Distributed under the GPL-3.0 License.
