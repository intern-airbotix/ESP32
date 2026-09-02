
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

## Drop-in mavesp8266 replacement for drone shows (this fork)

This fork turns DroneBridge for ESP32-C6 into a drop-in replacement for
[mavesp8266](https://github.com/BeyondRobotix/mavesp8266) in Skybrush-based drone shows.
A freshly flashed board works with a stock Skybrush server without any configuration on
either side - same port scheme, same discovery behavior as a mavesp8266 radio:

| Port  | Owner | Purpose |
|-------|-------|---------|
| 14550 | GCS   | Drone sends telemetry here; Skybrush listens here |
| 14555 | Drone | Drone listens here; Skybrush sends swarm broadcasts (ARM/START/RTK) here |

Behavior (verified against Skybrush server 2.49.1 with the stock `["default"]` connection preset):

-   Boots into STA mode, joins the show Wi-Fi, falls back to a config AP if the network is absent
-   Broadcasts telemetry to `<subnet>.255:14550` until a GCS answers, then switches to unicast
    to the GCS IP at port 14550; resumes discovery if the GCS goes silent for 30 s
-   Ignores broadcasts from other drones (MAVLink source sysid 1-250) - only GCS traffic
    (sysid 251-255) is accepted, so drones on a shared show network never cross-register
-   Never answers commands addressed to the flight controller - no ACK storms on swarm commands
-   No radio self-heartbeat; RADIO_STATUS only while a GCS is connected
-   Unique per-device mDNS hostname (`dronebridge-XXXXXX.local`, from the MAC)

Deploying a fleet: flash, set the show network SSID/password and the FC UART pins/baud in the
web GUI (AP `DroneBridge for ESP32`, http://192.168.2.1) - done. Both UDP ports remain
configurable in the GUI for non-standard setups. Boards configured with firmware older than
this scheme keep their stored `udp_listen_port=14550`; set it to 14555 (or factory-reset) to
match a stock Skybrush server, or keep the server's `broadcast_port` override - both work.

### ESP32-C5 (Seeed Studio XIAO ESP32-C5) - branch `c-5-integration`

The fork also builds for the ESP32-C5, which has a dual-band radio. It needs ESP-IDF 5.5.2 or newer,
so it has its own build script and sdkconfig, see [ESP32-C5.md](ESP32-C5.md):

```bash
./build_esp32c5.sh                      # -> build-codex/esp32c5/
./build_esp32c5.sh -p /dev/ttyACM0 flash
```

Bench-verified on a XIAO ESP32-C5 (chip rev v1.0) with an ArduPilot MatekH743 on SERIAL2 at 921600:
FC TX -> XIAO D7 (GPIO12, set as `gpio_rx`), FC RX <- XIAO D6 (GPIO11, set as `gpio_tx`), 3.3 V logic.
The console is on the USB port, so those header pins are free for the FC. Tested: STA mode (join,
discovery broadcast, unicast, TCP 5760, PARAM round trip), AP fallback after 80 s, AP mode telemetry.
Test helpers are in `tools/c5-test/`.

The band is selectable in the web GUI / REST API: `wifi_band` (0 = 2.4 GHz, 1 = 5 GHz, 2 = auto -
station mode only) and `wifi_chan_5g` (channel of the configuration / fallback access point, non-DFS
channels 36/40/44/48/149/153/157/161/165 only). The fork has no plain AP mode - that access point is
the one opened after the 80 s STA fallback and alongside Bluetooth LE mode, and it honours `wifi_band`
in both cases. LR and ESP-NOW modes stay on 2.4 GHz. On chips without a 5 GHz radio the parameters are
stored but ignored. See [ESP32-C5.md](ESP32-C5.md) for the details.

Unlike mavesp8266 there is no MAVLink parameter interface on component 240 (QGC's WiFi-Bridge
settings page) - configuration is via the web GUI / REST API.

It also allows for a fully transparent serial to WiFi pass-through link with variable packet size
(As of release v2.0 no continuous stream of data is required anymore in MAVLink and transparent mode).

DroneBridge for ESP32 is a telemetry/low data rate-only solution. There is no support for cameras connected to the ESP32 
since it does not support video encoding.

![DroneBridge for ESP32 concept](wiki/db_ESP32_setup.png)

## Features
-   Bidirectional: serial-to-WiFi, serial-to-WiFi Long-Range (LR), serial-to-ESP-NOW link, Bluetooth LE
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

**Officially supported and tested boards:**  
Do the project and yourself a favour and use one of the officially supported and tested boards below.   
These boards are very affordable, have everything you need, and are also very compact. Perfect for use on any drone. 

**[You can find more info on how to get the official boards here!](https://dronebridge.gitbook.io/docs/dronebridge-for-esp32/hardware-and-wiring#officially-supported-boards)**

<img src="https://github.com/user-attachments/assets/efd2b305-8046-4431-a4a0-ec8de07fd264" alt="Official Boadrd DroneBridge for ESP32 featuring the ESP32C3" width="350">
  
**[Order the PCB yourself using the KiCAD PCB Project & Production files with private and commercial use options! Easy solder version now available!](https://buymeacoffee.com/seeul8er/extras)**

[For further info please check the wiki!](https://dronebridge.gitbook.io/docs/dronebridge-for-esp32/hardware-and-wiring)

## Installation/Flashing using precompiled binaries

[It is recommended that you use the official online flashing tool!](https://drone-bridge.com/flasher/)

In any other case, there are multiple ways how to flash the firmware.  
**[For further info please check the wiki!](https://dronebridge.gitbook.io/docs/dronebridge-for-esp32/installation)**

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

## Further Support & Donations

**If you benefited from this project please consider a donation:** 
-   [PayPal](https://www.paypal.com/donate/?hosted_button_id=SG97392AJN73J)

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
