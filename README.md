
[![Contributors][contributors-shield]][contributors-url]
[![Forks][forks-shield]][forks-url]
[![Stargazers][stars-shield]][stars-url]
[![Issues][issues-shield]][issues-url]

<br />
<div align="center">
   <img src="wiki/DroneBridgeLogo_text.png" alt="DroneBridge logo" width="400">
   <h1>DroneBridge for ESP32</h1>
</div>

A firmware for the popular ESP32 modules from Espressif Systems. Probably the cheapest way to
communicate with your drone, UAV, UAS, ground-based vehicle or whatever you may call them.

It also allows for a fully transparent serial to WiFi pass-through link with variable packet size
(As of release v2.0 no continuous stream of data is required anymore in MAVLink and transparent mode).

DroneBridge for ESP32 is a telemetry/low data rate-only solution. There is no support for cameras connected to the ESP32 
since it does not support video encoding.

![DroneBridge for ESP32 concept](wiki/db_ESP32_setup.png)

## Features
-   Bidirectional: serial-to-WiFi, serial-to-WiFi Long-Range (LR), serial-to-ESP-NOW link, Bluetooth LE
-   **Dual-Band Wi-Fi 6 (2.4 GHz & 5 GHz)** support on ESP32-C5; Wi-Fi 6 on ESP32-C6; 2.4 GHz Wi-Fi on ESP32 Classic, C3, S2 & S3
-   Support for **MAVLink**, **MSP**, **LTM** or **any other payload** using transparent option
-   Affordable: ~7€
-   Up to **150m range** using standard WiFi
-   Up to **1km of range** using ESP-NOW or Wi-Fi LR Mode - sender & receiver must be ESP32 with LR-Mode enabled
-   **Fully encrypted** in all modes including ESP-NOW broadcasts secured using AES-GCM 256 bit!
-   Weight: <8 g
-   Supported by: QGroundControl, Mission Planner, mwptools, impload etc.
-   Easy to set up: Power connection + UART connection to flight controller
-   Fully configurable through an easy-to-use web interface
-   Parsing of LTM & MSPv2 for more reliable connection and less packet loss
-   Parsing of MAVLink with the injection of Radio Status packets for the display of RSSI in the GCS
-   Fully transparent telemetry down-link option
-   Reliable, low latency

<div align="center">
    <img src="wiki/DB_ESP32_NOW_Illistration.png" alt="DroneBridge with connectionless ESP-NOW protocol support for increased range of 1km or more.">
    <div>DroneBridge for ESP32 can be used to control drone swarms at a low cost.</div>
</div>
<br />
<div>
DroneBridge for ESP32 supports ESP-NOW LR, enabling ranges of more than 1km with external receiving antennas.<br />The number of drones is only limited by the channel capacity and the ESP32s processing power. All data is encrypted using AES256-GCM.
</div>

