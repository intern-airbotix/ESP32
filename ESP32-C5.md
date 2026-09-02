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
- `main/main.c`: Wi-Fi protocol selection goes through `db_wifi_set_protocols_for_band()` because the
  C5 boots in dual-band AUTO mode where `esp_wifi_set_protocol()` is rejected; the band is selected
  with `db_wifi_apply_band()` (`esp_wifi_set_band_mode()`) after every `esp_wifi_start()` and on STA
  start. `db_wifi_effective_band()` maps the `wifi_band` parameter onto what the active radio mode
  can actually do - see "2.4 GHz / 5 GHz band selection" below.
- `main/db_esp_now.c`: C5 added to the targets that expose the noise floor; the unused
  `espressif/esp-now` component header is no longer included.
- `main/idf_component.yml`: the `espressif/esp-now` component is skipped for the C5 (it does not
  compile there; DroneBridge only uses the native ESP-NOW API).
- `frontend/dronebridge.js`: chip id 23 shown as ESP32-C5 (12 is the ESP32-C2).
- `partitions_esp32c5.csv`: 1728K app partition, still inside a 2 MB layout.

On the first boot after `erase_flash` the driver still starts in dual-band mode; selecting the band on
STA start restarts the interface once, which logs a harmless `sta is connecting, return error`. The band
mode is persisted by the Wi-Fi driver, so later boots start directly in the band that was used last.

## 2.4 GHz / 5 GHz band selection

Two parameters (web GUI, REST `/api/settings`, NVS and MAVLink) control the band:

| Parameter      | MAVLink name      | Default | Meaning |
|----------------|-------------------|---------|---------|
| `wifi_band`    | `WIFI_BAND`       | 0       | 0 = 2.4 GHz only, 1 = 5 GHz only, 2 = auto (2.4 + 5 GHz, **station mode only**) |
| `wifi_chan_5g` | `WIFI_AP_CHAN_5G` | 36      | 5 GHz access point channel, 36-165 |

Behaviour per `esp32_mode`:

- **Configuration / fallback access point**: this fork has no plain AP mode - `esp32_mode` 1 always
  tries STA first and only opens an access point when the station link does not come up within 80 s.
  The same access point is opened alongside Bluetooth LE mode (6) for the web interface. Either way
  band 0 opens it on the 2.4 GHz channel `wifi_chan`, band 1 on the 5 GHz channel `wifi_chan_5g`.
  Band 2 (auto) is not possible for an access point - it is treated as 2.4 GHz with a warning.
- **AP_LR (3)** and **ESP-NOW (4, 5)**: always 2.4 GHz - Wi-Fi LR is a 2.4 GHz-only feature. A
  different `wifi_band` is logged at INFO level and ignored.
- **STA (2)**: band 0/1/2 map to `WIFI_BAND_MODE_2G_ONLY`/`5G_ONLY`/`AUTO`. The 2.4 GHz protocol
  bitmap keeps following `wifi_en_gn`; on 5 GHz and in auto mode 802.11a/n/ac/ax is enabled.
- **Bluetooth LE (6)**: the co-existence access point that serves the web interface honours
  `wifi_band` and `wifi_chan_5g` just like the fallback access point above.
- Chips without a 5 GHz radio store `wifi_band` but ignore it and log a warning once at boot.

Only the **non-DFS** channels 36, 40, 44, 48, 149, 153, 157, 161 and 165 can host an access point.
The DFS range (52-144) needs radar detection, which the firmware does not implement. `wifi_chan_5g`
has a min/max of 36-165, so a DFS channel is inside the accepted range: the settings write path
(REST `/api/settings` and MAVLink alike) therefore coerces anything that is not on the list to
channel 36 and logs an `ESP_LOGW`, and `db_ap_setup_and_start()` repeats the check at AP start to
also catch values stored by an older firmware.

A short press of the reset/boot button resets `wifi_band` to 2.4 GHz along with the SSID and
password, so the recovery access point is always reachable from a 2.4 GHz-only laptop or phone.

Two ESP-IDF constraints shape the implementation and are worth knowing when debugging:
`esp_wifi_set_band_mode()` returns `ESP_ERR_WIFI_NOT_STARTED` before `esp_wifi_start()`, and
`esp_wifi_set_config()` rejects a channel that is invalid for the band mode the driver is currently in.
Since the driver persists the band mode in NVS, a board that last ran on 2.4 GHz boots in 2.4 GHz mode
and would reject channel 36. `db_ap_setup_and_start()` therefore configures a placeholder channel
(1 on 2.4 GHz, 36 on 5 GHz) when the persisted band does not match, starts Wi-Fi, switches the band and
then re-applies the AP config with the real channel. It logs the result, e.g.
`AP running on 5 GHz channel 36`.

A third constraint: `esp_wifi_set_protocols()` does not set the 5 GHz protocol while the driver is in
2.4 GHz only mode and not the 2.4 GHz protocol while it is in 5 GHz only mode. Anything configured
before the band switch is dropped, so both `db_ap_setup_and_start()` and the `WIFI_EVENT_STA_START`
handler re-issue the protocol bitmap right after the band was applied. (A band change restarts the STA
interface and raises a second `WIFI_EVENT_STA_START`; the handler is idempotent.)

None of the 5 GHz steps aborts. A band or channel the driver rejects is a setting made from the web
interface and must not turn into a boot loop, so `db_ap_setup_and_start()` logs the error and falls
back to a 2.4 GHz access point on `wifi_chan` (look for `Falling back to a 2.4 GHz access point`
followed by the usual `AP running on ...` line).

`wifi_country.wifi_5g_channel_mask` is left at 0, which per `esp_wifi_types_generic.h` means "5 GHz
channels are allowed according to local regulatory rules" - the mask is not a required whitelist and
setting one would only ever narrow the regulatory set.

`GET /api/system/info` reports `wifi_5ghz` (1 on 5 GHz-capable chips) so the web interface can hide the
controls elsewhere; `GET /api/system/stats` reports the live `wifi_band_mode` (1/2/3, 0 = unknown or
radio off) and `wifi_channel`.

Limits: no status LED, reset-to-defaults button expected on GPIO0. On the XIAO ESP32-C5 attach the
u.FL antenna before powering the board.
