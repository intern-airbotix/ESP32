# Toolchain
This project uses the esp-idf framework v5.4.4 - it compiles for the ESP32 (Classic), ESP32-S2, ESP32-S3, ESP32-C3 & ESP32-C6.
The ESP32-C5 target is built separately with esp-idf v5.5.2 or newer via `./build_esp32c5.sh` (see ESP32-C5.md). Production C5 silicon does not work with esp-idf 5.4.

# Development Rules
For every function there must also be a doc string explaining it.
Keep a focus on safety, security, reliability and performance of the implementation.
String parameters (configuration parameters) must not be available via MAVLink parameters, it is enought if they are available via the REST:API and the web-interface.
The web-interface (frontend) is compiled to a single file with everything included except for some images. Keep it that way. Loading/Requesting additional resources from the ESP32 webserver while loading the web-interface may lead to requests not being served. The webserver may not be able to handle multiple requests (like for loading additional files).

# Limits
Do not implement OTA Updates.
ESP32-C5 supports both bands. The `wifi_band` parameter (0 = 2.4 GHz, 1 = 5 GHz, 2 = auto/station-only) selects the band and `wifi_chan_5g` the 5 GHz access point channel (non-DFS channels 36/40/44/48/149/153/157/161/165 only). All 5 GHz code is guarded with `#if CONFIG_SOC_WIFI_SUPPORT_5G`; on chips without a 5 GHz radio the parameters are stored but ignored. LR and ESP-NOW modes stay 2.4 GHz only.
Keep the non-C5 targets building with esp-idf 5.4.x; only the C5 build uses esp-idf 5.5.x.
Do not add features specifically required for drone light shows.

# Build & Execution Environment Setup
Setup a terminal with the correct environment variables and esp-idf version in order to be able to run commands. Run inside a powershell `C:\Espressif\tools\Microsoft.v5.4.4.PowerShell_profile.ps1`.
Once you ran the command inside the terminal you can also re-use the terminal for different commands.

# Build & Flash
To compile for a specific ESP32 chip/board you mus configure the project for that board first (if not configured for it already). Run `idf.py set-target esp32` inside the project root to configure it for the ESP32-Classic. Run `idf.py set-target esp32c3` inside the project root to configure it for the ESP32-C3. Run `idf.py set-target esp32c6` inside the project root to configure it for the ESP32-C6. 
Build and flash the firmware to a connected ESP32 using `idf.py build flash`. Run the commands from within the root working directory of the project. No need to monitor the full output of the commands. Just check the last bit of the output for any errors.
To just build run `idf.py build`. To just flash run `idf.py flash`

# Create a release
The version number and build index must be updated inside `main/parameters.h` and `CMakeLists.txt`.
The `$release_foldername` and `$release_name_zip` of `create_release_zip.ps1` need to be adjusted.
Run `create_release_zip.ps1`. It uses pre-defined configurations from the `config-defaults` folder. 

# How to Debug
Monitor the correct execution of the firmware by running `idf.py monitor`. Run the command from within the root working directory of the project.
You can run `idf.py fullclean` to clean the build directory. Sometimes that is not working, then manually delete the `build` folder.
If DroneBridge is running in access point mode and the computer is connected to the access point, you can access the REST:API and web-interface using the address `dronebridge.local` or `192.168.2.1`. There is no support for HTTPS.

# MCP & Documentation for esp-idf
There is an espressif-docs MCP available for using the esp-idf framework.

---

# AIRBOTIX FORK — rules for this fork specifically

Everything above is upstream DroneBridge guidance. This fork (deepak-airbotix/esp32-firmware-drone-bridge)
targets **ESP32-C6 only** and is a **drop-in mavesp8266 replacement for Skybrush drone shows**, so the
upstream limit "do not add features specifically required for drone light shows" does **not** apply here —
show support is the whole point of the fork. The environment is **Linux**, not PowerShell:
`source ~/esp/esp-idf/export.sh` then `idf.py …`.

