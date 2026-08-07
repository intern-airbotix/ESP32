# 1. The project

## What this is

A fork of [DroneBridge for ESP32](https://github.com/DroneBridge/ESP32) (base v2.2.1) turned into a
**drop-in replacement for [mavesp8266](https://github.com/BeyondRobotix/mavesp8266)** for
**Skybrush-based drone shows**, targeting **1000+ drone** fleets.

**Why:** Airbotix flies shows with mavesp8266 telemetry radios, but mavesp8266 only supports the
legacy ESP8266. The fleet is moving to **ESP32-C6**, so this fork makes DroneBridge behave like
mavesp8266 on the wire — same ports, same discovery model — while keeping DroneBridge's web GUI and
REST configuration.

Repo: `github.com/deepak-airbotix/esp32-firmware-drone-bridge` (branch `master`).

## Releases

### v1.0 — tag `v1.0` = commit `60f792c` (2026-08-05)
The field baseline: everything the fleet had been flying, plus one patch. Exactly
`d64d876` + an 11-line change to the broadcast filter in `main/db_esp32_control.c`
(verified: that is the *only* diff, and it is blob-identical to what was running on the user's
other machine). ELF `ee69bfe4523fbc77`.

**Known-broken in v1.0** (all bench-verified, this is why v2.0 exists): it classified broadcasts by
UDP **source port**, but the Skybrush server broadcasts from the same socket it listens on
(source port 14550) — so **every server broadcast was dropped**:
- RTK / RTCM corrections (broadcast-only in the server; no unicast fallback → 100 % loss)
- the 1 Hz GCS heartbeat (its unicast variant is dead code in the server)
- GCS LED light control, RC overrides
- any "broadcast toggle" command (ARM / land / RTL) — reported to the operator as success
- and drones drifted into permanent **full-rate broadcast telemetry** (the 30 s client expiry could
  never be refreshed, because the packet that would refresh it was the dropped heartbeat)

Per-drone unicast traffic always worked, which is why shows appeared fine.

### v2.0 — tag `v2.0` = commit `f3560dd` (2026-08-06) — CURRENT
ELF `a0dd7c61e5ed61ee`. The released binary was flashed to the bench drone and re-verified before
publishing. Changes since v1.0:

| Change | Commit | Why |
|---|---|---|
| Broadcast filter by **MAVLink source sysid** (GCS 251-255, drones 1-250) | `54de1b9` | *The* fix — restores all five dead classes at once |
| **mavesp port scheme by default**: listen 14555, send 14550 | `14dbc49` | Zero-config with the stock Skybrush preset; wire-compatible with mavesp8266 |
| Fixed-port send model (learn GCS IP, always send to fixed port) | `ac11fe4` | No duplicate streams to ephemeral sockets |
| Targeting gate — radio only answers what is addressed to *it* | `db840fe` | Kills swarm-wide spurious-ACK storms |
| No radio self-heartbeat in Wi-Fi modes | `7fa4b64` | A dead FC now goes visibly silent instead of being masked |
| MAC-suffixed mDNS hostname | `534d8fb` | No fleet-wide name collisions |
| Console → USB-Serial-JTAG | `13dec07` | Frees GPIO16/17 for the FC |
| Internal-telemetry socket opened in AP mode + guard | `b11e84b` | Fixes `errno 9` spam (upstream bug too) |
| msgID-unknown log demoted to debug | `84f2d2b` | Skybrush DATA16 is unknown to the common dialect; was 1 Hz spam |
| **802.11 b/g/n default in STA**, **UDP socket self-heal**, **no reboot while armed** | `f3560dd` | Field hardening — see below |

The three `f3560dd` fixes:
1. `wifi_en_gn` default false → **true**. STA was falling back to **802.11b-only**, capping
   throughput and forcing ERP protection across the whole show BSS. (Verified: board went from
   `phy:b` to `phy:bgn`.)
2. **UDP socket self-heal** — `db_open_serial_udp_socket()` returning −1 was stored unconditionally,
   leaving a drone associated but permanently deaf. Now retries ~1×/s.
3. **No radio reboot while armed** — new `DB_FC_ARMED` flag (set from the FC heartbeat) suppresses
   the 80 s STA-loss `esp_restart()`. Previously an in-flight Wi-Fi gap rebooted the radio (telemetry
   blackout) and could strand the aircraft in AP mode for the rest of the flight.

## Current state (2026-08-06)

- `master` = `f3560dd`, pushed. Tags `v1.0` and `v2.0` published with binaries.
- Bench: one drone (ESP `A BOT N2 1` + MicoAir743v2, ArduCopter V4.6.3, sysid 104) running the
  v2.0 release binary, validated end to end against Skybrush 2.25.2.
- **The fleet has NOT been migrated yet** — see `05_OPEN_ITEMS.md` for the two-phase plan.
