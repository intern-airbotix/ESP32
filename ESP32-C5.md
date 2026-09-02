# ESP32-C5 build (branch c-5-integration)

This fork can be built for the ESP32-C5 (tested on the Seeed Studio XIAO ESP32-C5, chip rev v1.0).
Production C5 silicon needs ESP-IDF 5.5.2 or newer, while the other targets stay on ESP-IDF 5.4.x,
so the C5 build uses its own toolchain, build directory and sdkconfig:

```bash
./build_esp32c5.sh                      # build -> build-codex/esp32c5/
./build_esp32c5.sh -p /dev/ttyACM0 flash
./build_esp32c5.sh menuconfig
```

The script expects ESP-IDF at `<parent of this repo>/.toolchains/esp-idf-v5.5.5` and its tools in
`<parent>/.toolchains/espressif-v5.5.5`; override with `DRONEBRIDGE_IDF_PATH`,
`DRONEBRIDGE_IDF_TOOLS_PATH` and `DRONEBRIDGE_NODE_PATH` (e.g. `/usr` for the system Node.js).
It never edits the shell profile or the regular `build/` directory.

What C5 support changes (all target-guarded, other chips unaffected):
- `main/main.c`: Wi-Fi protocol selection goes through `db_wifi_set_2g_protocols()` because the C5
  boots in dual-band AUTO mode where `esp_wifi_set_protocol()` is rejected; the radio is pinned to
  2.4 GHz with `esp_wifi_set_band_mode()` after every `esp_wifi_start()` and on STA start.
- `main/db_esp_now.c`: C5 added to the targets that expose the noise floor; the unused
  `espressif/esp-now` component header is no longer included.
- `main/idf_component.yml`: the `espressif/esp-now` component is skipped for the C5 (it does not
  compile there; DroneBridge only uses the native ESP-NOW API).
- `frontend/dronebridge.js`: chip id 23 shown as ESP32-C5 (12 is the ESP32-C2).
- `partitions_esp32c5.csv`: 1728K app partition, still inside a 2 MB layout.

On the first boot after `erase_flash` the driver still starts in dual-band mode; pinning it to 2.4 GHz on
STA start restarts the interface once, which logs a harmless `sta is connecting, return error`. The band
mode is persisted by the Wi-Fi driver, so later boots start directly in 2.4 GHz mode.

Limits: 2.4 GHz channels 1-13 only (no 5 GHz), no status LED, reset-to-defaults button expected on
GPIO0. On the XIAO ESP32-C5 attach the u.FL antenna before powering the board.
