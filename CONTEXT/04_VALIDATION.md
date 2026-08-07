# 4. What has been proven — and what has not

All results below are from **real hardware** (ESP32-C6 + ArduPilot FC) on the **real show AP**,
against **Skybrush server 2.25.2** (the production version) unless stated otherwise.

## v2.0 release binary — final bench suite, 8/8 PASS (2026-08-06)

Rig: `A BOT N2 1` (static 192.168.1.1, listen 14555, `wifi_en_gn=1`) + MicoAir743v2 (ArduCopter
V4.6.3, sysid 104) @921600. Server on the **stock preset** `udp-listen://:14550?broadcast_port=14555`.

| Test | Result |
|---|---|
| FC serial link (FC → ESP) | +51 MAVLink msgs / 10 s, RSSI −37…−41 |
| Server pairing | UAV 104 listed, paired at `192.168.1.200:14550` |
| Telemetry rate | 54–71 updates / 15–20 s (~3.5 Hz) |
| Uplink (command round trip) | ARM → real prearm refusal, DISARM → success ACK |
| **GCS broadcast (sysid 255) reaches the FC** | FC executed it and returned its ArduCopter banner |
| **Peer-drone broadcast (sysid 7)** | dropped — swarm isolation holds |
| Pairing stability, server idle 3 min | 12/12 PAIRED, 0 broadcast |
| 802.11 mode | `phy:bgn` (was `phy:b` before the fix) |

The **exact published binary** was flashed and re-verified after packaging — not just the dev build.

## v1.0 → v2.0, measured side by side

| Behaviour | v1.0 | v2.0 |
|---|---|---|
| Broadcast command from the server socket → FC | **DROPPED** | **EXECUTED by both FCs** |
| Server (sysid 255) broadcast accepted | dropped | accepted |
| Peer drone (sysid 7) broadcast | leaked to the FC | dropped |
| Pairing with an idle server, 3 min | one drone stuck broadcasting **12/12** | **0/12** broadcast |

Earlier v1.0-era results still worth keeping: two drones simultaneously (246 and 551 datagrams per
60 s, worst gap 0.53 s / 0.36 s, mutual isolation correct); GCS death → clients expire at 30.1 s →
telemetry continues via broadcast → a fresh GCS receives passively and re-pairs on the first packet;
AP fallback at t+80 s with web GUI and TCP 5760 both serving; link baseline RTT ~3.6 ms median.

## Source-level audits (4 parallel Opus agents, 2026-08-06)

- All 5 v1.0-broken classes traced to **one** root cause and confirmed fixed by the sysid filter;
  the exact call path that kills the broadcast-mode drift was traced line by line.
- The 10 post-v1.0 commits were audited individually: **all sound**, no memory-safety, crash or
  protocol-parsing bugs. Concerns raised were hardening and documentation, not correctness.
- Server 2.25.2 was read to determine, per traffic class, whether it broadcasts or unicasts —
  that is where the "RTK is broadcast-only with no unicast fallback" finding comes from.

## NOT proven — do not claim these

1. **RTK actually reaching RTK-FIXED.** The transport is proven (RTCM broadcasts reach the FC), but
   a drone transitioning to RTK-fixed needs **outdoor GPS + a live NTRIP feed**. Untested.
2. **The no-reboot-while-armed fix in real flight.** Logic and plumbing verified, but bench FCs
   will not arm, so the armed code path has never executed live.
3. **UDP socket self-heal under a real failure.** A `bind()` failure cannot be forced on the bench;
   code-verified only.
4. **Fleet scale.** Everything above is 1–2 drones. Airtime, association storms and the 250-sysid
   ceiling are analysis, not measurement.
5. **Mixed v2.0 + mavesp8266 on one network.** Reasoned to be safe (the port scheme isolates them)
   and peer-drop was verified synthetically, but no real mavesp8266 unit has been on the bench.
