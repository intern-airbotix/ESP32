# 5. Open items & fleet migration

## Fleet migration to v2.0 — two-phase, ordered

**Phase 1 — flash everything first.** Flash the entire fleet to v2.0, leaving `udp_listen_port` at
its stored value (14550 on legacy boards). A v2.0 board on 14550 is safe because the sysid filter has
replaced the port heuristic.

**Phase 2 — then flip the port fleetwide** to 14555 and set the server to the stock preset.

**Never run v1.0 and v2.0 radios on the same network.** A v2.0 drone's discovery broadcast leaves
from port 14555, which defeats v1.0's source-port filter: the v1.0 board latches onto its neighbour
as if it were the ground station, stops broadcasting, and is lost to the server until power-cycled.
(mavesp8266 coexists fine — the port scheme isolates it.)

Other migration facts:
- **Flashing does not change `udp_listen_port`** — stored NVS wins. It is an explicit
  `POST /api/settings` + readback per board.
- Use the **four individual binaries** to preserve configuration; the **merged** bin wipes NVS.
- Never `erase_flash` a configured board. Never tap the BOOT button on a running board (a short
  press resets SSID/password and forces AP mode).
- Drive tooling off the **static IP list**, not mDNS (names changed to `dronebridge-xxxxxx.local`).
- There is **no OTA** — every board is a physical flash operation.

Provisioning: `~/dronebridge-c6-report/provision_drone.py` sets and verifies `udp_listen_port=14555`,
`wifi_en_gn=1`, `radio_dis_onarm=0` and pins the GCS.
```
./provision_drone.py --check --range 1-20                 # audit only
./provision_drone.py --gcs 192.168.1.200 --range 1-20     # apply + verify
```

## Recommended per-drone show settings

| Setting | Value | Why |
|---|---|---|
| `udp_listen_port` | 14555 | stock Skybrush preset, mavesp-compatible |
| `wifi_en_gn` | 1 | 802.11 b/g/n; 0 = 11b-only and drags the whole show BSS down |
| `radio_dis_onarm` | 0 | the radio must stay up when the FC arms |
| `udp_client_ip` / `udp_client_port` | server IP / 14550 | pins the GCS: never expires, no discovery storm, no stray GCS can steal telemetry |

## Known remaining issues (none show-blocking, ranked)

1. **No Wi-Fi reconnect backoff** — every drone re-associates in lockstep after an AP blip. Add
   jittered backoff before `esp_wifi_connect()` in the disconnect handler.
2. **Blocking UART writes** — `uart_driver_install(..., tx_buffer_size = 0)` makes writes block the
   single control task, which now also carries all the previously-dropped broadcast traffic.
   One-line fix: give the driver a TX ring (`2048, 2048`).
3. **No mutex on the client list / socket** across four tasks (control loop, timer service, event
   task). Latent race, not yet observed.
4. **Per-drone RSSI never reaches Skybrush** — the server only accepts RADIO_STATUS from component
   240 (mavesp's UDP_BRIDGE); this fork uses 68 (TELEMETRY_RADIO). Changing it would reintroduce
   dead-FC masking unless guarded by FC-heartbeat liveness first.
5. **`RADIO_STATUS.txbuf` hard-coded 0** — inert with Skybrush (which drops the message), but it
   means "buffer full" to Mission Planner and similar. Set to 100.
6. **NetBIOS name still shared** (`dronebridge`) — only mDNS was made unique.
7. **`sysid > 250` threshold is hard-coded** vs the server's configurable system_id. Pin the server's
   system_id and cap fleet sysids at ≤ 250.
8. **Broadcast drops are silent** — no log line at any level. A throttled `ESP_LOGD` would turn the
   next occurrence of this bug class from a field mystery into a log grep.
9. **8-slot client list**, no MAVLink validation on registration; AP mode allows 10 stations.
10. `create_release_zip.*` config defaults are missing `CONFIG_LWIP_NETBUF_RECVINFO` — fix the files
    or keep them out of the release path.

## Fleet-scale roadmap

- **>250 drones needs network segmentation** — MAVLink sysid is a uint8 and Skybrush's per-network
  limit is 250.
- Airtime budget at 10–50 drones has been reasoned about but never measured. Measure before scaling.
- Consider an OTA partition (2 MB spare on 4 MB flash) — currently every update is physical.
- The FC on the bench has **full log storage (ENOSPC)**, which blocks arming; unrelated to the radio.

## Field items still unproven

RTK reaching RTK-FIXED (needs outdoor GPS + live NTRIP) and the no-reboot-while-armed path in real
flight. See `04_VALIDATION.md`.