**Read `CONTEXT/` before doing anything non-trivial** — full project history, bench setup, wire
behaviour, validation results and known-unproven items.

## Hard rules

1. **Build gate — always.** `idf.py set-target esp32c6` **before** `idf.py build`. `idf.py fullclean`
   wipes the target and the build silently reverts to `esp32`, where the USB-JTAG console option does
   not exist and is ignored — producing a wrong-chip image with no error. Before packaging, verify all three:
   ```
   grep -E '^CONFIG_IDF_TARGET="esp32c6"|^CONFIG_LWIP_NETBUF_RECVINFO=y|^CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y' sdkconfig
   ```
   Without `CONFIG_LWIP_NETBUF_RECVINFO` the whole broadcast filter is **silently disabled**.
2. **Do not use `create_release_zip.*` for fleet images** — its `config_defaults/` files drop
   `CONFIG_LWIP_NETBUF_RECVINFO`. Build with plain `idf.py build` from a clean checkout.
3. **Never `erase_flash` a configured fleet board** — it wipes NVS (Wi-Fi credentials, static IP, UART
   pins, baud). Flash the four offsets instead (`bootloader.bin` 0x0, `partition-table.bin` 0x8000,
   `db_esp32.bin` 0x10000, `www.bin` 0x190000); that preserves configuration. The **merged** bin is
   fresh-install only — it wipes NVS.
4. **Flashing does not change `udp_listen_port`** — stored NVS wins over the compiled default. Changing
   it is an explicit `POST /api/settings` + readback, per board.
5. **Do not modify flight-controller parameters** unless asked. Reading is fine.
6. **Check `udevadm info --query=property /dev/ttyACM*` before opening or flashing any port** — bench
   hardware may belong to the user or another agent, and assignments shuffle on every replug.
7. **Do not push or publish releases unless asked.**

## Wire behaviour (v2.0)

```
drone listens 14555  |  telemetry to GCS on 14550  |  TCP 5760  |  web GUI :80
no client known -> broadcast telemetry to <subnet>.255:14550
GCS packet in   -> register GCS IP at the fixed GCS port -> unicast telemetry
broadcast rule  -> accept if MAVLink src sysid > 250 (GCS), drop 1-250 (peer drone)
30 s idle       -> client expires -> discovery resumes (pinned clients never expire)
```

## Firmware map

- `main/db_esp32_control.c` — control loop, UDP socket, **broadcast filter** (sysid-based, STA only),
  client list (8 slots, 30 s idle expiry), `db_sta_gcs_port()`, discovery broadcast, internal telemetry.
- `main/db_serial.c` — two MAVLink parsers; both forward frames **before** result checks so unknown
  message IDs pass through. Radio side has the targeting gate; the serial side must stay permissive.
- `main/db_timers.c` — heartbeat (no-op in Wi-Fi modes) and RADIO_STATUS callbacks.
- `main/main.c` — Wi-Fi init, static IP, AP fallback, mDNS, `db_check_sta_link_timeout()` (80 s STA-loss
  reboot, suppressed while `DB_FC_ARMED`).
- `main/db_mavlink_msgs.c` — sets `DB_MAV_SYS_ID` and `DB_FC_ARMED` from the FC heartbeat.

MAVLink is fastmavlink with the **common dialect only** — ArduPilot messages (DATA16 = 169 etc.) parse
as "unknown" and are forwarded untouched. That is intentional, not a bug.

## Testing on Linux

- Fastest probe (no auth): `GET /api/system/stats` → `read_bytes`, `serial_dec_mav_msgs`, `udp_clients`.
- Skybrush test server + checker: `~/Documents/drone_show/skybrush-test-server/`
  (**2.25.2 is the production version** — use `skyenv-2.25`).
- Fleet provisioning: `~/dronebridge-c6-report/provision_drone.py`.
- Opening the ESP's USB serial port **resets the board**, and asserting DTR/RTS holds it in reset —
  open with `dtr=False, rts=False` (pyserial), or the board looks dead.

## Conventions

Short imperative commit subject, body explaining *why*. Trailer:
`Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`