## Drone Light Show Edition (DLSE)
![DragonDroneShow](https://github.com/user-attachments/assets/979ba184-3fab-4bdf-a315-61321d533b82)

Special version that is optimised to work for Drone Shows using [Skybrush](https://skybrush.io/)    
Check [drone-bridge.com](https://drone-bridge.com) to get the latest release!

-   Support for Skybrush
-   Remote Power Management (sleep, wakeup) of the show drone
-   Lots of additional checks to improve safety and reliability
-   [Open Source Commercial Support Suite](https://github.com/DroneBridge/DLSECommercialSupportSuite) to help you flash, manage & maintain your drones efficiently. Script your own tools following the given examples using the DLSE API.
-   Over The Air (OTA) Updates for the ESP32
-   WiFi6 (OFDMA) & 5GHz support on supported ESP32 chips
-   ESP32-C3, ESP32-C5 & ESP32-C6

## Hardware

### Supported Target Chips & Boards
DroneBridge for ESP32 supports multiple Espressif SoC generations:
- **ESP32-C5**: Dual-band Wi-Fi 6 (2.4 GHz & 5 GHz) + BLE 5 SoC (e.g., **Seeed Studio XIAO ESP32-C5** and generic ESP32-C5 boards).
- **ESP32-C6**: Wi-Fi 6 (2.4 GHz) + BLE 5 SoC (e.g., official DroneBridge HWv1.x C6 boards).
- **ESP32-C3**: Wi-Fi 4 + BLE 5 SoC (e.g., official DroneBridge HWv1.x C3 boards).
- **ESP32 (Classic)**: Dual-core Wi-Fi 4 + Bluetooth classic/BLE.
- **ESP32-S2 & ESP32-S3**: Single- and dual-core high-performance modules.

**Officially supported and tested boards:**  
Do the project and yourself a favour and use one of the officially supported and tested boards below.   
These boards are very affordable, have everything you need, and are also very compact. Perfect for use on any drone. 

**[You can find more info on how to get the official boards here!](https://dronebridge.gitbook.io/docs/dronebridge-for-esp32/hardware-and-wiring#officially-supported-boards)**

![Official Board DroneBridge for ESP32 featuring the ESP32C6](wiki/officialboard.webp)

[For further info please check the wiki!](https://dronebridge.gitbook.io/docs/dronebridge-for-esp32/hardware-and-wiring)

## ESP32-C5 Support & Technical Specifications

DroneBridge now natively supports the **Espressif ESP32-C5**, bringing **Dual-Band (2.4 GHz & 5 GHz) Wi-Fi 6** connectivity to drone telemetry links. Dual-band operation significantly avoids congested 2.4 GHz industrial/ISM frequencies, providing ultra-clean telemetry transmission on the 5 GHz spectrum.

### ESP32-C5 Chip Specifications
| Feature | Specification |
| :--- | :--- |
| **Processor** | 32-bit RISC-V single-core High-Performance (HP) CPU up to **240 MHz** + Low-Power (LP) RISC-V 32-bit core up to **48 MHz** |
| **Memory** | **400 KB** on-chip SRAM, **384 KB** ROM, 16 KB LP SRAM, external Quad/Octal SPI Flash & PSRAM support |
| **Wi-Fi Subsystem** | **Dual-Band (2.4 GHz & 5 GHz)** Wi-Fi 6 (IEEE 802.11ax/ac/n/a/b/g) |
| **Wi-Fi Bandwidth** | 20 MHz and 40 MHz channel bandwidth in both 2.4 GHz and 5 GHz bands |
| **Wi-Fi 6 Features** | OFDMA (Downlink & Uplink), MU-MIMO, Target Wake Time (TWT), 1024-QAM |
| **Long Range (LR)** | Espressif proprietary Wi-Fi LR mode supported |
| **Bluetooth** | Bluetooth 5.0 (LE), Bluetooth Mesh, 2 Mbps PHY, Long Range Coded PHY (125 kbps / 500 kbps) |
| **IEEE 802.15.4** | Thread and Zigbee 3.0 support |
| **Security** | Hardware Secure Boot, Flash Encryption (AES-128/256), Cryptographic Accelerators (RSA, ECDSA, ECC, HMAC, SHA-2) |
| **Operating Voltage** | 3.0 V ~ 3.6 V |

### Seeed Studio XIAO ESP32-C5 Board Integration
The **Seeed Studio XIAO ESP32-C5** is an ultra-compact (21 × 17.5 mm) thumb-sized development board, making it ideal for micro-drones, FPV quads, and weight-sensitive UAV builds.

| Function | Pin / GPIO | Board Marking | Description |
| :--- | :--- | :--- | :--- |
| **Telemetry UART TX** | `GPIO 11` | `D6` | Connects to Flight Controller RX (default 57600 baud, up to 921600+) |
| **Telemetry UART RX** | `GPIO 12` | `D7` | Connects to Flight Controller TX |
| **Status LED** | `GPIO 27` | `L` | Onboard user LED (Active-Low): indicates serial MAVLink/radio traffic & binding |
| **Factory Reset Button** | `GPIO 28` | `B` | Hardware button on the underside. Hold during boot to reset settings to default |
| **USB Serial / JTAG** | `GPIO 13 / 14` | `USB-C` | Direct connection to PC Ground Control Station (GCS) without FTDI adapter |
| **RF Antenna** | U.FL / IPEX | ANT | External dual-band 2.4 GHz / 5 GHz antenna connector |

### Building from Source for ESP32-C5
Using **ESP-IDF v5.5.x**:
```bash
# Set build target to esp32c5
idf.py set-target esp32c5

# Build using XIAO ESP32-C5 UART telemetry defaults
idf.py -D SDKCONFIG_DEFAULTS="config_defaults/sdkconfig.defaults.xiao.esp32c5" build

# Or build using XIAO ESP32-C5 USB-Serial (Ground Station) defaults
idf.py -D SDKCONFIG_DEFAULTS="config_defaults/sdkconfig.defaults.xiao.USBSerial.esp32c5" build

# Flash to connected board
idf.py -p /dev/ttyACM0 flash
```

## Installation/Flashing using precompiled binaries

[It is recommended that you use the official online flashing tool!](https://drone-bridge.com/flasher/)

In any other case, there are multiple ways how to flash the firmware using `esptool.py`.  
**Note for ESP32-C5**: The second-stage bootloader must be flashed at offset **`0x2000`** (rather than `0x0000` or `0x1000` used on other chips).

```bash
# ESP32-C5 (XIAO UART Telemetry):
esptool.py --chip esp32c5 -b 460800 --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 2MB --flash_freq 80m \
  0x2000 bootloader.bin 0x8000 partition-table.bin 0x10000 db_esp32.bin 0x190000 www.bin

# ESP32-C5 USB-Serial (Ground Station direct USB-C mode):
esptool.py --chip esp32c5 -b 460800 --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 2MB --flash_freq 80m \
  0x2000 bootloader.bin 0x8000 partition-table.bin 0x10000 db_esp32.bin 0x190000 www.bin
```

**[For further info please check the wiki and flashing_instructions.txt!](https://dronebridge.gitbook.io/docs/dronebridge-for-esp32/installation)**

## Wiring

1.  Connect the UART of the ESP32 to a 3.3V UART of your flight controller. It is not recommended to use the ESP32s pins that are marked with TX & RX since they often are connected to the internal serial ouput. Go for any other pin instead!
2.  Set the flight controller port to the desired protocol.

**Check out the manufacturer datasheet! Only some modules can take more than 3.3V. Follow the recommendations by the ESP32 board manufacturer for powering the device**  
**[For further info please check the wiki!](https://dronebridge.gitbook.io/docs/dronebridge-for-esp32/hardware-and-wiring)**

## Configuration
1.  Connect to the WiFi `DroneBridge ESP32` with password `dronebridge`
2.  In your browser type: `dronebridge.local` (Chrome: `http://dronebridge.local`) or `192.168.2.1` into the address bar.
 **You might need to disable the cellular connection to force the browser to use the WiFi connection**
3.  Configure as you please and hit `save`

![DroneBridge for ESP32 web interface](wiki/dbesp32_webinterface.png)

**[For further info please check the wiki!](https://dronebridge.gitbook.io/docs/dronebridge-for-esp32/configuration)**

## Use with QGroundControl, Mission Planner or any other GCS

![QGroundControl](https://docs.qgroundcontrol.com/master/assets/connected_vehicle.C1qygcZV.jpg)

-   The ESP will auto-send data to all connected devices via UDP to port 14550. QGroundControl should auto-connect using UDP
-   Connect via **TCP on port 5760** or **UDP on port 14550** to the ESP32 to send & receive data with a GCS of your choice. 
-   **In case of a UDP connection the GCS must send at least one packet (e.g. MAVLink heart beat etc.) to the UDP port of the ESP32 to register as an endpoint. Add ESP32 as an UDP target in the GCS**
-   Manually add a UDP target using the web interface

## Further Support

For questions or general chatting regarding DroneBridge for ESP32 please visit the Discord channel  
<div>
<a href="https://discord.gg/pqmHJNArE3">
<img src="wiki/discord-logo-blue.png" width="200px">
</a>
</div>

[contributors-shield]: https://img.shields.io/github/contributors/DroneBridge/ESP32.svg?style=for-the-badge
[contributors-url]: https://github.com/DroneBridge/ESP32/graphs/contributors
[forks-shield]: https://img.shields.io/github/forks/DroneBridge/ESP32.svg?style=for-the-badge
[forks-url]: https://github.com/DroneBridge/ESP32/network/members
[stars-shield]: https://img.shields.io/github/stars/DroneBridge/ESP32.svg?style=for-the-badge
[stars-url]: https://github.com/DroneBridge/ESP32/stargazers
[issues-shield]: https://img.shields.io/github/issues/DroneBridge/ESP32.svg?style=for-the-badge
[issues-url]: https://github.com/DroneBridge/ESP32/issues
